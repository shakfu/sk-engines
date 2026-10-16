# Granular — dual granular looper / sampler

`ENGINE=granular` (default) · `src/engine/granular/` (~50 files: adapter `granular_engine.{h,cpp}` + DSP) · class `GranularEngine`

The original instrument and the default build: a two-deck granular looper/sampler with recording, overdub, feedback, a per-deck step sequencer, FX, and CV/MIDI. This is the largest engine, and the one the platform's whole knob/pad grammar was designed around.

> Implementation, the DSP graph, and the file map live in [`docs/dev/granular-impl.md`](../dev/granular-impl.md). > Everything this page does **not** cover — power, clock and sync, CV/gate, routing, the SD card, > `config.txt`, MIDI and flashing — is shared by every engine and lives in [`../manual.md`](../manual.md).

**This page is the exhaustive control reference for granular.** It used to live in `docs/manual.md`, which is titled as the Spotykach manual and read as though granular were *the* instrument — awkward for a project of interchangeable engines. The platform half of that document stayed put; the granular half is below.

## Decks and modes

Each deck records into its own loop buffer (up to 42 seconds) and plays it back through a granular engine in one of three modes, indicated by color:

- **Reel (yellow)** - tape emulation, monophonic. Speed and pitch are linked: faster playback raises pitch.

- **Slice (blue)** - digital sampler/looper with independent pitch and speed. Up to 3-voice polyphony; set a deck to mono with `slc_mn_a` / `slc_mn_b` in `SK/config.txt`.

- **Drift (purple)** - granular texture generator; builds evolving soundscapes from a short recording.

## Recording and playback

- **Arm**: Alt+Play. Recording starts when the input crosses about -40 dB.

- **Stop and loop**: tap Play; the recorded material begins looping.

- **Overdub**: Alt+Play on a deck that is already playing. Overdub decay is set with Alt+Mix (feedback).

- **Cross-deck record**: Alt+Reverse records one deck into the other.

- **Reverse**: the Reverse pad plays the loop backwards.

## Primary controls (knobs)

| Knob       | Reel                    | Slice                          | Drift                  |
|------------|-------------------------|--------------------------------|------------------------|
| Pitch      | playback speed          | pitch (independent of speed)   | grain pitch modulation |
| Position   | loop start point        | loop start, quantized to 1/8   | grain position         |
| Size       | loop length (exp.)      | loop length, snapped to 1/8 (1/16 with Alt) | grain spread |
| Envelope   | loop envelope: off, fade-out, fade-in/out, fade-in (cycles) | as Reel | as Reel |
| Mix        | input vs playback balance; Alt+Mix sets overdub feedback | same | same |

Alt+Pitch enables quantized pitch values. Alt+Size sets grain size in Drift and the finer 1/16 size snap in Slice.

Alt+Position sets the start-offset walk, in Slice or on a cue-sliced buffer. At 0 the start stays
where Position puts it. Above 0 it advances one size-worth of slices every 1..8 triggers and wraps at
the end of the buffer, so a fixed loop moves through the recording instead of repeating. Stopping the
deck, or returning the knob to 0, rewinds it to the Position point.

## Effects

Two families, each on a dedicated pad per deck. Hold the effect pad and tap the Tap button to cycle the effect type within the family; shape parameters live on the main knobs while the effect is held.

- **Grit**: signal degradation. Grit + Tap cycles three types; the Grit LED shows which.

  - Analog saturation (yellow) - soft saturation through to distortion (driven by Pitch).

  - Bit crusher / decimator (orange) - downsampling and bit reduction to harsh digital clipping.

  - DJ filter - Pitch left of centre closes a low-pass (violet), right of centre opens a high-pass (green); Position sets resonance. The ring shows both halves and the active cutoff.

- **Flux**: tape delay - Pitch sets the delay time, Mix sets wet level, Position sets feedback while Flux is active. Flux + Tap switches between free and clocked delay. Clocked delay sets the time as a note value of the clock - 1/32, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 4/4 - and blinks the Flux LED with the clock.

Each effect can be locked on (Alt + effect pad) so it stays engaged without holding.

## WAV cue markers

A loaded WAV's `cue ` markers become slice points, with the end of the buffer as the last one. `cue_sm_a` / `cue_sm_b` in `SK/config.txt` decide how a deck uses them: 0 ignores them (default), 1 snaps start and size to them, 2 snaps size to them only while Alt is held. Up to 255 markers per file.

## MIDI CC

Per deck, on that deck's MIDI channel (`mid_ch_a` / `mid_ch_b`). Values are 0-127 across the knob's range; the pad CCs act on any value above 0.

| CC | Control | CC | Control | CC | Control |
|---|---|---|---|---|---|
| 3 | crossfade | 24 | pitch | 90 | mod amount |
| 14 | record (input) | 25 | mix | 102 | grit on |
| 15 | record (other deck) | 26 | overdub feedback | 103 | grit intensity |
| 20 | position | 27 | env size (Drift) | 104 | grit mix |
| 21 | start-offset walk | 28 | window size (Drift) | 105 | flux on |
| 22 | size | 85 | play | 106 | flux time |
| 23 | envelope | 86 | play reverse | 107 | flux feedback |
| | | 89 | mod rate | 108 | flux mix |
| | | | | 109 | grit character (filter resonance) |

## Modulation sources A and B

Each deck has a modulation source that is either an LFO or an envelope follower, with its own CV output (0 to +5 V).

- Source A: sample-and-hold or square LFO.

- Source B: sine or sawtooth LFO.

- The Cycle knob sets LFO rate, with tempo-sync divisions from 1/32 up to 4 bars.

- The Glow knob attenuates the modulation depth.

- The envelope follower tracks its deck's post-Mix output.

## Sequencer

Each deck has a 1/16-resolution trigger sequencer. Patterns persist across mode changes.

- **Record**: Alt+Seq to arm (Alt LED blinks white), then tap the deck's Seq pad in rhythm. Tap Alt or Play to stop.

- **Clear**: hold Alt+Seq for about 2 seconds (the Play LED blinks quickly).

## Under the hood

The rest of this page is orientation for the implementation.

### Modes (the routing/sequencer behaviour above, in code)

- **Slice** — playback and recording are synced to the clock.

- **Reel / Drift** — playback and recording are unsynced (free).

- When the **sequencer** is engaged, the clock drives playback in all modes.

These per-mode decisions live inside `Deck`; the engine forwards every transport tick to every deck and the deck's mode logic decides whether to act (see the transport-decoupling note below).

## Transport

Granular `Core` subscribes to the platform transport (`ITransport::set_on_tick`) and fans each tick out to the decks / panner / modulators / metronome click (the `Core::_on_transport_tick` sink — the granular half of the old `Driver`, which was split into the platform `Transport` + this sink). It also reads `is_key_sub_quarter()` for sequencer arming. Tempo, tap, clock-source, key-interval, and clock-out are platform-owned (see [README](README.md#the-transport-shared-clock)).

## Controls

Granular is the engine the platform's knob/modifier grammar was designed around, so it uses the full surface (the modifier columns in the [README routing table](README.md#knobs-how-a-physical-control-reaches-an-engine)): the direct knobs (Size/Pos/Speed/Mix/Env), the Flux/Grit FX-pad layers, the Alt layer (Feedback, the Slice size snap, start-offset walk, mod sync), and the tap-hold layer (Tempo, KeyInterval, ClickMix, Pan). Pads drive record / play / reverse / sequencer-arm / FX, and MIDI notes trigger pitched grains. The mode/route switches go through `set_config`.

For the exhaustive control reference, the device's user manual is the authority (see the project `spotykach-manual` notes); `docs/architecture.md` covers the platform/engine layer.

## Persistence

`Storage` saves/loads loop audio and `config.txt` on the SD card via the engine's byte-range audio port (`audio_data` / `audio_recorded_bytes` / ...); `Settings` persists smaller settings. Granular advertises the tape-storage capability.

## Notes vs the published manual

The published manual's "Known Limitations" list is partly out of date relative to this firmware:

- **Gate outputs**: implemented here (short pulse on granular re-trigger); the published manual lists them as not implemented.

- **Buffer clearing**: a buffer clear exists internally and runs when re-arming a deck, but there is no dedicated user-facing "clear without recording" gesture yet.

- **Sample persistence between power cycles**: an auto-preload path exists; confirm the exact behaviour on your unit.

- **Spot pad**: currently used only for calibration entry; broader functionality is not yet implemented.

When you change any of the above (or any feature) in code, update this section and the relevant body text so the manual stays in sync.
