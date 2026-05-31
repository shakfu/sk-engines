# Platform / engine refactor — status & resume guide

## Goal

Turn this firmware into a **fixed hardware/UI platform** that hosts a swappable DSP
**engine**, so future firmware variants reuse the board + the whole interaction language
(multi-function knobs, pickup, ring feedback, pad gestures, transport, storage) and only swap
the parameters and DSP. (Organelle/Axoloti/plugin-host model on fixed hardware.) The granular
looper is the first/reference engine.

## Where it stands — PAUSED at the input-decoupled milestone

The **input side is fully on the engine**; the **output/IO side is deferred**. Everything
builds, flashes, and runs; this is a deliberate stopping point, not a half-finished state.

### Layers today

- **`src/core/`** — the DSP graph (`Core` + two `Deck`s, `Generator`/`Vox`, `Buffer`, `Fx`,
  `Driver`/transport, `Track`, `Panner`, `Modulator`). Now **platform-independent**: no
  libDaisy include; all hardware dependencies injected via `EngineContext` (`itimesource.h`,
  `engine_context.h`) — sample rate, block size, buffers, and an `ITimeSource`.
- **`src/engine/`** — the contract + the granular engine.
  - `iengine.h` — `IEngine`: the **active** interface is the audio lifecycle only
    (`init`/`prepare`/`process`). The full target interface (onControl/onGesture/onMidi/
    render/param/bindings/capabilities) is documented in-file as the design target but **not
    yet abstract** — see the caveat below.
  - `granular_engine.{h,cpp}` — `GranularEngine : IEngine`. Owns `Core`. Beyond the audio
    lifecycle it holds the **granular input API** the platform now drives:
    `set_param`/`param`/`capabilities`, `handle_midi_note`/`handle_midi_transport`,
    `set_fx`/`toggle_fx_lock`, `on_play_pad`/`on_record_pad`/`stop_if_generating`/
    `clear_buffer`, `on_seq_toggle_arm`/`on_seq_trigger`/`clear_sequence`/`disarm_track`.
    `engine_params.h` defines `ParamId`, `FxKind`, and the `Capabilities` bitset.
- **`src/ui/` (CoreUI)** — the platform. Reads pots/pads/CV/MIDI, owns the interaction
  mechanics (`MValue` pickup, `Hold` timers, the `_touched` modifier bitset, gesture
  recognition, storage/tape, LED rendering), and drives the engine through its API.
- **`src/hw/`, `src/memory/`** — HAL + settings/SD storage.

### Decoupled (done)

- **Knobs → params** — `core.ui.cpp`'s apply pass calls `engine.set_param(ParamId, deck, v)`;
  the engine owns the mode-dependent dispatch (Reel/Slice/Drift). Two stragglers remain direct
  (`Speed` middle-pitch readback, `ModSpeed` alt-sync) — to be folded into the param API later.
- **MIDI** — `core.ui.midi.cpp` parses MIDI + clocks transport (platform); note→trigger and
  transport→play/stop are `engine.handle_midi_*`.
- **Pads** — `core.ui.pads.cpp` is **deck-DSP-free**; all flux/grit/play/rev/seq/alt actions go
  through the engine. Only transport (`driver`) calls remain, by design.
- **CV + gate** — `read_cv` calls `engine.cv_mix`/`cv_size_pos`/`cv_voct`/`cv_crossfade`
  (platform still does the hardware read + calibration); `process_gate_in`/`_process_gate_out`
  call `engine.on_gate_trigger` / `engine.gate_out_triggered` (platform keeps edge/latency +
  the gate-out pulse). `cv_voct` caches the V/Oct speed the gate trigger uses.
- **Storage** — `Storage`/`DeckStorage` hold a `GranularEngine*` (not `Deck*`) and save/load the
  loop buffer through a byte-based audio port (`audio_data`/`audio_recorded_bytes`/
  `audio_capacity_bytes`/`audio_apply_loaded`/`audio_is_empty`). The tape/slot state machine,
  preload, and SD `Card` I/O stay platform.

### Still coupled (deferred) — the `engine.core()` escape hatch

The platform reaches the granular `Core` directly for **one** thing only (see the `core()`
comment in `granular_engine.h`). A non-granular 2nd engine can't render its own UI until it
moves to an engine-side handler:

1. **LED rendering** (`core.ui.leds.cpp`) — reads deck/buffer/generator/fx state to draw rings
   + indicators, interleaved with the `MValue` value-display overlay. The hard redesign
   (`render(DisplayModel&)` + moving `MValue` into a platform toolkit).

### Grounding sketch (done) — `DisplayModel` + `PassthroughEngine` (host-only)

Before committing to the LED redesign, the display contract was made concrete against a real
second consumer. Two new headers, **not wired into the firmware** (`app.cpp`/`src/ui/` unchanged,
so zero on-device impact — they only compile into the host test):

- **`src/engine/display_model.h`** — hardware-agnostic panel data an engine's `render()` fills
  and the platform blits to WS2812: `Pixel ring[2][32]` + named `Indicator`s (`play/rev/grit/
  flux/gate_in/cycle/alt`, `mode_left/center/right`, `clock_in`, `fader[2]`, `spot`) + `clear()`.
  The engine never touches `Hardware`/`LEDRing` (those carry `apply(Hardware&)`); it only fills
  this data and the platform owns the model->`LedId` blit. The real LED migration will likely add
  `set_segment`/`add_point` primitives by extracting `LEDRing`'s drawing half (minus `apply`).
- **`src/engine/passthrough_engine.h`** — a deliberately non-granular `PassthroughEngine : IEngine`:
  stereo passthrough `process` (tracks last-block peak), `capabilities() == CapTransport` only
  (opts out of Recording/Tape/Sequencer), and `render(DisplayModel&)` drawing a level meter on
  both rings + lit play indicators. Its comment enumerates the **full** platform-driven surface a
  real 2nd engine must implement (`set_param`/`param`, `handle_midi_*`, `set_fx`/`on_*_pad`,
  `cv_*`/`on_gate_*`, the `audio_*` storage port, `render`) and notes these belong on a **lifted
  shared interface** — today `CoreUI`/`Storage` hold a concrete `GranularEngine&`/`*`.

`host/test_engine_params.cpp` got a passthrough smoke (capabilities, in==out passthrough, render
lights rings/play, silence collapses the meter and proves `clear()`). What the sketch grounds: the
`DisplayModel` contract holds for a non-looper, capability opt-in works, and the "lift a shared
interface" requirement is now explicit in code. `make -j8` SRAM_EXEC stays 99.37% (unchanged).

### Caveat: concrete vs abstract

The granular input methods live on the **concrete `GranularEngine`**, not on the abstract
`IEngine`. `CoreUI` holds a `GranularEngine&`. That's fine for one engine; a second engine
would require lifting a shared interface (or the platform dispatching per engine type).

## Off-target testing

`make -C host` builds a desktop harness (`host/main_host.cpp`) that runs the real `Core`/
`GranularEngine` over WAV via the `IEngine` interface; `make -C host test` runs
`host/test_engine_params.cpp` (asserts the param API across all deck modes). The host **cannot**
exercise the pot/pad/LED/MIDI hardware paths — those changes are verified by flashing.

## Memory budget

The app boots from SRAM (`BOOT_SRAM`, `alt_sram.lds`); code lives in the **186 KB SRAM_EXEC**
region, currently ~99.2% full (~1.4 KB free). ITCMRAM relocation was rejected (under BOOT_SRAM
the load image can't cleanly leave SRAM_EXEC without relying on unverifiable bootloader load
behavior). The two largest non-RT UI TUs (`core.ui.leds.cpp`, `core.ui.pads.cpp`) are compiled
`-Os` (`#pragma GCC optimize`) to claw back space; the audio DSP stays `-O2 -funroll-loops`.
**The deferred LED migration needs more headroom** — next safe lever is `-Os` on `core.ui.cpp`
(its only audio-adjacent code, `read_cv`, is per-block 500 Hz, not per-sample).

## Resume roadmap

1. **LEDs → `IEngine::render(DisplayModel&)`** (largest/riskiest). The `DisplayModel` is already
   sketched concrete to the panel (`src/engine/display_model.h`, 2×32-px rings + named indicators)
   and validated by `PassthroughEngine` (see "Grounding sketch" above); this round makes the
   granular engine fill it. Likely extract `LEDRing`'s drawing half (minus `apply`) into ring
   primitives; move `MValue` into a **platform toolkit keyed by `ParamId`** with engine-declared
   per-param color/format so the platform owns the value-display overlay. Resolve the ring
   compositing rule (transient value-display vs steady-state).
2. **CV / gate / storage** off the granular `Core` (engine `onCV`, gate/trigger handler,
   storage capability). Co-requisite with LEDs for a 2nd engine.
3. **2nd engine** (e.g. passthrough/delay, `Capabilities = {Transport}` only) as a separate
   firmware variant — flushes hidden coupling, validates capability opt-in, and grounds the
   `DisplayModel`/render design with a real second consumer.
4. **Build/boundary enforcement + DSP libraries** — split into compile units / static libs,
   drop the blanket `-Isrc/`, separate platform/engine include roots.

Detailed per-phase notes live in the planning doc the maintainer keeps outside the repo.
