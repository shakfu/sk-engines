# Dev resume notes — SD-streaming `tape` engine (dual-deck record decks)

> Resume point for the streaming **tape** engine - the in-SDRAM length cap is removed and the engine is
> hardware-verified as a **dual-deck** machine: two independent mono decks, routing switch + mix fader +
> per-deck pan/volume, and four ENV loop modes. (`reso` shipped separately - see `docs/engines/reso.md`
> and `CHANGELOG.md` `[0.2.2]`.)

## TL;DR — where we are

A `tape` engine that streams audio to/from the SD card so playback and recording are no longer capped by
the ~42 s (float) / 84 s (int16) in-SDRAM loop buffer (`kSourceMaxSeconds`). It is **linear** (no random
seek - can't meet the 2 ms deadline). The original single-stream gate cleared on hardware; it then grew
into a **dual-deck** instrument:

- **Two independent mono decks** (A/B), each play-XOR-record, its own ring + 8 slot files under
  `/tapes/`. Deck A records input A and plays to L, deck B records input B and plays to R.
- **Routing switch** (`ConfigId::Route`): LEFT = per-deck Alt+POS pan, CENTRE = both centered, RIGHT =
  random pan. **Mix fader** (`Crossfade`) = A/B blend. **MIX** knob = per-deck volume. **PITCH** = per-deck
  varispeed. **Alt+POS** (`AltPos`) = pan; **Alt+PITCH** (`Aux`) = tape-slot select; **bare POS** reserved.
- **ENV knob** = per-deck loop mode (4 quadrants): none / plain / faded (seam fade) / Frippertronics
  (per-pass decay, auto-stops).
- **Tape slots:** 8 per deck under `/tapes/`, selected by Alt+PITCH (non-destructive multi-take).

Dual-deck + loops are hardware-verified ("working so far"). The latest control-map additions (Alt+POS
pan, Alt+PITCH slots, `/tapes/` files) build clean + host-green, pending re-flash. SRAM_EXEC ~81.5%.

## Plan + status

1. **Ring + stream core (host-tested)** — [done]. Lock-free SPSC ring + `PlayStream`/`RecordStream`
   (+ looping: `PlayStream::set_loop` rewinds the source at EOF).
2. **Streaming WAV codec (host-tested)** — [done]. Placeholder header -> patch-on-finalize; mono-capable
   (`wav_header(size, channels)`); `WavStreamReader::rewind` for loops.
3. **Device bring-up (single stream)** — [done, hardware-verified]. Cleared after the SD-unmount fix
   (see "Resolved during hardware bring-up").
4. **Dual-deck + routing + loops** — [done, hardware-verified]. Per-deck `StreamDeck`, per-deck
   `IStreamDeck`, mono I/O (input A->deck A, input B->deck B), routing switch / pan / mix-fader / MIX
   volume, ENV loop modes.
5. **Polish backlog** — [ ] see "What's left".

## Architecture

Producer/consumer split, **per deck**, mirroring the existing `Storage` service:
- **Audio ISR** (`tape_engine.process()`) only touches the lock-free per-deck SDRAM rings
  (`play_consume(deck)` / `record_produce(deck)`); never blocks, never calls FatFs.
- **Main loop** (`AppImpl::Loop` → `StreamDeck::process()`) does the slow FatFs I/O for both decks
  sequentially.
- The engine reaches the service through a per-deck `IStreamDeck*` injected via `EngineContext`.

`StreamDeck` holds **two `Deck` units** (`std::atomic<Mode> mode` + ring + `PlayStream`/`RecordStream` +
`WavStreamReader`/`WavStreamWriter` + `FatFile`). `process()` pumps both. The `Mem` struct carries one
ring per deck (`ring_a`/`ring_b`) + a shared scratch.

Data flow (per deck):
- **Play:** SD file → `WavStreamReader` → `PlayStream.pump()` fills the ring → ISR `play_consume` drains
  it (zero-fill on underrun). Looping: at EOF `PlayStream` calls `source->rewind()` and keeps filling.
- **Record:** ISR `record_produce` fills the ring → `RecordStream.pump()` → `WavStreamWriter` (mono) →
  on stop, flush tail + `finalize()` patches the size fields.
- **Audio (engine):** each deck renders a mono stream (varispeed playback / record-monitor / silence),
  then a per-block gain matrix (MIX volume × mix-fader blend × pan, selected by the routing switch) mixes
  both decks to the stereo bus `out[0]`=L / `out[1]`=R. Pan/blend gains are precomputed on knob change.
- **Loop shaping (engine):** for Faded/Frippertronics, the engine reads `loop_frames(deck)`, tracks
  source-frame position, and applies a seam fade or per-pass decay. A faded-out Frippertronics deck is
  flagged from the ISR and `stop()`-ped in `prepare()` (main loop), never in the audio path.

SDRAM: **one 1 MB ring per deck** (~5.5 s mono read-ahead; power-of-two as `SpscRing` requires) + a 32 KB
scratch shared by both decks (sequential pumps). No increase over the original two-ring layout. Format is
mono float WAV (one channel per deck) → no sample conversion on the audio path.

## Files

New (core, host-tested, engine-agnostic):
- `src/memory/spsc_ring.h` — lock-free SPSC byte ring.
- `src/memory/audio_stream.h` — `IChunkSource` (+ `rewind`) / `IChunkSink`, `PlayStream` (+ looping),
  `RecordStream`.
- `src/memory/byte_file.h` — `IByteFile` seam.
- `src/memory/wav_stream.h` — `WavStreamReader` (+ `rewind`/`data_bytes`) / `WavStreamWriter` (mono).
- `host/test_stream.cpp` — end-to-end suite incl. the loop-rewind test (`make -C host test`).

New (device):
- `src/engine/istreamdeck.h` — per-deck `IStreamDeck` contract (+ `set_loop`/`loop_frames`).
- `src/hw/fat_file.{h,cpp}` — FatFs `IByteFile` (guarded).
- `src/hw/stream_deck.{h,cpp}` — dual-deck `StreamDeck` service (guarded).
- `src/engine/tape/tape_engine.{h,cpp}` — the engine (slim `IEngine` header + impl): dual decks, varispeed, routing/pan/mix/volume, loop modes.

Edited:
- `src/engine/engine_context.h` — `IStreamDeck* stream = nullptr;`.
- `src/hw/buffer.sdram.{h,cpp}` — `streamMem()` + per-deck rings (`ring_a`/`ring_b`, guarded).
- `src/memory/wav.h` — `wav_header(size, channels=2)` for mono per-deck files.
- `src/memory/storage.h` — `#if SPK_ENGINE_TAPE`: keep the SD card mounted (skip `_can_unmount()`).
- `src/hw/fat_file.cpp` — `open_write` creates the parent dir (`/tapes`) before the file.
- `src/engine/engine_params.h` — adds `ParamId::AltPos` + `CapAltPos` (the Alt+POS knob layer).
- `src/ui/core.ui.{h,cpp}` — routes Alt+POS → `AltPos` for `CapAltPos` engines (gated like `CapAux`/`Aux`).
- `src/engine/granular/granular_engine.cpp` — `AltPos` no-op case (`-Wswitch`-clean).
- `src/app.cpp` — construct/init/inject `StreamDeck`, pump it in `Loop` (guarded).
- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` — register `tape`.

## Control / UX

- **Play pad** (per deck) = play toggle; **Alt+Play** = record toggle (`on_play_pad`/`on_record_pad`,
  main-loop-safe). The **Rev pad is inert** (reserved for reverse playback). Play XOR record per deck,
  300 ms same-deck debounce.
- **PITCH** (`Speed`) = per-deck varispeed (`exp2((v-0.5)*2)`, 0.5×…2×, ±1 octave). **Alt+PITCH** (`Aux`,
  `CapAux`) = tape-slot select (visual selector while held). **Alt+POS** (`AltPos`, `CapAltPos`) = per-deck
  equal-power pan (LEFT routing); **bare POS is reserved** for a future loop-start (ignored now). **MIX**
  (`Mix`) = per-deck volume. **ENV** (`Env`) = loop mode (4 quadrants). **Mix fader** (`Crossfade`) = A/B
  blend. **Routing switch** (`Route`) = pan topology.
- I/O: two MONO inputs (A normalled to B); deck A records input A, deck B records input B; stereo bus →
  headphone + the individual jacks.
- Display: per deck, idle ring **off**, playing **green**, recording **red**, rejected start **amber**
  (`0xff6000`) ~1.2 s; status also on the **Play-pad LED** (the pad you press). Routing position on the
  **mode L/C/R** indicator. While Alt held, the ring shows the **8-slot selector** (dots, selected bright).
- `capabilities() = CapOwnDisplay | CapDualDeck | CapAux | CapAltPos`. NOT `CapTapeStorage`.

**Tape slots:** 8 per deck, `/tapes/tape_a_<n>.wav` / `tape_b_<n>.wav`. Alt+PITCH (`Aux`) picks the active
slot; the next Play/record targets it (no interrupt of a playing deck). `FatFile::open_write` creates
`/tapes` on first record. Single-digit slots keep the names 8.3-safe.

**Alt-layer remaps are capability-gated, so they never degrade other engines.** Alt+POS→`AltPos` fires
only for `CapAltPos` engines; everyone else hits the exact original `Pos` path (`process(val,!fx,…)` +
`set_param(Pos,…)`) byte-for-byte - including pickup and the value display. Same model as `CapAux` for
Alt+PITCH. (An earlier cut gated `Pos` globally and silently froze granular's Alt+POS; the capability
gate fixes that - non-tape firmware is unaffected.)

Loop modes (ENV): None / Plain / Faded / Fripp. Looping is in the stream (`set_loop` + `rewind`); the
fade/decay is engine-side via `loop_frames` + source-frame position tracking. Tunable constants:
`kFadeFrames` (~50 ms seam fade), `kFrippDecay` (0.6/pass), `kFrippFloor` (0.02 → auto-stop).

## Build / flash / test

```
make engine-tape                          # clean + build + DFU flash (Make)
make -j8 ENGINE=tape                       # build only (~81.5% SRAM_EXEC)
make -C host test                          # host suites incl. test-stream (all green)
```
On hardware: **Alt+Play(A/B)** records each deck's input, **Play(A/B)** plays them back together
(A→L, B→R). POS/MIX/ENV/PITCH per deck; mix fader blends; routing switch picks the pan topology.

## Resolved during hardware bring-up

**1. SD card unmounted out from under the stream (the single-stream blocker).** Record flashed the ring
briefly then went dark and **no file was created** - `f_open` was failing, not the audio path.
`Storage::process()` unmounts via `_can_unmount()` once settings are read and both decks are idle
(`f_mount(NULL) + _fsi.DeInit()`); the tape `FatFile` then opened on a dead volume → `FR_NOT_READY` →
`start_record` false → no file. Fix: under `#if SPK_ENGINE_TAPE`, skip that unmount so the volume stays
mounted all session (`src/memory/storage.h`).

**2. LED feedback unreadable / failures silent.** Idle is fully **off**, active full-bright (was
dim-vs-bright same-hue, indistinguishable on the LEDs); a rejected `start_*` flashes **amber** ~1.2 s
(via the injected `ITimeSource`). The dual-deck rework also **moved control onto the Play pads** (which
have LEDs), retiring the earlier Seq-pad wart - the pad you press is now the pad that lights.

**3. Input was being summed to mono (dual-deck fix).** The first dual-deck cut recorded
`0.5*(in[0]+in[1])`. Per the hardware manual the inputs are two INDEPENDENT mono jacks - deck A records
input A, deck B records input B. Corrected.

## Risks / watch-items (steady-state verified; sustained both-decks-at-2x unproven)

- **Dropouts/underruns.** Mono-per-deck keeps two simultaneous streams near one old stereo stream's
  bandwidth (~192 KB/s each, vs ~1.5 MB/s on `MEDIUM_SLOW` / 1-bit SDMMC; ~5.5 s mono ring). If they
  occur: bump SD speed/bus width (`card.cpp` `recognize()`) and/or deepen rings.
- **SD mount lifecycle** — RESOLVED (above): the tape build keeps the card mounted. FatFile and Storage's
  Card share one FatFs volume but never do I/O concurrently (Storage stays idle in the tape build).
- **Format** = float WAV (2× int16 bandwidth). int16 streaming is the main future throughput lever.

## What's left for the implementation

### Hardening (known gaps)
- **SD-full / partial `f_write`** — `RecordStream::pump()` pulls bytes before `sink->write()`; a short
  write (disk full) loses them. Don't advance the ring until written, or stop cleanly on SD-full.
- **Error feedback** — [done, coarse] amber flash on `start_*` failure; one colour for all causes.
- **Mount-readiness** — [partly] card stays mounted, so steady-state works; the first ~1 s after boot
  still fails (now amber, not silent).
- **Format validation** — `WavStreamReader` parses any header but the engine assumes mono float 48k; a
  differently-formatted file plays as garbage. Guard before arbitrary-file playback.
- **Concurrency review** — play-finished + finalize + loop-rewind observed on device; edge cases (stop
  during the `_finalizing` window, the Frippertronics ISR→prepare stop flag, underrun at a loop seam) are
  reasoned-through but not stress-tested.

### Feature backlog
- **Slots** — [done] 8 per deck under `/tapes/`, Alt+PITCH select. Next: a recorded-vs-empty slot
  indication (probe with `f_stat`) and more banks.
- **Seek / scrub** within the long file (`f_lseek` + ring flush).
- **Transport sync** + tempo-aligned loop points; **reverse** playback (Rev pad reserved).
- **int16 streaming** (halves SD bandwidth — the main throughput lever).
- **Loop-mode tuning** — the seam-fade length and Frippertronics decay/floor are first-cut; tune by ear.
  Progress/level meters on the rings; wider/quantized varispeed.

### Docs / housekeeping
- `CHANGELOG.md` — [done] `[Unreleased]` tape entry (kept current with the dual-deck work).
- `README.md` / `docs/engines/README.md` — [done] `tape` in the engine lists, build options, flash targets.
- `docs/engines/tape.md` — [done] rewritten to the dual-deck state.
- Commit the changeset (user does).

## Build-system notes

- All streaming code is `#if defined(SPK_ENGINE_TAPE)` so non-tape engines are byte-identical (granular
  only +8 B from the shared `EngineContext` field). The `storage.h` unmount guard and the `wav.h` channels
  param default (2) likewise leave non-tape builds unchanged.
- The guarded TUs (`stream_deck.o`, `fat_file.o`, `buffer.sdram.o`) depend on `build/.engine-stamp` (like
  `app.o`) so `make ENGINE=tape` over a stale build doesn't relink empty objects. The granular-engine
  switch-without-clean contamination is a separate, pre-existing limitation (the `engine-*` targets
  `make clean` anyway).

## Broader session context (already committed / done)

- `reso` engine shipped (Mutable Instruments Rings voice; renamed from `karp`); pitch bug root-caused
  (`cv_voct` clobbering the knob) + fixed; defaults engine-seeded; Alt+PITCH model selector; vendored
  Rings/stmlib trimmed under `src/engine/reso/thirdparty/`. See `docs/engines/reso.md`, `CHANGELOG.md`
  `[0.2.2]`.
- Opt-in CMake build added (additive; `make -f Makefile.cmake`); the `make` build stays canonical.
