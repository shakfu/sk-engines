# Dev resume notes — SD-streaming `tape` engine (eliminate the playback/record length cap)

> Resume point for the streaming **tape** engine - the length cap is removed and the engine is
> hardware-verified (record / play / varispeed all confirmed on device). (`reso` shipped separately - see
> `docs/engines/reso.md` and `CHANGELOG.md` `[0.2.2]`.)

## TL;DR — where we left off

Building a `tape` engine that streams audio to/from the SD card so playback and recording are no longer
capped by the ~42 s (float) / 84 s (int16) in-SDRAM loop buffer (`kSourceMaxSeconds` in `src/config.h`).
It is a **linear** player/recorder (not granular scrubbing — random SD seek can't meet the 2 ms audio
deadline). All four plan steps are done and the engine is hardware-verified; the host-testable core is
fully green and both build systems build and fit.

**Status: hardware-verified.** A >42 s take records to `/TAPE.WAV`, plays back, and the PITCH varispeed
sweep works on the device - the original length cap is gone. Bring-up turned up one real blocker (the SD
card was unmounted out from under the stream) plus an LED-feedback rework; both are fixed and described
under "Resolved during hardware bring-up". What remains is hardening + features, not the gate.

## Plan + status

1. **Ring + stream core (host-tested)** — [done]. Lock-free SPSC ring + `PlayStream`/`RecordStream`.
2. **Streaming WAV codec (host-tested)** — [done]. Placeholder header -> patch-on-finalize, over an
   `IByteFile` seam; full end-to-end host test (ISR->ring->WAV file->ring->ISR), byte-exact.
3. **Device bring-up** — [done, hardware-verified]. `FatFile`, `StreamDeck` service, `tape` engine,
   SDRAM rings, `EngineContext` injection, `AppImpl` pump, registration. **+ varispeed playback.**
4. **HARDWARE VERIFY** — [done]. Recorded a >42 s take, played it back, swept PITCH on the device;
   cleared after fixing the SD-unmount blocker (see "Resolved during hardware bring-up").
5. **Polish backlog** — [ ] see "What's left".

## Architecture

Producer/consumer split mirroring the existing `Storage` service:
- **Audio ISR** (`tape_engine.process()`) only ever touches the **lock-free SDRAM rings** (bounded,
  never blocks): `play_consume` / `record_produce`.
- **Main loop** (`AppImpl::Loop` → `StreamDeck::process()`) does the slow FatFs chunked I/O.
- The engine reaches the service through `IStreamDeck*` injected via `EngineContext` (like `ITransport`),
  so FatFs stays platform-side.

Data flow:
- **Play:** SD file → `WavStreamReader` (parses header, seeks to `data`, stops at `DataSize`) →
  `PlayStream.pump()` fills the play ring → ISR `play_consume` drains it (zero-fill on underrun).
- **Record:** ISR `record_produce` fills the record ring → `RecordStream.pump()` writes chunks via
  `WavStreamWriter` → on stop, flush tail + `finalize()` patches the WAV size fields (`f_lseek(0)`).
- **Varispeed:** the engine resamples the play ring with a linear-interpolating fractional read
  (`_speed` frames/output-frame); read-ahead/SD-pump scale with consumption automatically.

SDRAM: two **1 MB** rings (~2.7 s float-stereo read-ahead each, power-of-two as `SpscRing` requires) +
a 32 KB chunk scratch, static `DSY_SDRAM_BSS` in `buffer.sdram.cpp`, tape-only. Streaming format is the
build's native WAV body (float, interleaved L/R) → no sample conversion on the audio path.

## Files

New (core, host-tested, engine-agnostic):
- `src/memory/spsc_ring.h` — lock-free SPSC byte ring over an external buffer (kfifo-style, wrap-safe).
- `src/memory/audio_stream.h` — `IChunkSource`/`IChunkSink`, `PlayStream`, `RecordStream`.
- `src/memory/byte_file.h` — `IByteFile` seam (read/write/seek).
- `src/memory/wav_stream.h` — `WavStreamReader`/`WavStreamWriter` (streaming WAV codec over `IByteFile`).
- `host/test_stream.cpp` (+ `host/Makefile` `test-stream`, wired into `make -C host test`).

New (device):
- `src/engine/istreamdeck.h` — the `IStreamDeck` contract.
- `src/hw/fat_file.{h,cpp}` — FatFs `IByteFile` (body guarded `#if defined(SPK_ENGINE_TAPE)`).
- `src/hw/stream_deck.{h,cpp}` — the `StreamDeck` platform service (guarded body).
- `src/engine/tape/tape_engine.h` — the engine (header-only). Play/record + varispeed + LED feedback.

Edited:
- `src/engine/engine_context.h` — `IStreamDeck* stream = nullptr;`.
- `src/hw/buffer.sdram.{h,cpp}` — `streamMem()` + the SDRAM rings (guarded).
- `src/memory/storage.h` — `#if SPK_ENGINE_TAPE`: keep the SD card mounted (skip `_can_unmount()`'s
  unmount) so the tape stream's FatFile can open files all session. See "Resolved during bring-up".
- `src/app.cpp` — construct/init/inject `StreamDeck`, pump it in `Loop` (all `#if SPK_ENGINE_TAPE`).
- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` — register `tape`.

## Control / UX (first cut — deliberately minimal)

- **SeqA pad** = play toggle, **SeqB pad** = record toggle. One fixed file: **`/TAPE.WAV`** (SD root).
  (Chosen because `on_seq_trigger` is known-good routing for non-storage engines; called in the main
  loop so opening/closing FatFs there is safe.) Play and record are mutually exclusive. A 300 ms
  same-deck debounce guards capacitive-pad glitches.
- **PITCH knob** (either deck, `ParamId::Speed`) = **varispeed** playback, `exp2((v-0.5)*2)` → 0.5×…2×
  (±1 octave), default 0.5 = unity. Tape-style (pitch+speed linked).
- Display: idle = both rings **off**; ring A **bright green** = playing, ring B **bright red** =
  recording; a rejected start flashes that ring **amber** (`0xff6000`) ~1.2 s. **Input monitored** to
  output while recording. `capabilities() = CapOwnDisplay | CapDualDeck`. NOT `CapTapeStorage` (so
  platform `Storage` stays out of the way; the tape engine owns its own SD streaming).
- **LED caveat:** the Seq pads have no LED in the hardware map, so status renders on the **Play-pad
  LEDs** + the rings while control is on the **Seq pads** - the pad you press is not the pad that lights.
  Moving control onto the (LED-equipped) Play pads is a clean future fix.

## Build / flash / test

```
make engine-tape                          # clean + build + DFU flash (Make)
make -f Makefile.cmake ENGINE=tape program-dfu   # CMake path
make -j8 ENGINE=tape                      # build only (Make ~80.5% SRAM_EXEC; CMake ~81%)
make -C host test                         # host suites incl. test-stream (all green)
```
On hardware (after ~1 s for SD mount): **SeqB** to record a >42 s take into the input → **SeqB** to stop
(finalizes `/TAPE.WAV`) → **SeqA** to play it back → turn **PITCH** while playing to hear varispeed.

## Resolved during hardware bring-up

The host-green core ran on device after two real fixes found by flashing:

**1. SD card unmounted out from under the stream (the blocker).** Symptom: pressing record flashed the
ring briefly then went dark, and **no `/TAPE.WAV` was ever created** - so `f_open` was failing, not the
audio path. Cause: `Storage::process()` unmounts the card via `_can_unmount()` once settings are read
and both decks are idle (`f_mount(NULL) + _fsi.DeInit()`); the tape `FatFile` then opened files on a dead
volume -> `f_open` returned `FR_NOT_READY` -> `start_record` returned `false` -> no file. Non-streaming
builds never hit this because they mount on demand around each save/load. Fix: under `#if
SPK_ENGINE_TAPE`, skip that unmount so the volume stays mounted for the whole session
(`src/memory/storage.h`). The original note's "Storage only mounts + reads settings" was wrong - it
actively unmounts.

**2. LED feedback unreadable / failures silent.** Reworked the engine `render()`:
- Idle is now fully **off** and active is full-bright (was dim-vs-bright of the same hue, which is
  indistinguishable on the hardware LEDs) - an engage now reads as an unambiguous off->on.
- A rejected `start_*` (the symptom above) flashed nothing; it now flashes the ring **amber**
  (`0xff6000`) ~1.2 s, timed via the injected `ITimeSource`.
- Control is on the **Seq pads** but the status LED lives on the **Play pads** (the Seq pads have no LED
  in the hardware map) - logged as a UX wart, not yet moved. A 300 ms same-deck debounce was added (the
  double-trigger it guards turned out not to be the cause - the unmount was - but it is cheap glitch
  insurance and prevents a real capacitive misfire).

## Risks / watch-items (steady-state verified; longer/faster takes still unproven)

- **Dropouts/underruns.** Held for the verification take. Throughput is comfortable in theory (384 KB/s
  float-stereo, 768 KB/s at 2×, vs ~1.5 MB/s on the `MEDIUM_SLOW` / 1-bit SDMMC; ~2.7 s ring). If they
  occur on longer/faster takes: bump SD speed/bus width (`card.cpp` `recognize()`) and/or deepen rings.
- **SD mount lifecycle** — RESOLVED (see above): `Storage` used to unmount the card after reading
  settings, killing the tape stream; the tape build now keeps it mounted. `/TAPE.WAV` is at the volume
  root. FatFile and `Storage`'s `Card` share one FatFs volume but never do I/O concurrently (Storage
  stays idle in the tape build; the tape engine owns all streaming I/O).
- **Format** = float WAV (2× the SD bandwidth of int16). int16 streaming is a future optimization.

## What's left for the implementation

### Gate (CLEARED)
- **Hardware-verify** — [done] recorded a >42 s take, played it back, and swept PITCH on device (after
  the SD-unmount fix). SD throughput held for this take; if longer or 2x-speed takes underrun, **int16
  streaming** jumps up the list.

### Hardening (known gaps) — do before it's "solid"
- **SD-full / partial `f_write`** — `RecordStream::pump()` pulls bytes out of the ring before
  `sink->write()`; a short write (disk full) loses them. Don't advance the ring until written, or stop
  cleanly on SD-full.
- **Error feedback** — [done] `start_play`/`start_record` failures now flash the ring **amber** ~1.2 s
  (were silently ignored). Still coarse: one colour for all causes (missing file / not mounted / disk
  full); per-reason indication would need `StreamDeck` to report a reason code.
- **Mount-readiness** — [partly] the card now stays mounted for the session, so steady-state play/record
  works. A press in the first ~1 s after boot (before the mount completes) still fails - but now flashes
  amber instead of dying silently. Could gate on mount state or auto-retry.
- **Format validation** — `WavStreamReader` parses any WAV header, but the engine then reinterprets the
  body as native float-stereo-48k; a differently-formatted file → garbage. Guard (reject/convert) before
  arbitrary-file playback. (Self-recorded `/TAPE.WAV` is consistent, so fine for the gate.)
- **Concurrency review** — the ISR↔main-loop atomic handshakes (the `_finalizing` flush window,
  play-finished detection) are reasoned-through; play-finished + finalize observed working on device,
  but the edge cases (stop during finalize, underrun at EOF) are still un-stress-tested.

### Feature backlog (the length cap is already gone without these)
- **File selection** (not the single fixed `/TAPE.WAV`) — reuse the storage slot/tape UI.
- **Seek / scrub** within the long file (`f_lseek` + ring flush) — closest to positioning on a stream.
- **Transport sync** + loop points; **reverse** playback.
- **int16 streaming** (halves SD bandwidth — the main throughput lever).
- **Two streams / per-deck** (A and B independent, 2 file handles).
- **Progress / level** on the rings; **move play/record control onto the Play pads** (which have LEDs)
  instead of the LED-less Seq pads; wider/quantized varispeed range.

### Docs / housekeeping (to "land" it)
- `CHANGELOG.md` entry (tape engine + streaming infra).
- `README.md` — add `tape` to the engine list, build options, flash targets.
- `docs/engines/tape.md` — [done] written and updated to the hardware-verified state (banner, status,
  risks, hardening all reflect the cleared gate).
- Commit the changeset (user does).

## Build-system notes

- All streaming code is `#if defined(SPK_ENGINE_TAPE)` so non-tape engines are byte-identical (granular
  only +8 B from the shared `EngineContext` field). The `storage.h` unmount guard is likewise tape-only.
- The guarded TUs (`stream_deck.o`, `fat_file.o`, `buffer.sdram.o`) are flag-dependent, so the `Makefile`
  makes them depend on `build/.engine-stamp` (like `app.o`) — otherwise `make ENGINE=tape` over a stale
  non-tape build relinks empty objects (undefined `StreamDeck`/`FatFile`/`streamMem`). The `engine-*`
  one-shot targets `make clean` anyway; the granular-engine switch-without-clean contamination is a
  separate, pre-existing limitation.

## Broader session context (already committed / done)

- `reso` engine shipped (Mutable Instruments Rings voice; renamed from `karp`); pitch bug root-caused
  (`cv_voct` clobbering the knob) + fixed; defaults (ENV/SIZE/cycle) engine-seeded; Alt+PITCH model
  selector shown while Alt held; vendored Rings/stmlib trimmed + colocated under
  `src/engine/reso/thirdparty/`. See `docs/engines/reso.md` and `CHANGELOG.md` `[0.2.2]`.
- Opt-in CMake build added (additive; `make -f Makefile.cmake`); the `make` build stays canonical.
