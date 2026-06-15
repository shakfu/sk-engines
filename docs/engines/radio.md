# radio engine

`ENGINE=radio` · `src/engine/radio/radio_engine.{h,cpp}` · class `RadioEngine`

A **dual virtual [RadioMusic](https://github.com/TomWhitwell/RadioMusic)**. Each deck (A/B) is an
**independent virtual radio** browsing one shared SD library of banks; the two radios are blended by the
platform crossfader and placed by the routing switch, so it plays like a pair of radios you tune and mix.

Built on the same SD-streaming stack as the [tape](tape.md) engine (the platform `StreamDeck` service,
gated by `SPK_USE_STREAM`), so it inherits the lock-free ring + main-loop pump and adds only what
RadioMusic needs: a headerless raw-16-bit codec, a directory scan, and the free-running playhead.

> **Status: builds + host-tested; hardware bring-up pending.** `make -j8 ENGINE=radio` links at ~82.8%
> SRAM_EXEC (no record path, no Faust kernels). `make -C host test` includes `test-radio` (raw codec,
> station/bank quantization, the playhead modulo, varispeed, static, RESET) - all green. The pot/pad/
> LED/CV paths have no host harness and are unverified on the device.

---

## The free-running virtual playhead

The defining RadioMusic behaviour: every station sounds as though it kept broadcasting while you were
tuned elsewhere. A single monotonic **frame clock** advances each audio block; tuning to a station opens
its file and seeks to `(clock + START) mod station_length` before streaming forward, so each station
lands at its "live" position. Leaving a station and returning finds it advanced, exactly like a radio.

The raw 16-bit format makes this cheap: a station's length is `filesize / 2` (a pure `f_stat`, no header
parse) and a seek is `f_lseek(frame * 2)`. The file open + seek happens off the audio path in `prepare()`
(main loop); the audio ISR only ever drains the per-deck ring.

---

## Controls (per deck)

| Control | `ParamId` / config | Effect |
|---|---|---|
| **PITCH** (`Speed`) | + `cv_voct` (V/oct jack) | **STATION** select - the tuning dial. Knob + CV summed, quantized to the nearest station in the bank. |
| **POS** (`Pos`) | + `cv_size_pos` (size/pos jack) | **START** offset into the station (applied on the next switch / RESET). |
| **SIZE** (`Size`) | | **varispeed** 0.5x..2x (center = unity). RadioMusic is fixed-rate; this is a platform bonus. |
| **ENV** (`Env`) | | inter-station **STATIC** amount (tuning hiss crossfaded in on a station change). |
| **MIX** (`Mix`) | | deck **volume**. |
| **Alt+PITCH** (`Aux`) | | **BANK** select (held selector, ring dots) - "hold RESET for bank" on the original. |
| **Mix fader** (`Crossfade`) | | A/B blend of the two radios. |
| **Routing switch** (`Route`) | | stereo topology (below). |
| **Play pad** / **gate-in** | `on_play_pad` / `on_gate_trigger` | **RESET** - (re)start a stopped deck at the live position. Debounced, and a **no-op while the deck is already streaming** (re-seeking a live free-running station is redundant, and honoring a repeated/floating-gate trigger would flush the ring continuously - the constant-stutter bug). The Rev pad is inert. |

`capabilities() = CapOwnDisplay | CapDualDeck | CapAux`. `CapAux` claims Alt+PITCH for the BANK selector
(the same held-selector mechanism tape uses for slots and edrums for model select). It does **not**
advertise `CapTapeStorage` - the radio owns its own SD streaming, like tape.

### Routing / stereo image

- **LEFT (`DoubleMono`):** radio A hard-left, radio B hard-right (two radios across the stereo field).
- **CENTRE (`Stereo`):** both radios centred.
- **RIGHT (`GenerativeStereo`):** each radio at a random pan (re-rolled on entering the mode).

### Static (tuning hiss)

A cheap filtered-noise generator (LCG white + one-pole low-pass, no table) crossfades in on each station
change; **ENV** sets the level. With ENV high, sweeping PITCH switches stations immediately so you hear
the dial-tuning churn + hiss; with ENV low, a short settle timer means a sweep only opens the station you
land on (no zipper of file opens). A non-playing deck (empty bank / failed open) outputs dead-air hiss at
the ENV level.

### Display

Per deck the ring shows the **station position** (a bright marker at `station/N`) over a faint base, green
while playing, amber on a failed open / empty bank. Holding **Alt** shows the **BANK** selector (16 dots,
the current bank bright). The routing switch lights the mode L/C/R indicator.

---

## SD-card layout

A shared library both decks browse independently:

```text
/radio/0/   01.raw 02.raw ...      (bank 0)
/radio/1/   ...                    (bank 1)
 ...
/radio/15/  ...                    (bank 15, up to 48 stations each)
```

- **Banks** = numbered folders `0`..`15` (mirrors RadioMusic's 16 folders).
- **Stations** = `.raw` files in a bank, enumerated by directory scan (`f_opendir`/`f_readdir`). Use
  short **8.3 names** (e.g. `01.raw`) - longer names are skipped so the index stays bounded and the file
  stays re-openable.
- **macOS metadata is filtered.** Copying files to a FAT card in Finder writes a hidden AppleDouble
  companion `._NAME.raw` (~4 KB of metadata, not audio) next to every `NAME.raw`, plus `.DS_Store`. The
  companion ends in `.raw`, so `scan_bank` explicitly drops it (FAT hidden/system attribute + leading-dot
  name + a 32 KB minimum size); otherwise each would index as a bogus tiny "station" that loops a sliver
  of garbage - a fast stutter, alternating with the real stations as you tune. The converter's
  `from-dir`/`mirror` skip dotfiles on the source side too. To clean an existing card: `dot_clean
  /Volumes/CARD` then delete any remaining `._*` / `.DS_Store`.

### Audio format

**Headerless raw signed 16-bit mono PCM, little-endian, 48 kHz** (`.raw`) - the original RadioMusic
format, fixed here to the device rate so no on-device resampling is needed. The firmware does no
conversion: it reads the body straight into int16 frames (`int16 * 1/32768` to float in the ISR), so a
wrong-format file plays as garbage. ~192 KB/s for two decks (half the tape engine's float bandwidth).

Convert sources with [`scripts/convert_radio_audio.py`](../../scripts/convert_radio_audio.py). The
original RadioMusic library is itself headerless 16-bit-mono `.raw` at **44.1 kHz** (folders `0`..`15`
at the card root), so the converter states that input format (`--in-rate`, default 44100) and resamples
to 48 kHz. The whole-card `mirror` command reproduces a stock card under `/radio`, preserving names:

```text
# mirror a RadioMusic card's 0..15 folders into /radio/0..15 at 48 kHz, keeping names
scripts/convert_radio_audio.py mirror --keep-names -o /Volumes/SD ~/Downloads/16gb-Disk

# one bank from a folder of your own files
scripts/convert_radio_audio.py from-dir --bank 0 -o /Volumes/SD ./samples

# or a one-liner per file
ffmpeg -i in.wav -ac 1 -ar 48000 -f s16le /Volumes/SD/radio/0/01.raw
```

### Source sample rate (`rate.txt`)

A headerless `.raw` carries no sample rate, so the engine assumes the device rate, **48 kHz**, by
default. To play an **unconverted original card** (44.1 kHz) at correct pitch, drop a one-line text file
**`/radio/rate.txt`** containing the source rate:

```text
44100
```

The engine reads it once at boot and rebases the resampler (consume `rate/48000` source frames per
output frame), so noon on SIZE gives correct pitch and the varispeed still works around it. Any rate in
8000..192000 is accepted (RadioMusic's 11025/22050/44100/48000/96000 all work). With **no** `rate.txt`
the engine runs at 48 kHz - so cards produced by the converter (already 48 kHz) need no file, and only an
unconverted original card needs `echo 44100 > /Volumes/SD/radio/rate.txt`.

So there are two ways to use an original 44.1 kHz card: **convert it** to 48 kHz with `mirror` (no
`rate.txt` needed), or **drop the files in as-is and add `rate.txt`**. Without either, an unconverted
card plays ~9% sharp (also pullable by ear with SIZE). Stations can be arbitrarily long - the originals
run to ~25 min / 130 MB each; streaming has no length cap, so only the (tiny) per-station name+length
index sits in RAM.

---

## Architecture / files

The audio ISR (`process()`) only touches the per-deck rings: pull int16 frames, run a 2-frame
linear-interp varispeed resampler, crossfade the static, mix to the soft-limited stereo bus. All FatFs
work (bank scan, file open + seek, the bank index) runs in `prepare()` (main loop) via `StreamDeck`.

New:

- `src/memory/raw_stream.h` - `RawStreamReader : IChunkSource` (headerless 16-bit body; `seek_to_frame`).
- `src/engine/radio/radio_engine.{h,cpp}` - the engine.
- `host/test_radio.cpp` - host suite (wired into `make -C host test`).
- `scripts/convert_radio_audio.py` - source -> `.raw` converter.

Edited (all `SPK_USE_STREAM`-guarded, byte-identical for non-streaming engines):

- `src/engine/istreamdeck.h` - `start_play_raw` (seek-on-open), `frames_of`, `scan_bank`, `BankEntry`,
  `read_text` (the `rate.txt` read).
- `src/hw/stream_deck.{h,cpp}` - the three new calls + a `RawStreamReader` per deck.
- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` - register `radio`.

---

## Build / test

```text
make -j8 ENGINE=radio        # build (~82.8% SRAM_EXEC)
make engine-radio            # clean + build + DFU flash
make -C host test            # host suites incl. test-radio (all green)
```

---

## Risks / watch-items

- **Dropouts** when both decks sweep stations at once (each switch is an f_open + seek + ring refill).
  Two int16 mono streams are ~half the tape engine's proven float bandwidth, so steady-state is expected
  to hold; sustained simultaneous sweeping is the unmeasured case.
- **Mount delay** - the card mounts ~1 s after boot; the engine retries the bank scan for `kBootScanMs`
  (5 s) so stations appear once mounted rather than showing dead air.
- **First-cut constants** (`kSettleMs`, `kStaticDec`, `kNoiseLevel`, `kStaticThresh`) are tunable by ear
  on hardware, like the tape engine's loop constants.
- **START is applied on switch**, not continuously, to keep SD I/O bounded. Continuous scrub would need
  a lighter in-file seek.
- **Re-open = ring flush.** A (re)open flushes and re-seeds the per-deck ring, so anything that triggers
  re-opens at audio rate produces a stutter (the same short grain replayed). Three guards prevent that:
  - **Station-select hysteresis** (`kStationHyst`) - a deadband so small CV/pot noise near a station
    boundary doesn't flip the target (oscillation needs to swing > 2x the margin).
  - **Settle-every-prepare** - the settle timer (`kSettleMs`) is reset whenever the target moves *at all*,
    so a target that oscillates between two stations (parking PITCH on a boundary) **never settles** and
    the engine just holds the last clean station instead of re-opening ~5x/s there. (With static turned
    *up*, switching is immediate by design - "dial tuning" - and boundary stability then rests on the
    hysteresis alone.)
  - **RESET** (Play pad + gate) is **debounced** (`kDebounceMs`) and a **no-op on a live deck**, so a
    repeated/floating-gate trigger can't flush a live stream.

---

## What's left

### `.wav` station support (16-bit mono PCM)

Accept **16-bit mono PCM `.wav`** stations alongside `.raw` (the format RadioMusic's 2017 firmware
added). Additive and not difficult - a WAV body *is* int16 PCM, byte-identical to a `.raw` once the
header is skipped, so the int16 `_pull` + resampler are unchanged, and the `StreamDeck` `Deck` already
holds both a `WavStreamReader` and the `RawStreamReader`. Steps:

1. `WavStreamReader::seek_to_frame(frame)` - the same one-liner as `RawStreamReader`, seeking to
   `_data_start + frame*2` (it already computes `_data_start`/`_data_size` from the chunk walk).
2. Relax its format gate to accept **16-bit mono PCM** (`fmt == 1`, `bits == 16`) regardless of the
   build's float/int default - e.g. a `begin(f, expect_bits, expect_fmt)` parameter. *This is the only
   part needing real thought; everything else is mechanical.*
3. `StreamDeck::start_play_wav(deck, path, start_frame, loop)` - mirrors `start_play_raw` but binds the
   WAV reader and seeks past the header.
4. `scan_bank` also enumerates `.wav`, with a per-`BankEntry` flag (raw vs wav).
5. `RadioEngine::_do_open` dispatches to the raw or wav entry by that flag.

Two subtleties, both minor:

- **Indexing cost.** A `.raw` length is a pure `f_stat` (filesize/2, no file open); a WAV's length is the
  *data-chunk* size, so indexing a bank of WAVs means an `f_open` + header parse per station (like tape's
  slot probe). Fine for <=48 files at boot, just not as cheap as raw.
- The format gate (step 2) is the only code wired to the build's float/int default; the rest is
  extension-dispatch bookkeeping.

**Bonus:** a WAV header carries its own sample rate, so WAV stations could auto-rebase *per file* (a
44.1k WAV would just play correctly), making `rate.txt` unnecessary for them - at the cost of a per-station
rate ratio instead of the global one. That is the natural place to take it.

Estimate: ~half a day incl. host tests, no architectural change. Mixed raw + wav banks supported.
