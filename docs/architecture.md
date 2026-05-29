# Spotykach Architecture

This document describes the big-picture architecture of the Spotykach firmware: the
hardware platform, the execution model, how the subsystems fit together, and how audio
and control data flow through the system. For a file-by-file map of the source tree see
[source-guide.md](source-guide.md). For an analysis of weaknesses and improvement
opportunities see [review-260529.md](review-260529.md).

## 1. What the instrument is

Spotykach is a screenless, dual-deck looping/sampling instrument. Each of the two decks
(A and B) records audio into its own loop buffer and plays it back through a granular
engine that can operate in three modes:

- **Reel** - tape emulation; playback speed and pitch are linked (monophonic).
- **Slice** - digital sampler/looper with independent pitch and speed; up to 3-voice
  polyphony.
- **Drift** - granular texture generator that spreads many overlapping grains from a
  short recording.

On top of the two decks sit shared facilities: a clock/transport with internal, TRS
(4 PPQN) and MIDI (24 PPQN) sync; per-deck modulation sources (LFO or envelope
follower) with CV outputs; two FX families per deck (Grit = saturation/bitcrush, Flux =
tape delay); an A/B crossfade; routing/panning (mono, stereo, generative stereo); a
per-deck step sequencer; and SD-card sample storage organised as 6 "tapes" of 6 slots.

The feature set documented in the user manual is summarised in
[review-260529.md](review-260529.md#appendix-manual-feature-map), where it is also
cross-checked against the code.

## 2. Hardware platform

The firmware targets an Electro-Smith **Daisy Seed** (STM32H7, Cortex-M7) and is built
against a pinned `bleeptools` fork of libDaisy plus DaisySP. Audio runs at **48 kHz**
with a **96-sample** block. The application is linked to run from SRAM and is started by
a custom Daisy bootloader (`APP_TYPE = BOOT_SRAM`, linker script `alt_sram.lds`,
`bootloader-spotykach-v2.bin`), which is why firmware is flashed over USB DFU from the
rear USB-C port rather than the Seed's own port.

Board I/O, all owned and initialised by the `Hardware` class (`src/hw/hardware.h`):

| Function          | Device / bus                       | Notes                                  |
|-------------------|------------------------------------|----------------------------------------|
| Audio in/out      | Daisy SAI codec                    | 48 kHz, 96-sample block                |
| Mod CV out (x2)   | Daisy DAC (DMA)                    | per-deck LFO/mod, 0..+5V               |
| Pots/sliders (15) | `AnalogControl` over analog muxes  | 7 per deck + crossfade                 |
| CV inputs (7)     | `AnalogControl`, bipolar           | size/pos, mix, V/oct per deck + xfade  |
| Pads (12)         | MPR121 capacitive touch (I2C)      | play/rev/grit/flux/seq per deck, spot, alt |
| LEDs (~98)        | WS2812 chain (PWM+DMA)             | two 32-LED rings + indicators          |
| Switches          | two 74HC165 shift registers (SPI-like) | mode, routing, CV-target, tap, trims |
| Clock/gate        | GPIO                               | clock in, gate in A/B, gate out A/B    |
| MIDI              | UART                               | 24 PPQN clock + notes, in and out      |
| Storage           | SD card over SDMMC + FatFS         | FAT32, `SK/` directory                 |

## 3. Execution model

There is no RTOS. Concurrency comes from interrupts layered over a single main loop.
`AppImpl` (`app.cpp`) owns the five top-level subsystems and wires the callbacks in
`AppImpl::Init`:

```
Core      _core;     // all DSP + musical timing
CoreUI    _ui;       // controls, LEDs, pads, MIDI
Hardware  _hw;       // Daisy peripherals and board I/O
Settings  _settings; // calibration data in QSPI flash
Storage   _storage;  // SD-card samples + text config
```

Four execution contexts run concurrently, ordered here from highest to lowest timing
priority:

1. **Audio callback** - `AppImpl::ProcessAudio` (48 kHz / 96 frames, ~500 us per block).
   Reads analog controls, ticks the UI's per-sample needs (`_ui.tick`, `_ui.read_cv`),
   then runs `Core::process`. This is the hard real-time path: no allocation, no
   blocking, no SD or flash access.
2. **DAC callback** - `DACCallback`. Per sample, generates the two modulation/LFO CV
   outputs via `Core::mod(deck).process` and feeds the UI's LFO display.
3. **TIM5 timer ISR** - `T5Callback` at 250 Hz. Polls the gate inputs and renders LEDs
   (every fourth call, so LEDs refresh at ~62 Hz).
4. **Main loop** - `AppImpl::Loop`. Runs non-real-time work: `CoreUI::process` (pads,
   MIDI, switches, value display, gate out), `Core::prepare`, `Storage::process`
   (the cooperative SD state machine), and the "boot button held 3s -> reset to
   bootloader" check.

The deliberate split is that **SD-card and flash I/O live only in the main loop**, never
in the audio path, and large buffers are pre-allocated up front so the audio path never
allocates. The main risk in this model is that the main loop and the audio callback
share data (deck/UI state) without locks; correctness relies on the audio path only
reading parameter snapshots and on writes being word-sized. See the review for the race
analysis.

## 4. Subsystem map

```
                         +-------------------+
            pads/pots --> |      CoreUI       | <-- MIDI in
            switches  --> | (src/ui)          | --> MIDI out, gate out
                         +----+----------+----+
                              |          ^
                       params|          | state (playheads, levels, progress)
                              v          |
        audio in  ----->  +---+----------+----+  -----> audio out
                          |       Core        |
                          |    (src/core)     |  -----> mod CV out (via DAC cb)
                          +--+-----+------+---++
                             |     |      |   |
              +--------------+     |      |   +-------------+
              v                    v      v                 v
        +-----------+        +---------+ +--------+   +-----------+
        |  Deck A   |        | Deck B  | | Driver |   |  Panner   |
        | (looper+  |        |         | | clock/ |   |  XFade    |
        |  granular)|        |         | | sync   |   |  Click    |
        +-----+-----+        +----+----+ +----+---+   +-----------+
              |                   |            |
              v                   v            v
        +-----------+      large buffers   tempo/ticks
        | SDRAMBuffer pool (src/hw/buffer.sdram) |
        +----------------------------------------+
              ^
              |  samples (load/save), config
        +-----+-----------------+
        | Storage / Card / FatFS|  (src/memory, src/hw/card)
        +-----------------------+
```

`CoreUI` is the only subsystem that talks to both `Hardware` and `Core`. It translates
hardware events into musical actions on the core, reads core state back out to drive the
LEDs and gate/MIDI outputs, and drives `Storage` for the SD-card UI. `Core` does not know
about hardware; it is a self-contained DSP graph fed parameters and producing samples.

## 5. Core DSP graph

`Core` (`src/core/core.cpp`) holds the two decks plus the shared `Driver`, `XFade` (the
A/B crossfade mix), `Click`, `Panner`, and a per-deck `Modulator`. The per-sample signal
flow in `Core::process` is, conceptually:

```
in[L,R] --route--> Deck A.process_out --> panner --\
                                                    >-- XFade(mix) --> +click --> SoftLimit --> out[L,R]
in[L,R] --route--> Deck B.process_out --> panner --/
```

with a cross-record feedback path: each deck's input for recording can be the external
input or the *other* deck's output, selected by `Deck::Source` (external/internal), which
is what enables cross-deck recording and resampling. The `Route` enum (DoubleMono,
Stereo, GenerativeStereo) selects the input topology and, via `infer_panner_mode`, the
panner behaviour.

### Clock / transport

`Driver` (`src/core/driver.cpp`) is the transport. `SynClock` runs a 48-PPQN internal
timeline that can phase-lock to an external 4-PPQN (TRS) or 24-PPQN (MIDI) source using
fractional tick accumulation and a tempo-adjustment feedback loop. `Tempo` provides
tap-tempo (averaged, with outlier rejection, clamped 20-250 BPM). `Divider` turns the
PPQN stream into musical triggers (default 1/16, with swing/triplet options). The driver
distributes ticks to both decks and to the modulators, fires "key" and "quarter"
callbacks used by the UI for LEDs and clock-out, and converts incoming TRS clock to
outgoing MIDI clock.

### Deck

Each `Deck` (`src/core/deck.cpp`) is a complete looper voice:

- `Buffer` - the loop buffer with a record/overdub state machine (idle/fadein/sustain/
  fadeout, 4 ms Hann fades) and feedback-controlled overdub. Reads use linear or cubic
  interpolation for pitch-shifted playback.
- `Generator` + `Vox` - the granular engine. The generator owns start/size/spread/
  pitch/speed, slicing (manual and automatic), and reverse, and runs up to
  `kVoxCount` (3) voices, each managing several overlapping windowed grains. Mode
  (Reel/Slice/Drift) selects linear-vs-spread grain behaviour and tape-vs-digital speed
  semantics.
- `Track` - a per-deck step sequencer recording `Event`s (note plus parameter locks)
  onto a fixed grid; patterns persist across mode changes.
- `Dispatcher` - polyphonic voice allocation between the sequencer/clock events and the
  granular voices, with a "hold" voice for monophonic modes.
- `Fx` - the per-deck Grit (Drive = overdrive, or Reduce = decimator/bitcrush) and Flux
  (echo delay) effects, each soft-switched on/off.
- `Detector` - onset detection used to auto-start recording at the -40 dB threshold.

### Modulation

Per deck, a `Modulator` wraps an `LFO` (sine/square/saw/sample-and-hold) and a
`Follower` (envelope follower on the deck output). It can free-run (0.01-12 Hz) or sync
to musical divisions (1/32 to 4 bars). Its output drives both an internal modulation
target (loop start/size, pitch, mix) and the physical mod CV output via the DAC callback.

## 6. Memory model

All large buffers live in external SDRAM and are handed out once at init by the
`SDRAMBuffer::pool()` singleton (`src/hw/buffer.sdram.cpp`):

- Per-deck source (loop) buffers sized for `kSourceMaxSeconds` (42 s) of stereo audio,
  plus a third source buffer.
- Per-deck/per-channel detector and delay (echo) buffers.
- Per-deck slice-point arrays and `Track` event buffers.
- A 32 KB staging buffer for SD-card chunked I/O.

`Core::init` pulls these pointers from the pool and passes them into each deck through a
`Deck::Params` struct; decks and effects never allocate their own large storage. Smaller
state lives in the objects themselves (statically allocated). Calibration data persists in
QSPI flash via `Settings` and libDaisy's `PersistentStorage`; user config (MIDI channels,
preload) persists as `SK/config.txt` on the SD card and is parsed by `Config`.

## 7. Control and persistence flow

- **Pots/sliders**: read in the audio callback, debounced/smoothed by libDaisy
  `PotMonitor`, enqueued onto a `UiEventQueue`, drained in the main loop, and applied to
  the core through `MValue` wrappers that implement pickup/catch behaviour to avoid
  parameter jumps.
- **Pads**: MPR121 touch polled in the main loop; touch/release dispatched through
  callbacks into `CoreUI`, which encodes the short/long/hold and Alt-combination
  gestures.
- **CV**: read per audio block; V/oct mapped to speed via a precomputed `SpeedMap`.
- **Storage**: SD operations are a cooperative state machine driven from the main loop;
  `init_*` operations and config/preload reads are blocking but bounded and only happen
  outside the audio path. The most-recent sample per deck can auto-preload on boot.

## 8. Conventions

- Most non-trivial classes are non-copyable via the `NOCOPY` macro (`src/nocopy.h`).
- Several services are singletons: `SDRAMBuffer::pool`, `Config::dynamic`,
  `Expose::values` (debug value printing), `Meter::cpu` (CPU load metering).
- `infrasonic` namespace (`common.h`) holds shared math/log helpers and the `INFS_*`
  macros; logging compiles out unless built with `DEBUG=1`.
- Dotted filenames denote variants of a concept: `fx.drive.cpp`, `fx.reduce.cpp`,
  `core.ui.midi.cpp`, `random.lfo.h`.
