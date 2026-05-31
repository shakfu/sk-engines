# Platform / engine refactor — status & resume guide

## Goal

Turn this firmware into a **fixed hardware/UI platform** that hosts a swappable DSP
**engine**, so future firmware variants reuse the board + the whole interaction language
(multi-function knobs, pickup, ring feedback, pad gestures, transport, storage) and only swap
the parameters and DSP. (Organelle/Axoloti/plugin-host model on fixed hardware.) The granular
looper is the first/reference engine.

## Where it stands — LED migration in progress

The **input side + CV/gate/storage are fully on the engine**; the **LED rendering** is the last
coupling and is now being migrated round by round (prep + Round 1 structural + Round 2 indicators
done and flash-verified; the ring steady-state and transport indicators remain — see below).
Everything builds, flashes, and runs at each round; every step is behavior-preserving.

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

### Still coupled — the `engine.core()` escape hatch (LED migration in progress)

In `core.ui.leds.cpp` the only remaining `Core` read is **the ring steady-state** (`_draw_ring`) —
buffer segment, playheads, record/overdub heads. This is Round 3 (next): the steady-state is
interleaved with transient `MValue` value-displays (`_show_value(pos)`, the size-change arc,
`_show_pitch`, poly-slice) that draw *relative to the segment geometry*. The fix has the engine
draw the steady-state and return the geometry the platform overlays render against; the `MValue`
overlays stay platform (knob pickup state). After Round 3 the LED file no longer touches `Core`.

Separately (not a LED concern), `core.ui.cpp`/`pads`/`midi` still call `_core.driver()` for
transport *actions* (`tick`/`toggle_play`/`reset`), kept by design — so the `engine.core()` hatch
survives the LED migration; cutting it is the transport-ownership round (see roadmap).

### LED migration progress (committed, flash-verified)

- **Prep — hardware-free ring canvas + SRAM headroom.** `LEDRing` lost its `apply(Hardware&,LedId)`
  member and `hw/hardware.h` include; it now blits through a templated `apply(Sink)` (the platform
  supplies a pixel sink; the chain-index remap `set_led` moved platform-side). The cache +
  `is_updated` double-buffer handshake stays inside the canvas. `DisplayModel` holds `LEDRing
  ring[2]`. `-Os` added to `core.ui.cpp`. (Key constraint to preserve: `LEDRing` is a
  producer/consumer across two contexts — main loop draws, the TIM5 ISR blits + resets
  `is_updated`.)
- **Round 1 — structural.** `CoreUI`'s ring storage became a `DisplayModel _display` member
  (`_ring[ref]` -> `_display.ring[ref]`), so the model is the platform's live ring buffer that
  `engine.render(DisplayModel&)` will fill. Behavior-identical.
- **Round 2 — indicators.** `GranularEngine` exposes `fx_leds`/`play_leds`/`alt_leds` (POD state
  structs); `_draw_fx`/`_draw_play`/`_draw_alt` read those instead of `_core.deck()/.fx()/.track()`,
  keeping the platform's color palette + blink/timer/storage logic. Those three are now
  `_core.`-free (faithful query substitution, same shape as the 3c pad migrations).
- **Round 2.5 — transport/topology.** `GranularEngine` exposes `transport_leds`/`deck_leds`/`mix`/
  `route`; the ISR `_draw_leds`, `_draw_launching`, and `_show_key_intervals` read those, and the
  `clock_*_color` helpers take state instead of `Driver&`. **`_draw_ring` is now the sole remaining
  `_core.` reader in `core.ui.leds.cpp`** — Round 3 (the ring steady-state) is cleanly isolated.

  Note: "LEDs off `core()`" is not the same as "the `core()` hatch is removed". `core.ui.cpp`/
  `pads`/`midi` still call `_core.driver()` for transport *actions* (`tick`/`toggle_play`/`reset`),
  kept by design pending the transport-ownership decision (see roadmap).

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
region, currently ~99.3% full (~1.25 KB free). ITCMRAM relocation was rejected (under BOOT_SRAM
the load image can't cleanly leave SRAM_EXEC without relying on unverifiable bootloader load
behavior). The three non-RT UI TUs `core.ui.leds.cpp`, `core.ui.pads.cpp`, and `core.ui.cpp` are
compiled `-Os` (`#pragma GCC optimize`) to claw back space; the audio DSP stays
`-O2 -funroll-loops`. The `-Os`-on-`core.ui.cpp` lever has been spent. If Round 3 needs more
headroom, the remaining levers are `-Os` on `core.ui.midi.cpp` or tagging individual migrated
`render()` methods `__attribute__((optimize("Os")))` while `process()` stays `-O2`.

## Resume roadmap

1. **Round 3 — the ring → `render(DisplayModel&)`** (the hard piece). Move `_draw_ring`'s
   steady-state (segment/playheads/heads) into the engine, filling `_display.ring[ref]`. Co-design
   it with the **`MValue` → platform toolkit keyed by `ParamId`** (engine-declared per-param
   color/format) so the platform owns the transient value-display overlay that currently
   interleaves with the steady-state. Resolve the compositing rule (transient vs steady-state) and
   have the engine return the segment geometry the overlays render against.
2. **Transport / topology indicators** — get `_draw_leds`/`_draw_launching`/`_show_key_intervals`
   off `driver()`/`mod()`/`route()`/`deck.mode()`. Likely entails deciding whether `Driver`/
   transport stays in `Core` or becomes a platform-owned service. After this the `core()` hatch can
   be cut.
3. **Lift the shared `IEngine` interface** — `CoreUI`/`Storage` hold a concrete `GranularEngine&`/
   `*`; a 2nd engine needs the granular surface lifted to the interface (or per-engine dispatch).
4. **2nd engine** (e.g. the `PassthroughEngine` sketch promoted to a firmware variant,
   `Capabilities = {Transport}`) — flushes hidden coupling, validates capability opt-in.
5. **Build/boundary enforcement + DSP libraries** — split into compile units / static libs,
   drop the blanket `-Isrc/`, separate platform/engine include roots.

Detailed per-phase notes live in the planning doc the maintainer keeps outside the repo.
