# int16 storage scope: half-size loop buffer (the "lo-fi via bit depth" path)

Goal: store the loop buffer as 16-bit integer samples instead of 32-bit float, halving
bytes per frame. This either **doubles record time** (bump `kSourceMaxSeconds` 42 -> 84,
same SDRAM) or **frees ~23 MB of SDRAM** (keep 42 s). The "lo-fi" character comes from
16-bit quantization, not rate reduction.

Contrast with [lofi-path-b-scope.md](lofi-path-b-scope.md): this path changes only the
per-sample *storage format*. The sample **count** and sample **rate** are unchanged, so
**none** of the frame<->tempo<->tick coupling that dominates Path B applies here. That is
the entire reason to prefer it: far smaller surface area and no timing risk.

## The win, quantified

- `Buffer::Frame` is `{float l, r}` = 8 bytes today. As `{int16_t l, r}` it is 4 bytes.
- Each source buffer: ~15.5 MB -> ~7.75 MB. Three of them (`_srcBuf1/2/3`): ~46.5 MB ->
  ~23.3 MB. Frees ~23 MB of the 64 MB SDRAM (currently ~80% used).
- To spend that on time instead: `kSourceMaxSeconds` 42 -> 84 returns memory to today's
  footprint at double the length.
- `SRAM_EXEC` (code) is unaffected; the conversions add a few instructions.

## Why the surface area is small

Samples are read and written element-wise in exactly **two** places. Everything else holds
a `Frame*` and never inspects the fields, so a type change flows through automatically.

### The two conversion points (the core change)

| Site | Today | int16 change |
|------|-------|--------------|
| `Buffer::_read` (buffer.cpp:162-164) | `auto f = _buffer[frame]; out0 = f.l; out1 = f.r;` | convert on read: `out0 = f.l * (1/32768.f)` etc. The interpolation in `read_linear`/`read_cubic` runs on these floats and is unchanged. |
| `Buffer::write` (buffer.cpp:192-195) | reads `f`, mixes in float (`in*fade + f.l*fb_fade`), stores back | convert existing `f.l` int16->float, do the float mix, then **clamp to [-1,1) and quantize** back to int16. This is the only write point and where quantization + clipping happen. |

### Type + allocation (flows automatically)

| Site | Note |
|------|------|
| `Buffer::Frame` (buffer.h:16) | `float l, r` -> `int16_t l, r`. The single source of truth. |
| `_srcBuf1/2/3[aligned(kSourceBufferLength)]` (buffer.sdram.cpp:14,15,49) | typed `Buffer::Frame`, so they halve automatically. |
| `kSourceBufferLength * sizeof(Buffer::Frame)` (buffer.sdram.cpp:72) | byte math follows the type. |
| `sourceBuffer()`, `main_buf`, `raw()`, `deck.h:41`, `core.cpp:30` | pass `Frame*` only; no logic change. |
| `clear()` memset(0) | int16 zero is still silence. Fine. |

## The real ripple: persistence (SD / WAV)

This is the one area with more work than the buffer itself, because the on-disk format
changes.

| Site | Issue |
|------|-------|
| storage.cpp:106-109, 139-144 | `ad.body = raw(); body_size = rec_size() * sizeof(Frame)` dumps raw buffer bytes to the file. With int16 frames the body is now 16-bit interleaved PCM. |
| wav.h `wav_header(size)` builder | Header currently declares `AudioFormat = 3` (IEEE float), `BitsPerSample = 32`, `BytePerBloc = 8`. It must declare `AudioFormat = 1` (PCM int), `BitsPerSample = 16`, `BytePerBloc = 4`, `BytePerSec = SampleRate * 4`, or every saved file mislabels its format. |
| load path (storage + wav parser) | Loading dumps file bytes back into the raw buffer, assuming the file layout matches `Frame`. **Backward compatibility breaks**: existing 32-bit-float `.WAV` tapes on users' SD cards would load as garbage into an int16 buffer. Options: (a) accept incompatibility and document it; (b) read `BitsPerSample`/`AudioFormat` on load and convert float->int16 (and int16->float) so old and new tapes both work; (c) version the directory/format. (b) is the safe choice and is bounded work in `wav.h`'s parser + the load copy. |

## Behavior / quality decisions to make

1. **Clamping is mandatory, not optional.** The float buffer tolerates values >1.0
   (headroom); int16 wraps on overflow, which is a loud click. The write mix
   (`in*fade + f*fb_fade`, with feedback) can exceed 1.0, so it **must** clamp to the int16
   range before quantizing. (Float storage silently tolerated this; int16 does not.)
2. **Compounding quantization in overdub/feedback.** `write` re-quantizes every pass
   (existing int16 -> float -> mix -> int16). Layered overdubs accumulate quantization
   noise - tape-like degradation. This may be a desirable lo-fi feature or an objectionable
   build-up depending on taste; it is a behavior change from the float path either way.
3. **Scale + clip point.** Pick the float<->int convention (`* 32768`, clamp
   `[-32768, 32767]`) and the clip ceiling. One decision, applied at both conversion points.
4. **Optional dither.** TPDF dither before quantization trades a touch of noise floor for
   less correlated quantization distortion. A quality lever if 16-bit sounds too "digital";
   skip it if the crunch is the point.
5. **Bit-depth as the lo-fi knob.** int16 is the clean 2x. int8 (1 byte/sample) would be 4x
   time at ~48 dB SNR - much grittier; mentioned only as the axis, not recommended as the
   default.

## What does NOT change (the advantage over Path B)

- No sample rate, tempo, tick, increment, `_size`, or frame<->beat math. `rec_size()` is
  still a frame count at 48 kHz. The entire Path B risk surface (the `720000` factor, the
  `_size`/increment triangle, spread caps) is **untouched**.
- Interpolation (`read_linear`/`read_cubic`) - operates on the floats `_read` produces.
- Grain engine, clock, FX, modulation, UI, detector (separate float buffers) - all unchanged.

## Comparison

| | Path B (half-rate) | int16 storage |
|---|---|---|
| Record time | 2x (84 s) | 2x (84 s) or keep 42 s + free ~23 MB |
| Lo-fi character | aliasing / rate reduction | 16-bit quantization |
| Touches timing/tempo/tick math | yes (the hard part) | **no** |
| Core change | decimate-on-write + increment baseline + `_size` design call | two convert points + clamp |
| Persistence impact | declare stored rate in header | change PCM format (16-bit) + load-compat for old float tapes |
| Main risk | `_size`/increment coupling, loop-sync factor | clipping/quantization behavior; backward-compat with existing tapes |
| Can combine | -- | yes: int16 + half-rate = 4x |

## Recommendation + test plan

int16 is the smaller, safer experiment and a good first move - it delivers the same 2x with
none of Path B's timing risk. Recommended order:

1. Decide the persistence policy (incompatible vs convert-on-load). Convert-on-load (b)
   keeps users' tapes working and is the only part with real subtlety.
2. Change `Frame` to int16; add convert+clamp at `_read` and `write`; pick scale/clip.
3. Bump `kSourceMaxSeconds` to 84 (or leave at 42 to bank the SDRAM).
4. Update the `wav.h` builder to 16-bit PCM and the parser/load path to handle both depths.

Test first, in the host harness (`test/`): the float<->int16 convert+clamp is a pure free
function - unit-test round-trip, clipping at +/-1, and quantization step. The `wav.h`
builder/parser already has host tests (`test_wav.cpp`); extend them to assert a 16-bit PCM
header round-trips and that a legacy 32-bit-float header is still detected on load. Both are
pure logic and need no hardware. The only thing that can't be host-tested is the audible
overdub-quantization build-up (decision 2) - that needs an ear on hardware.

## Bottom line

Two conversion points, one type change, a clamp, and a persistence-format update with a
load-compat shim. No timing math. It is the lower-risk way to double record time, and it
composes with Path B later for 4x if desired.
