# Granular — dual granular looper / sampler

`ENGINE=granular` (default) · `src/engine/granular/` (~50 files: adapter `granular_engine.{h,cpp}` + DSP) · class `GranularEngine`

The original instrument and the default build: a two-deck granular looper/sampler with recording, overdub, feedback, a per-deck step sequencer, FX, and CV/MIDI. This is the largest engine; this page is an orientation. Deeper material lives in `docs/architecture.md`, `docs/engine-layout.md`, and the refactor-history docs.

> Implementation, the DSP graph, and the file map live in [`docs/dev/granular-impl.md`](../dev/granular-impl.md).

## Modes (the routing/sequencer behavior the manual describes)

- **Slice** — playback and recording are synced to the clock.

- **Reel / Drift** — playback and recording are unsynced (free).

- When the **sequencer** is engaged, the clock drives playback in all modes.

These per-mode decisions live inside `Deck`; the engine forwards every transport tick to every deck and the deck's mode logic decides whether to act (see the transport-decoupling note below).

## Transport

Granular `Core` subscribes to the platform transport (`ITransport::set_on_tick`) and fans each tick out to the decks / panner / modulators / metronome click (the `Core::_on_transport_tick` sink — the granular half of the old `Driver`, which was split into the platform `Transport` + this sink). It also reads `is_key_sub_quarter()` for sequencer arming. Tempo, tap, clock-source, key-interval, and clock-out are platform-owned (see [README](README.md#the-transport-shared-clock)).

## Controls

Granular is the engine the platform's knob/modifier grammar was designed around, so it uses the full surface (the modifier columns in the [README routing table](README.md#knobs-how-a-physical-control-reaches-an-engine)): the direct knobs (Size/Pos/Speed/Mix/Env), the Flux/Grit FX-pad layers, the Alt layer (Feedback, PolySlice, mod sync), and the tap-hold layer (Tempo, KeyInterval, ClickMix, Pan). Pads drive record / play / reverse / sequencer-arm / FX, and MIDI notes trigger pitched grains. The mode/route switches go through `set_config`.

For the exhaustive control reference, the device's user manual is the authority (see the project `spotykach-manual` notes); `docs/architecture.md` covers the platform/engine seam.

## Persistence

`Storage` saves/loads loop audio and `config.txt` on the SD card via the engine's byte-range audio port (`audio_data` / `audio_recorded_bytes` / ...); `Settings` persists smaller settings. Granular advertises the tape-storage capability.
