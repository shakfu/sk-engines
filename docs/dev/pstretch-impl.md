# Dev notes — real-time PaulStretch (`pstretch` engine)

Implementation, design, and roadmap for `ENGINE=pstretch` — a clean-room, real-time PaulStretch ambient
time-smear. This is the developer doc (architecture, the source-mode plan, the file map, risks); a
user-facing control reference will live in `docs/engines/pstretch.md` once the source modes settle.

## Status — pick up here (2026-06-30)

**Working and hardware-verified clean.** Flashed on the H7 (spotykach); boots, runs, audio is glitch-free
at the full **4096 / 85 ms** window. CPU (via `METER=1`): **avg ~32% / max ~64%** with both decks. Builds
on Make + CMake (SRAM_EXEC ~77%, SRAM ~72%); `make -C host test-pstretch` is green.

Done:
- **Live smear** + **Freeze** (Play pad) + **Capture/hold** (Rev pad — grab the recent ~5 s and loop the
  stretch *through* it). Dual-deck (A=L, B=R), routing, crossfade, dry/wet, ENV tone, ±1-oct pitch.
- **Real-time performance** solved (see that section below): FFT working set in on-chip SRAM, the hop
  pipelined across blocks, finely-chunked worker ticks. This was the hard part and it is finished.
- **Mode-switch phase A/B retired** (the recompute path + its host test removed); the Mode switch is now
  **unused / free** for the Phase-2 source select.
- Docs (this file, `docs/engines/pstretch.md`, controls diagram) and the `CHANGELOG.md` entry are current.

NOT done / next session:
1. **By-ear character pass** — the one thing only a human can sign off: does the 85 ms wash sound right?
   Tune the per-algorithm ranges (stretch curve, diffusion, default tone) to taste on hardware.
2. **Phase 2: SD-file source mode** — fully scoped in "Roadmap" below. The big realization: the slow read
   head makes SD bandwidth trivial, so stream (don't RAM-load) arbitrarily long clips on the `radio`
   `StreamDeck` stack. The Mode switch becomes Live/Capture/SD; Aux becomes the clip slot.
3. **8192 window option** — now affordable (avg is only 32% at 4096); the vendored FFT takes any 2^k. Would
   give a lusher wash. Wire it as a control or a constant + re-measure.
4. **`float` position rebase** — `_wpos`/`_rpos` lose integer precision after ~6 min continuous (2^24
   samples). Keep them bounded before relying on long unbroken sessions.

Tuning knobs (all in the engine/voice): `kWindow=4096`, `kHop`, `kRing=2^18` (~5.5 s ring / capture span),
`kWorkBudget=14` (worker ticks/block, shared across voices — raise if audio underruns, lower if a block
overruns), `kChunk=512` (work granularity), `kLut=1024` (smear cos/sin table).

Measuring CPU: build `make engine-pstretch METER=1`; the load meter prints to the **FS_EXTERNAL** USB-CDC
(the spotykach USB-C — the app must be *running*, not in bootloader). Read `/dev/cu.usbmodem*` (`screen`/
`cat`). NOTE: `max` is a **sticky running peak** (won't drop on a fix) — watch `avg`. pstretch needs an
audio **input** (it's an effect, not a generator) and the wet output is delayed; use Freeze/Capture or a
live source to hear it. **Nothing is committed** (user commits).

## What it is

PaulStretch (Nasca Octavian Paul) turns audio into a diffuse, evolving spectral wash: FFT large
overlapping windows, RANDOMIZE the phases (keep magnitudes), overlap-add back to time. Advancing the
analysis read head slowly — or freezing it — stretches/smears time arbitrarily.

This engine is a **clean-room** reimplementation from the published algorithm description (NOT derived
from any GPL source / essej's JUCE port), so it stays **MIT** like the rest of the platform (unlike the
GPLv3 `glitch`/`qdelay`). It is **self-contained**: a vendored radix-2 FFT (`engine/pstretch/fft.h`), no
CMSIS-DSP — CMSIS-DSP's sources are not compiled into this firmware's `libdaisy.a` and its
`arm_rfft_fast_f32` caps at 4096, whereas a vendored FFT runs identically on the host test and the device
and takes any power-of-two window.

## The central idea: the source is orthogonal to the stretch

The PaulStretch DSP only ever reads grains from a ring buffer. *What fills that ring* is independent of
the stretch math, so one engine can offer several **source modes** behind a selector, all sharing the
FFT, window, phase-smear, freeze, and every knob:

```
            +- Live:    the audio input writes the ring in real time (read head chases it)
ring buffer +- Capture: the input filled the ring, then froze (read head loops a captured span)
            +- SD-file: a clip streams into the ring slowly, kept ahead of the read head
                          |
                          v
                  PaulStretch read head (windowed grain -> FFT -> phase smear -> IFFT -> overlap-add)
```

### Why SD-file streaming is feasible on the H7 (the key insight)

The analysis head advances at `input_rate / stretch`. At 50x stretch it consumes source at 1/50 real time
(~1 KB/s), so an SD-file source does NOT need the whole (multi-minute) clip in RAM — it is *streamed*
slowly, keeping only a few seconds windowed around the slow read head. SD bandwidth is trivial and the
clip can be arbitrarily long (a 3-minute file at 50x plays for ~2.5 hours). This is what makes the
classic "stretch a song into an hour-long drone" practical on the device, and it reuses the same
`StreamDeck` stack (`SPK_USE_STREAM`, lock-free SDRAM ring + main-loop FatFs pump, slot scan/seek) that
the `radio` engine already uses — so SD mode is *reuse*, not a new storage subsystem.

## DSP (engine/pstretch/pstretch_voice.h)

One mono `Stretcher` per deck. Per hop (hop = window/2, 50% overlap):

1. Extract a windowed grain at the read head, pitch-resampled (linear interp). Window = the PaulStretch
   `(1 - x^2)^1.25`, x in [-1,1] (flat-topped, smooth edges).
2. Forward FFT (vendored radix-2; real input via a full complex transform).
3. **Phase smear**: new phase = original + `diffusion * random offset`. Implemented as a complex rotation
   `X' = X * e^{i*phi}` (preserves |X| with no atan2/sqrt). diffusion=0 reconstructs the grain (clean),
   diffusion=1 gives a uniformly random phase (full wash). Conjugate symmetry kept (bins k / n-k, real
   DC/Nyquist) so the inverse is real.
4. Inverse FFT, window again, overlap-add into the accumulator, emit `hop` samples normalized by the
   50%-overlap COLA gain `1/(w[i]^2 + w[i+hop]^2)`.
5. Advance the read head by `hop / stretch` input samples (frozen = hold -> static evolving drone).

In **live** mode the head is clamped to the written, not-yet-overwritten span of the ring; when it falls
too far behind (large stretch) it holds at the trailing edge and the advancing write head drags it
forward at real time — so the "stretch" is heard as an evolving smear of the recent past, NOT the literal
present. **Freeze** is how you drone on the current instant. (This delayed-smear behaviour is inherent to
real-time stretching; the capture/SD modes instead loop/stream a fixed source, so the stretch plays
*through* the material the classic way.)

### Real-time performance (the bit that actually matters on hardware)

A naive "do the whole hop when the output runs dry" pull model is unusable on the device, even though the
*average* CPU is low. Measured with `METER=1` (the load meter on the FS_EXTERNAL USB-CDC), the journey was
**avg 200% / max 4297%  ->  avg 32% / max 64%**, via three fixes, in order of impact:

1. **FFT working set in on-chip SRAM, not the SDRAM arena** (avg 200% -> 40%). The FFT hammers `re/im` and
   the twiddle/bit-reversal tables with *scattered* access; on the H7 scattered SDRAM (FMC) access is ~10x
   slower than on-chip SRAM. The whole FFT working set (`re/im`, `ola`, window, tables, the cos/sin LUT) is
   engine `.bss` (SRAM); only the big per-voice input ring (~1 MB) stays in the SDRAM arena, where it is
   read roughly sequentially. **General lesson for H7 DSP engines: keep FFT/scattered buffers in SRAM.**

2. **Pipeline the hop across blocks** (max 4297% -> 145%). A whole 4096 hop in one audio block overruns it
   and underruns the DMA (a glitch) regardless of average load. The Stretcher is a worker: `write_input()`
   captures input, `work(budget)` advances a state machine, `drain()` pulls from an output FIFO. The engine
   shares a small `kWorkBudget` across both voices each block. Because a hop's output lasts ~21 blocks,
   there is slack to compute the *next* hop incrementally. (This also forced `re/im` to be per-voice, since
   the two voices' transforms now overlap in time.)

3. **Fine, uniform work ticks** (max 145% -> 64% at the full 4096 window). Coarse ticks (a whole FFT stage,
   or a whole extract/smear/ola pass) still spiked a block even though avg was only ~36%. So `FFT::step`
   slices by **butterflies** (`Job{stage, idx}`, `maxBf` per call) and extract/smear/ola process `kChunk`
   (512) elements per tick. Every tick is now bounded, so `kWorkBudget` (14) flattens the per-block load to
   ~avg. (An interim build dropped the window to 2048 for headroom; the finer ticks let 4096/85 ms come
   back at max 64%.)

The cos/sin **LUT** for the smear (random phase -> table quantization inaudible) was added during this but
turned out not to be the bottleneck (the FFT/buffer work was). The earlier **phase A/B** (an optimized
complex rotation vs an `atan2`/`sqrt` recompute, selectable on the Mode switch) was confirmed equivalent
on the host test and showed no measurable load difference, so the recompute path was **retired** - only
the rotation remains, and the Mode switch is now free (for the Phase-2 source select).

## Engine (engine/pstretch/pstretch_engine.{h,cpp}, class `PstretchEngine`)

Dual-deck (A = left input, B = right, like the delay), `CapOwnDisplay | CapDualDeck`. Per-deck rings +
the shared FFT/window scratch are sub-allocated from the injected SDRAM arena. Audio path: each voice
produces a wet stream; the engine applies dry/wet, an ENV tone low-pass, the routing-switch stereo
placement, and the crossfader A/B blend, soft-limited.

### Control map (current, Phase 1)

| Control | Effect |
|---|---|
| **SIZE** | Stretch 1x..64x (exponential) — read-head crawl speed |
| **POS** | Diffusion 0..1 (clean window resynthesis .. full phase-randomized wash) |
| **PITCH** | Pitch shift +/- 1 octave (grain resample) |
| **ENV** | Output tone (one-pole low-pass) |
| **MIX** | Dry/wet |
| **Play pad** | Freeze the read head (evolving drone on the current spot) — per deck |
| **Rev pad** | Capture/hold (latched): grab the recent ring and loop the stretch *through* it — per deck |
| **Crossfade** | A/B blend of the two decks |
| **Routing switch** | Stereo topology (Stereo / DoubleMono / GenerativeStereo) |
| **Mode switch** | unused (free for the Phase-2 source select) |

## Roadmap — all three source modes, staged

**Phase 1 (live + capture; no SD).** Live smear (done) + a latched **capture/hold** gesture on the Rev
pad: snapshot the recent ring, stop writing live input, and loop the read head *through* the captured
span so a big stretch traverses the whole captured phrase (the classic PaulStretch drone from a few
seconds of input). Cheap, no new infrastructure. Ship + tune the stretch *sound* and measure CPU on
hardware before adding streaming.

**Phase 2 (SD-file source).** Add an SD-clip source on the `StreamDeck` stack (`radio` is the template):
scan a folder of clips, stream the selected one slowly into the analysis ring ahead of the read head,
loop or stop at end. Control rework: the **Mode switch** becomes the SOURCE selector (Live / Capture /
SD), the phase A/B retires off it, **Alt+PITCH (Aux)** becomes the clip-slot selector (like radio banks,
add `CapAux`), and the diffusion-vs-scrub contention on POS is resolved (scrub on Alt+POS, or diffusion
fixed in SD mode). The streaming pump lives in `prepare()` and must throttle (the read head is slower
than real time, so the ring must not overrun).

**Deferred.** Per-deck independent clips, longer windows (the vendored FFT supports 8192 for a smoother
wash; 4096/85 ms today), an in-RAM record-to-slot, and a bigger capture ring.

## Files

- `src/engine/pstretch/fft.h` — vendored radix-2 complex FFT (MIT, clean-room), arena-backed tables.
- `src/engine/pstretch/pstretch_voice.h` — the mono `Stretcher` (ring, read head, window, phase smear,
  OLA; live clamp + capture loop).
- `src/engine/pstretch/pstretch_engine.{h,cpp}` — the `IEngine` dual-deck wrapper.
- `host/test_pstretch.cpp` — FFT round-trip/known-bin, finite/bounded/produces-signal, dry passthrough,
  stretch changes output, freeze, capture loops, routing.
- Registered in `engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake`, `host/Makefile` (+
  `engine-pstretch` one-shot targets).

## Risks / open items

- **CPU: measured, comfortable.** `METER=1` on hardware reads avg ~32% / max ~64% at 4096/48 kHz with both
  decks (see Real-time performance above) — glitch-free. `kWorkBudget` (14) and `kChunk` (512) are the
  spread/throughput knobs; raise budget if audio underruns, lower it if a block ever overruns.
- **Free-running write head as `float`** — `_wpos`/`_rpos` are floats, so positions lose integer precision
  after ~6 min of continuous runtime (2^24 samples). Rebase them (keep bounded) before relying on long
  unbroken sessions; not yet done.
- **Window is 4096 (85 ms)** — a good smear; 8192 (smoother still) is a follow-on (the vendored FFT
  supports any 2^k; cost is CPU + buffer size, and there is headroom).
- **Delayed-smear UX** in live mode (above) — intentional; capture/SD modes are the "play through the
  material" answer.
- **Build status:** SRAM engine, links + fits (`SRAM_EXEC` ~77% Make, SRAM ~72%), host test green,
  **hardware-verified clean** (audio + CPU meter).
