# tape engine

`ENGINE=tape` · `src/engine/tape/tape_engine.h` (header-only) · class `TapeEngine`

A streaming **tape deck**: plays arbitrarily long files from the SD card and records arbitrarily long takes back to it, removing the in-SDRAM loop-length cap that bounds the other engines (~42 s float / ~84 s int16, `kSourceMaxSeconds` in `src/config.h`). It is a **linear** player/recorder, not a granular scrubber - random SD seek cannot meet the 2 ms audio deadline, so the tape only ever streams forward.

The engine itself is deliberately thin. In `process()` (the audio ISR) it only moves interleaved float frames between the platform's lock-free SDRAM rings and the audio buffers; all the slow FatFs I/O happens off the audio path in the platform's `StreamDeck` pump (main loop).

> **Status: hardware-verified.** A >42 s take records to `/TAPE.WAV`, plays back, and the PITCH varispeed sweep works on the device - the length cap is gone. Bring-up cleared one real blocker (the SD card was being unmounted out from under the stream) plus an LED-feedback rework; both are fixed. What remains is hardening + features, not the gate. The deeper build/bring-up narrative (and the resolved-bug writeup) lives in `docs/dev/tape-impl.md`.

---

## Status at a glance

- **Hardware-verified:** a >42 s take records, plays back, and the PITCH varispeed sweep works on the device. The original in-SDRAM length cap is gone.

- Core (rings + streaming WAV codec) **host-tested**: `make -C host test` includes `test-stream` (ISR -> ring -> WAV file -> ring -> ISR, byte-exact) - all green.

- Device path integrated end to end (`FatFile`, `StreamDeck`, the engine, SDRAM rings, `EngineContext` injection, the `AppImpl` pump, registration).

- Firmware links and fits: `make -j8 ENGINE=tape` -> ~80.5% SRAM_EXEC (Make) / ~81% (CMake), no overflow.

- SD throughput held for the verification take; longer / 2x-speed takes are not yet stress-tested (if they underrun, int16 streaming jumps up the priority list).

---

## Concept

Two halves, each a producer/consumer split that mirrors the existing `Storage` service:

- **Play:** the SD file is parsed by `WavStreamReader` (reads the header, seeks to `data`, stops at `DataSize`); `PlayStream.pump()` fills the play ring from the main loop; the ISR drains it via `play_consume`, zero-filling (silence) on underrun.

- **Record:** the ISR pushes the live input into the record ring via `record_produce`; `RecordStream.pump()` writes chunks through `WavStreamWriter` from the main loop; on stop it flushes the tail and `finalize()` patches the WAV size fields (`f_lseek(0)`).

The engine reaches the platform service only through an injected `IStreamDeck*` (delivered via `EngineContext`, exactly like `ITransport`), so FatFs stays entirely platform-side. The streaming format is the build's **native WAV body** (float, interleaved L/R), so neither play nor record needs any sample conversion - the audio path is already float.

### Varispeed

Playback is resampled in the ISR by a 2-frame linear interpolator: the engine advances `_speed` source frames per output frame and reads fractionally between the two. Pitch and speed move together (tape-style) - there is no time-stretch. `_speed == 1` is unity; read-ahead and the SD pump scale with consumption automatically, so faster playback simply drains the ring faster.

---

## Controls (first cut - deliberately minimal)

This is the smallest UX that exercises the streaming core; richer control (file selection, per-deck streams, transport sync) is backlog, not yet built.

| Control | Action |
|---|---|
| **SeqA pad** | play toggle |
| **SeqB pad** | record toggle |
| **PITCH knob** (either deck, `ParamId::Speed`) | varispeed playback |

- Play and record are **mutually exclusive**, both bound to one fixed file: **`/TAPE.WAV`** at the SD root. (The Seq pads were chosen because `on_seq_trigger` is known-good main-loop routing for non-storage engines, so opening/closing FatFs there is safe.)

- **PITCH** maps `v in [0,1]` to `exp2((v-0.5)*2)` -> 0.5x .. 2x (+/-1 octave); default 0.5 = unity. Either deck's PITCH drives the single stream.

- **Display:** idle = both rings **off**; ring A **bright green** = playing, ring B **bright red** = recording; a rejected start (no file / card not mounted) flashes that ring **amber** for ~1.2 s. The live input is **monitored to the output while recording** so you hear what you are capturing.

- **LED caveat:** the Seq pads have no LED, so status renders on the **Play-pad LEDs** plus the rings while control is on the **Seq pads** - the pad you press is not the pad that lights. A 300 ms same-deck debounce guards capacitive-pad glitches. Moving control onto the (LED-equipped) Play pads is a clean future fix.

`capabilities() = CapOwnDisplay | CapDualDeck`. Note it does **not** advertise `CapTapeStorage` - the tape engine owns its own SD streaming, so the platform `Storage` service stays out of the way (it mounts the card at boot and, in the tape build, leaves it mounted for the stream; see Risks).

---

## Architecture

Two contexts, never sharing mutable state except through the lock-free rings:

- **Audio ISR** (`TapeEngine::process()`) only ever touches the bounded SDRAM rings (`play_consume` / `record_produce`); it never blocks and never calls FatFs.

- **Main loop** (`AppImpl::Loop` -> `StreamDeck::process()`) does the slow FatFs chunked I/O - filling the play ring ahead of the ISR and draining the record ring to disk.

The `IStreamDeck` contract encodes this threading split directly: `play_consume`/`record_produce` are ISR-only and never block; `start_play`/`start_record`/`stop` are main-loop-only control ops that touch FatFs; `is_playing`/`is_recording` are cheap reads safe from either side.

**SDRAM layout** (static `DSY_SDRAM_BSS` in `buffer.sdram.cpp`, tape-only): two **1 MB** rings (~2.7 s of float-stereo read-ahead each; power-of-two as `SpscRing` requires) plus a 32 KB chunk scratch.

---

## Files

New core (host-tested, engine-agnostic):

- `src/memory/spsc_ring.h` - lock-free SPSC byte ring over an external buffer (kfifo-style, wrap-safe).

- `src/memory/audio_stream.h` - `IChunkSource` / `IChunkSink`, `PlayStream`, `RecordStream`.

- `src/memory/byte_file.h` - `IByteFile` seam (read / write / seek).

- `src/memory/wav_stream.h` - `WavStreamReader` / `WavStreamWriter` (streaming WAV codec over `IByteFile`).

- `host/test_stream.cpp` - the end-to-end host suite (wired into `make -C host test`).

New device:

- `src/engine/istreamdeck.h` - the `IStreamDeck` contract.

- `src/hw/fat_file.{h,cpp}` - FatFs-backed `IByteFile` (body guarded `#if defined(SPK_ENGINE_TAPE)`).

- `src/hw/stream_deck.{h,cpp}` - the `StreamDeck` platform service (guarded body).

- `src/engine/tape/tape_engine.h` - the engine (header-only): play / record + varispeed + LED feedback.

Edited:

- `src/engine/engine_context.h` - adds `IStreamDeck* stream = nullptr;`.

- `src/hw/buffer.sdram.{h,cpp}` - `streamMem()` + the SDRAM rings (guarded).

- `src/memory/storage.h` - under `#if SPK_ENGINE_TAPE`, keep the SD card mounted (skip the `_can_unmount()` unmount) so the tape stream can open files all session.

- `src/app.cpp` - construct / init / inject `StreamDeck`, pump it in `Loop` (all `#if SPK_ENGINE_TAPE`).

- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` - register `tape`.

---

## Build / flash / test

```text
make engine-tape                                  # clean + build + DFU flash (Make path)
make -f Makefile.cmake ENGINE=tape program-dfu    # CMake path
make -j8 ENGINE=tape                              # build only (~80% SRAM_EXEC)
make -C host test                                 # host suites incl. test-stream (all green)
```

On hardware, after ~1 s for the SD to mount: **SeqB** to record a >42 s take into the input -> **SeqB** to stop (finalizes `/TAPE.WAV`) -> **SeqA** to play it back -> turn **PITCH** while playing to hear varispeed.

### Build-system note

All streaming code is `#if defined(SPK_ENGINE_TAPE)`, so non-tape engines are byte-identical (granular gains only +8 B from the shared `EngineContext` field). Because the guarded TUs (`stream_deck.o`, `fat_file.o`, `buffer.sdram.o`) are flag-dependent, the `Makefile` makes them depend on `build/.engine-stamp` (like `app.o`); otherwise a `make ENGINE=tape` over a stale non-tape build would relink empty objects (undefined `StreamDeck` / `FatFile` / `streamMem`). The `engine-tape` one-shot target runs `make clean` regardless.

---

## Risks / watch-items

- **Dropouts / underruns** (steady-state held; longer / faster takes unproven). Throughput is comfortable in theory (384 KB/s float-stereo, 768 KB/s at 2x, vs ~1.5 MB/s on the `MEDIUM_SLOW` 1-bit SDMMC; ~2.7 s ring). If they occur: raise SD speed / bus width (`card.cpp` `recognize()`) and/or deepen the rings.

- **SD mount lifecycle** (resolved). `/TAPE.WAV` is at the volume root. `Storage` used to unmount the card after reading settings (via `_can_unmount()`), which killed the tape stream - the tape build now keeps it mounted. `FatFile` and `Storage`'s `Card` share one FatFs volume but never do I/O concurrently (Storage stays idle in the tape build; the tape engine owns all streaming I/O). See `docs/dev/tape-impl.md`.

- **Format = float WAV.** Twice the SD bandwidth of int16; int16 streaming is the main future throughput lever.

---

## What's left

### Gate (cleared)

- **Hardware-verify** - done: recorded a >42 s take, played it back, and swept PITCH on device (after the SD-unmount fix). SD throughput held for that take.

### Hardening (known gaps)

- **SD-full / partial `f_write`** - `RecordStream::pump()` pulls bytes out of the ring before `sink->write()`; a short write (disk full) loses them. Don't advance the ring until written, or stop cleanly on SD-full.

- **Error feedback** - done (coarse): `start_play` / `start_record` failures now flash the ring amber ~1.2 s. Still one colour for all causes (missing file / not mounted / disk full); per-reason feedback would need `StreamDeck` to report a reason code.

- **Mount-readiness** - partly: the card now stays mounted for the session, so steady-state works. A press in the first ~1 s after boot (before the mount completes) still fails, but now flashes amber rather than dying silently; could gate on mount state or auto-retry.

- **Format validation** - `WavStreamReader` parses any WAV header, but the engine then reinterprets the body as native float-stereo-48k; a differently-formatted file plays as garbage. Guard (reject / convert) before allowing arbitrary-file playback. (A self-recorded `/TAPE.WAV` is internally consistent, so this is fine for the gate.)

- **Concurrency review** - play-finished + finalize were observed working on device; the remaining edge cases of the ISR <-> main-loop atomic handshakes (stop during the `_finalizing` flush window, underrun at EOF) are reasoned-through but not yet stress-tested.

### Feature backlog (the length cap is already gone without these)

- **File selection** (beyond the single fixed `/TAPE.WAV`) - reuse the storage slot / tape UI.

- **Seek / scrub** within the long file (`f_lseek` + ring flush).

- **Transport sync** + loop points; **reverse** playback.

- **int16 streaming** - halves SD bandwidth, the main throughput lever.

- **Two streams / per-deck** (A and B independent, two file handles).

- **Progress / level** meters on the rings; proper play/record pad gestures (vs the Seq pads); a wider / quantized varispeed range.
