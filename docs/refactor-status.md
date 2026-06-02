# Platform / engine refactor — status & resume guide

## Goal

Turn this firmware into a **fixed hardware/UI platform** that hosts a swappable DSP
**engine**, so future firmware variants reuse the board + the whole interaction language
(multi-function knobs, pickup, ring feedback, pad gestures, transport, storage) and only swap
the parameters and DSP. (Organelle/Axoloti/plugin-host model on fixed hardware.) The granular
looper is the first/reference engine.

## Where it stands — LED migration complete

The **input side, CV/gate/storage, and now all LED rendering are on the engine**. `core.ui.leds.cpp`
no longer touches `Core` (`grep _core.` returns nothing). What remains is not a granular-coupling
problem but the platform-ization tail: the `engine.core()` hatch still survives. A call-site audit
(2026-06-02) found it carries **three** coupling categories, not the "transport actions only" this
doc previously claimed — transport/`Driver`, switch-config writes, and deck-state readbacks (see
"Still coupled" below for the full inventory). Everything builds, flashes, and runs; every step was
behavior-preserving and flash-verified.

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

### Still coupled — the `engine.core()` escape hatch (three categories)

LED rendering is fully migrated, but a call-site audit (2026-06-02) corrected an earlier claim:
the hatch is **not** "transport actions only." Removing `engine.core()` requires absorbing three
distinct categories, and cutting only transport will **not** let `engine.core()` be deleted.

- **Category 1 — transport / `Driver`** (the round the roadmap calls item 1). Distinct methods the
  platform calls: `set_on_quarter`/`set_on_clock_out` (init wiring, `core.ui.cpp:52,55`), `source`/
  `tick` (clock tick, `core.ui.midi.cpp:12,15,20`), `is_external_sync`/`reset` (SeqA gesture,
  `core.ui.pads.cpp:68-69`), `toggle_source` (alt clock gesture, `core.ui.pads.cpp:206` /
  `core.ui.cpp:614`), `is_external_sync`/`tap_tempo`/`tempo` (tap tempo, `core.ui.cpp:629-633`),
  `set_tempo_norm` (set-tempo-by-size, `core.ui.cpp:674`). That is **10 methods, not the 3
  (`tick`/`toggle_play`/`reset`) previously listed** — and `toggle_play` does not appear in the UI
  at all (it is already engine-internal, `granular_engine.cpp:108`).
- **Category 2 — switch-config writes** (`_process_switches`, `core.ui.cpp:541-626`), NOT transport:
  `set_route(...)`, `mod(ref).set_type()/set_lfo_type()`, `deck.set_mode()` + `infer_panner_mode()`,
  `deck.set_start_mod_on()/set_size_mod_on()`, `deck.fx().switch_grit_mode()` + grit readback. These
  are enum/topology configs the scalar `set_param(ParamId, …)` API never absorbed.
- **Category 3 — deck-state readbacks** (apply pass + pot queue), NOT transport: `deck.mode()` for
  `is_chord`/`Slice`/`Drift` dispatch (`core.ui.cpp:119-120,166,178,374,407,451,483`), `deck.is_empty()`
  (`380,457`), `deck.norm_start()`/`deck.fx().grit_*`/`flux_*` for initial `MValue` seeding (`64-85`),
  `deck.tempo_to_fit()` (`671`).

The `Driver`/transport lives in `Core` (engine) today but is conceptually platform. Categories 2-3
are exactly what roadmap item 3's `MValue` → `ParamId` toolkit is meant to generalize (engine declares
its modes/configs; platform stops reading `deck.mode()`), so absorbing them now as bespoke wrappers
builds throwaway scaffolding — the same trap noted for the UI A/B consolidation. See the re-scoped
roadmap item 1.

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
  `pads`/`midi` still call `_core.` for transport, switch-config writes, and deck-state readbacks
  (see the three categories under "Still coupled"), kept by design pending the transport-ownership
  decision (see roadmap).
- **Round 3 — the ring steady-state (completes the LED migration).** `GranularEngine` gained
  `render_ring(LEDRing&, ref, breathe_brightness) -> RingGeometry`, drawing the empty/recording/
  playing segment + playheads + heads and returning the geometry the platform's transient overlays
  (`_show_value(pos)`, the size-change arc, overdub head) render against. `_draw_ring` keeps its
  exclusive-overlay priority chain + the always-tail `MValue` value-displays; the steady-state arms
  collapsed to one `render_ring` call. Byte-faithful port; `render_ring` is `optimize("Os")`-tagged.
  **`core.ui.leds.cpp` is now `_core.`-free.**

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

`host/test_engine_params.cpp` got a passthrough smoke (capabilities, `in==out` passthrough, render
lights rings/play, silence collapses the meter and proves `clear()`). What the sketch grounds: the
`DisplayModel` contract holds for a non-looper, capability opt-in works, and the "lift a shared
interface" requirement is now explicit in code. `make -j8` SRAM_EXEC stays `99.37%` (unchanged).

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
region, currently ~99.5% full (~912 B free). ITCMRAM relocation was rejected (under BOOT_SRAM
the load image can't cleanly leave SRAM_EXEC without relying on unverifiable bootloader load
behavior). The three non-RT UI TUs `core.ui.leds.cpp`, `core.ui.pads.cpp`, and `core.ui.cpp` are
compiled `-Os` (`#pragma GCC optimize`), and `GranularEngine::render_ring` is `optimize("Os")`-tagged;
the audio DSP stays `-O2 -funroll-loops`. The `-Os`-on-`core.ui.cpp` lever has been spent. The next
rounds (transport ownership, interface lift) will want headroom — the remaining lever is `-Os` on
`core.ui.midi.cpp`, then `core.ui.midi`/other non-RT TUs.

## Resume roadmap — remaining tasks

The input side, CV/gate/storage, and all LED rendering are decoupled. What remains is the
platform-ization tail. Each item is its own behavior-preserving, flash-verified round.

1. **Transport off `core()` — Category 1 only (NEXT).** Re-scoped after the 2026-06-02 call-site
   audit: this round absorbs **only the transport/`Driver` category** (the 10 methods listed under
   "Still coupled" Category 1) behind engine methods, using the same query/action pattern as `*_leds`.
   It does **not** remove `engine.core()` — Categories 2 (switch-config writes) and 3 (deck-state
   readbacks) still hold it, so the milestone is "transport off `core()`", paralleling "LEDs off
   `core()`", not "hatch removed". Categories 2-3 are deferred to item 2/3 on purpose: they are
   enum/config dispatch and mode readback that the `MValue` → `ParamId` toolkit (item 3) regenerates
   as engine-declared bindings, so hand-wrapping them now would be throwaway scaffolding.

   Within Category 1 there is still the architectural fork: does `Driver`/transport stay inside `Core`
   (engine) or move out to a **platform-owned transport service**? `Driver` is conceptually platform
   (clock/transport) but lives in `Core` and fans ticks to both decks, so relocation is non-trivial.
   Recommended now: the **stopgap** — expose the 10 transport methods on the engine without relocating
   `Driver` — and defer the relocation to item 2 (interface lift) / engine #2, when a second consumer
   exists to validate the shared-transport boundary. Plan it before coding; it lands entirely in the
   three `-Os`-maxed UI TUs, so bank the SRAM-positive mechanical wins first (see watch-item).
2. **Lift the shared `IEngine` interface.** `CoreUI` holds a `GranularEngine&` and `Storage` a
   `GranularEngine*`; the whole granular surface (`set_param`/`param`, `handle_midi_*`, `set_fx`/
   `on_*_pad`, `cv_*`/`on_gate_*`, `audio_*`, `*_leds`, `render_ring`, plus item 1's transport
   methods) lives on the **concrete** `GranularEngine`, not the abstract `IEngine`. A 2nd engine
   needs this lifted to a shared interface (or the platform dispatching per engine type). This round
   also owns the deferred decisions from item 1: whether to relocate `Driver` to a platform-owned
   transport service (now that a second consumer exists to validate it), and how to absorb the
   remaining `engine.core()` Categories 2-3 — likely folded into the item-3 toolkit rather than
   wrapped here. Prereq for item 3.
3. **2nd engine (Phase 4).** Promote the `PassthroughEngine` sketch (or a simple delay) to a real
   firmware variant, `Capabilities = {Transport}` only. Flushes hidden coupling, validates capability
   opt-in. Likely forces the **`MValue` → engine-agnostic toolkit keyed by `ParamId`** generalization
   (engine-declared per-param color/format) so a non-granular engine gets value-displays on the ring;
   that generalization is what lets us *delete* the platform's per-indicator interpretation (see the
   "Why the refactor added code" note) rather than keep the current query-substitution scaffolding.
4. **Build/boundary enforcement + DSP libraries (Phase 5).** Split into compile units / static libs
   (`libgranular`/`libtransport`/`libfx`/`libseq`), drop the blanket `-Isrc/`, give platform and
   engine separate include roots. *Optional:* template `Buffer` on sample format to retire the
   `LOFI_INT16` switch; make the remaining `config.h` sample-rate constants functions of `sample_rate`.

**Standing watch-item — SRAM_EXEC.** ~912 B free and the migration has been net-additive. Items 1-2
add more surface. Next reclaim lever: `-Os` on `core.ui.midi.cpp`, then other non-RT TUs. ITCMRAM
stays rejected (BOOT_SRAM single-blob load).

Detailed per-round notes (the contracts, the byte-faithful porting constraints, the geometry struct,
etc.) live in the planning doc the maintainer keeps outside the repo (`elegant-baking-panda.md`).

## Why the refactor has not reduced code (and when it will)

The refactor has **added** net code, not removed it — SRAM_EXEC rose ~99.0% → 99.52% across the
rounds, and `-Os` levers were spent to stay under the 186 KB ceiling. This is expected, not a
regression, for the reasons below. The success metric here is *coupling reduction* and the
*existence of a swappable platform/engine boundary*, not lines of code.

1. **Decoupling is additive by nature.** The goal was to insert a *seam* between platform and
   engine. A call that was direct (`deck.is_playing()`) now crosses a boundary
   (`engine.play_leds().playing` — a wrapper method + a POD struct + a forwarding body). You don't
   remove logic by adding an interface; you relocate it and pay for the indirection.
2. **Behavior-preservation forbade simplification.** The prime directive was byte-identical behavior
   under hardware-only verification, so every round did *faithful* moves (leaf substitution, handler
   migration, verbatim ports). The intricate logic — the apply-pass switch, the `MValue` overlays,
   the size-overlay geometry, the priority chain — was moved unchanged, never consolidated. Code
   shrinks when you collapse logic; that was deliberately off-limits in exchange for safety.
3. **The payoff is amortized over engines that don't exist yet.** Abstraction pays off as *reuse*.
   With N=1 engine, the full abstraction cost is paid and zero reuse benefit collected. The reduction
   never appears as a smaller `GranularEngine`; it appears later as *engine #2 being tiny* because it
   inherits the whole platform for free.
4. **We are mid-transformation, carrying scaffolding.** The query-substitution approach used for the
   LED rounds — engine *exposes state* (`*_leds`, `render_ring`), platform *still interprets* it into
   colors/blink — is a deliberate half-step. It adds wrappers **without** removing the platform's
   interpretation (`grit_color`, blink timers, the palette). The end-state — engine fills a
   `DisplayModel` directly, platform just blits — would let us *delete* both the query wrappers and
   the platform color/blink code (a real reduction), and it is deferred to the 2nd-engine work (item 3
   above). So part of the current size is transitional, not permanent.

The genuine reductions that did happen (dead `_middle_pitch_a/b`; the `_speed_map`/`_speed_mult`
consolidation; `Deck*` → byte-port in `Storage`) were real but small, and outweighed by the boundary
surface. A refactor aimed at net *reduction* would instead attack the logic itself (collapse the
apply-pass switch, generalize the overlays into a table) — which can genuinely shrink the codebase but
is a blind rewrite of the most intricate, hardware-only-verified code in the system. We traded size
for regression safety; that is a real trade, not a free one. To get hard numbers rather than the SRAM
proxy, run `git diff --stat` across the refactor commit range for added-vs-removed per round.
