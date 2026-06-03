# Spotykach Source Guide

A navigational map of the source tree for developers adding or changing features. Read
[architecture.md](architecture.md) first for the big picture. Line numbers are
intentionally omitted here because they drift; names of classes and methods are stable
enough to grep for.

## Top level

| File             | Role |
|------------------|------|
| `main.cpp`       | Entry point. Constructs `spotykach::Application`, calls `Init()` then `Loop()`. |
| `app.h`/`app.cpp`| `Application` is a thin facade over `AppImpl`, which owns all subsystems and wires the audio, DAC and TIM5 callbacks. Start here to understand boot order and the execution contexts. |
| `meter.h`        | Optional CPU-load meter singleton (compiled in behind `#define METER`). |
| `Makefile`       | Build config: sources, C++17, SRAM-boot linkage, the `ENGINE` selector, the `check-boundary` guard, `libs`/`clean-libs` targets. Inherits the generic libDaisy makefile. |

## `src/` root

| File              | Role |
|-------------------|------|
| `common.h`        | `infrasonic` namespace: math helpers (`lerp`, `map`, dbfs conversions, one-pole coefficient), the `Log` alias, assertions, and `INFS_*` macros. |
| `config.h`        | Three things: the runtime `Config::dynamic()` singleton (parses `config.txt`: MIDI channels, play/stop, preload), the compile-time DSP constants (`kPPQNIntern`, window/slice sizes, LFO range, drift coefficients, fade lengths, `kSourceMaxSeconds`), and the shared tempo range + helpers (`kTempoMin/MaxBpm`, `tempo_abs_to_norm`/`tempo_norm_to_abs`). |
| `settings.h`      | `Settings`: calibration data (CV offsets, V/oct references) persisted to QSPI flash via libDaisy `PersistentStorage`. |
| `expose.h`        | `Expose::values()` debug singleton for printing internal values; gated behind `DEBUG`. |
| `nocopy.h`        | `NOCOPY(Class)` macro used throughout to delete copy/assign. |

## `src/engine/` - the platform/engine contract + the engines

The contract headers the platform talks to (`iengine.h`, `engine_context.h`, `itimesource.h`,
`engine_params.h`, `display_model.h`, `engine_leds.h`, `mode.h`, `deck_ref.h`, `color.h`,
`led.ring.h`, `engine_select.h`, `granular_engine.h`) live at the top of `src/engine/`; each engine
has its own subdirectory (`granular/`, `delay/`, `passthrough/`). See
[engine-layout.md](engine-layout.md) for the full contract map and how the boundary is enforced.

## `src/dsp/` - engine-agnostic DSP primitives

Shared primitives any engine (or the platform) may use; the dependency flows platform/engine -> dsp,
never the reverse. A primitive earns a place here once it has a real second consumer.

| File           | Role |
|----------------|------|
| `lutsinosc.h`  | Table-based sine lookup (`LUT_Sin_Value_At`); used by the granular LFO and the UI's LED breathe/rings. |
| `smooth.h`     | `OnePoleSmoother` for click-free parameter smoothing. |
| `deline.h`     | Fractional-read delay line. |
| `hann.h`       | Hann window lookup (grain and record fades). |

## `src/engine/granular/` - the granular engine's private DSP and musical timing

This is where audio is generated. None of it touches hardware directly.

### Orchestration

| File          | Role |
|---------------|------|
| `core.*`      | `Core`: owns both decks, the driver, crossfade, click, panner, modulators. `process()` is the per-block/per-sample signal flow; `prepare()` is the main-loop hook; `set_route`/`infer_panner_mode` handle routing. |
| `driver.*`    | `Driver`: the transport. Distributes ticks to decks and modulators, manages clock source (internal/TS4/MIDI), key intervals, quarter/key callbacks, clock-out, and reset. The `kKeyInterval` enum is contract-owned (`engine/mode.h`); the `kKeyIntervals[]` lookup stays here. |
| `synclock.*`  | `SynClock`: 48-PPQN internal timeline with fractional accumulation and external-sync phase-lock/tempo-adjust. |
| `tempo.*`     | `Tempo`: tap-tempo averaging with outlier rejection, 20-250 BPM clamp (range `kTempoMin/MaxBpm` + the BPM<->norm helpers live in `config.h`). |
| `divider.*`   | `Divider`: PPQN -> musical triggers (1/16 default) with swing and triplet options. |

### Per-deck looping + granular

| File              | Role |
|-------------------|------|
| `deck.*`          | `Deck`: the looper voice. Record/overdub/playback state machine, quantization/grid, mode switching (Reel/Slice/Drift), start/size/feedback, and the glue between buffer, generator, track, dispatcher and fx. Largest and most stateful class. |
| `buffer.*`        | `Buffer`: the loop buffer. Record fades (Hann), overdub feedback, linear and cubic interpolated reads. |
| `generator.*`     | `Generator`: granular engine front-end. Owns start/size/spread/pitch/speed, slicing (`slice`, `auto_slice`, `clear_slices`), reverse, speed modes; drives `kVoxCount` (3) voxes. |
| `vox.*`           | `Vox`: one granular voice; manages multiple overlapping `Window` grains, attack/decay envelope, spread-mode seeding. |
| `window.h`        | `Window`: a single grain - playhead, increment, loop bounds, pan, interpolation choice. Header-only. (Uses `dsp/hann.h`.) |
| `track.*`         | `Track`: per-deck step sequencer over a fixed grid of `Event`s; record/clear/rewind. |
| `dispatcher.h`    | `Dispatcher<N>`: polyphonic voice allocation with a "hold" voice for mono modes. Header-only template. |
| `event.h`         | `Event` struct: note on/off plus four parameter locks with presence flags. |
| `detector.*`      | `Detector`: onset detection for auto-start recording. |
| `follower.*`      | `Follower`: RMS-style envelope follower with exponential attack/release. |

### Effects

| File             | Role |
|------------------|------|
| `fx.*`           | `Fx`: per-deck effect host. Owns Grit (Drive or Reduce) and Flux (echo delay), each soft-switched. Grit mode toggling, intensities, mixes, feedback. |
| `fx.drive.*`     | `Drive`: daisysp Overdrive plus loudness compensation and dry/wet mix. |
| `fx.reduce.*`    | `Reduce`: decimator/bitcrusher plus a lowpass, dry/wet mix. |
| `echo.h`         | `EchoDelay` template used by Flux. Header-only. |
| `biquad.*`       | Biquad filter (has a TODO for an ARM-accelerated path). |

### Modulation + routing

| File              | Role |
|-------------------|------|
| `modulator.*`     | `Modulator`: wraps an LFO and a follower; free-run or tempo-synced; produces internal mod and CV out. |
| `lfo.h`           | `LFO`: sine (table, via `dsp/lutsinosc.h`)/square/saw/random selectable. Header-only. |
| `random.lfo.*`    | `RandomLFO`: sample-and-hold style random source. |
| `panner.*`        | `Panner`: smooth/step/off panning; drives generative stereo. |
| `xfade.h`         | `XFade`: equal-power crossfade used both for A/B mix and per-deck in/out mix. Header-only. |
| `softswitch.h`    | `SoftSwitch` for click-free effect/route enable (uses `dsp/hann.h`). |

### Other helpers

`click.*` (metronome), `cpattern.*`, `adenv.*` (AD envelope), `globals.h`,
`skip.write.head.h`. Granular's `mode.h` redirects to the contract `engine/mode.h`, which owns the
shared enums (`Mode`, `Route`, `ModType`, `GritMode`, `DeckSource`, `ClockSource`, `kKeyInterval`).

## `src/ui/` - controls, LEDs, MIDI

| File               | Role |
|--------------------|------|
| `core.ui.*`        | `CoreUI`: the main UI class and loop. Pot/CV handling, the deferred-apply mechanism, gate out, switches, value display, state machine (launching/init_values/ready). The header declares all the per-deck `MValue`s and LED state. |
| `core.ui.pads.cpp` | Pad gesture logic: play/rev/grit/flux/seq/spot/alt semantics, Alt combinations, hold gestures (sequence clear, storage entry). |
| `core.ui.leds.cpp` | LED rendering: ring drawing, playheads, parameter value display, recording/clock blink, launch animation, breathing. |
| `core.ui.midi.cpp` | MIDI in/out: clock, realtime start/stop/continue, note-on -> trigger, channel routing, deferred transmit. |
| `led.ring.*`       | `LEDRing`: maps normalized positions/segments to the 32 physical ring LEDs with sub-LED interpolation. |
| `color.*`          | Mode/effect color constants and helpers. |
| `mvalue.*`         | `MValue`: knob-to-parameter wrapper with pickup/catch (deadband) tracking; unique ids for change display. |
| `calibrator.*`     | `Calibrator`: CV offset and V/oct linearity calibration (uses blocking sample collection). |
| `speed.map.h`      | `SpeedMap<N>`: precomputed semitone->speed table with interpolation. |
| `hold.h`           | `Hold<ms>`: long-press gesture timer with an indicate threshold. |
| `time.iterval.h`   | `TimeInterval<ms>`: debounce/throttle timer (note the filename typo `iterval`). |
| `voct.h`           | `CalibratedVOct`: V/oct CV to pitch conversion using calibration references. |

## `src/hw/` - peripherals

| File                | Role |
|---------------------|------|
| `hardware.*`        | `Hardware`: owns and initialises every peripheral. Defines the `LedId`, `AnalogControlId`, `Pad`, `CvInputId` enums (the canonical pin/index maps). Exposes pot/CV reads, gate/clock I/O, MPR121, LEDs, MIDI handle, and the `ProcessAnalogControls`/`ProcessDigitalControls`/`ProcessPads` entry points. |
| `buffer.sdram.*`    | `SDRAMBuffer::pool()`: the SDRAM partitioning singleton. Sizing constants live here. |
| `card.*`            | `Card`: FatFS wrapper over SDMMC. State machine for chunked audio read/write and blocking file read/write; WAV validation. |
| `sr_165.*`          | `ShiftRegister165`: bit-banged read of the two 74HC165 switch registers. |
| `ws2812.*`          | `Ws2812`: WS2812 LED driver (PWM + DMA, gamma + dither). Has TODOs for error handling and color order. |

## `src/memory/` - storage and config persistence

| File           | Role |
|----------------|------|
| `storage.*`      | `Storage` (coordinator) and `DeckStorage` (per-deck). Tape/slot model, save/load, boot preload sequencing, the `SK/` layout, and the cooperative `process()` state machine. |
| `wav.h`          | WAV header read/write helpers (expects 48 kHz, stereo, 32-bit float). |
| `pcm_loader.h`   | `PcmLoader`: streaming accounting for loading an audio file body into a buffer (used by `Card`). Self-contained; relocated here from the granular engine in Phase 5 R4. |
| `pcm_convert.h`  | Sample-width conversion helpers used during load. |
| `sample16.h`     | 16-bit PCM sample type + conversion (the `LOFI_INT16` storage format). |

## Where features live (quick index)

- Add a **new playback mode**: `mode.h`, `Deck::_set_mode`, `Generator`/`Vox` mode
  handling, plus UI mode selection in `core.ui.*` and a color in `color.*`.
- Change **recording behaviour**: `Buffer` state machine and `Deck` record methods
  (`toggle_recording`, `_start_recording`, `_clock_recording`, `_set_grid`).
- Add an **effect**: model it on `fx.drive.*`/`fx.reduce.*`, register it in `Fx`, and
  expose UI control in `core.ui.pads.cpp`/`core.ui.leds.cpp`.
- Change **sync/clock**: `Driver`, `SynClock`, `Divider`, `Tempo`; clock-source UI in
  `core.ui.midi.cpp` and `core.ui.cpp`.
- Add a **config option**: extend `Config::Values` and the parser in `config.h`, and read
  it where needed (e.g. `core.ui.midi.cpp` for MIDI).
- Change **storage/SD**: `Storage`/`DeckStorage`, `Card`, `wav.h`, and the storage UI
  gestures in `core.ui.pads.cpp`.
- Re-map **controls/LEDs/pins**: the enums in `hardware.h` are the single source of truth.
