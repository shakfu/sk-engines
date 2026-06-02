# Platform / engine refactor — status & resume guide

## Resuming — start here (paused 2026-06-02)

**Done:** pre-work (SRAM #2/#5), item 1 (transport off `core()`), item 2 (IEngine interface lifted,
sub-rounds 2a-2d). All flash-verified.

**Commit state (verified 2026-06-02):** all rounds through item 2 are committed and pushed to
`origin/main`; the working tree is clean. Item 2's surface landed across `732e4b3` (`granular_engine.h`
+ UI edits) and `8b1b435` (`iengine.h` interface lift, the `IEngine&`/`IEngine*` flip in
`CoreUI`/`Storage`, the `Route` move `core.h`->`mode.h`, and the 2 new files
`src/engine/engine_leds.h` + `docs/item2-interface-lift-plan.md`). (The earlier "item 2 lives only in
the working tree" warning is resolved. Note `8b1b435`'s message — "pause till later" — is vague for
what is actually the bulk of the interface lift, so bisecting this range by message won't help.)

**Item 3(a) CORE GOAL COMPLETE (2026-06-02, builds clean; flash-verify pending): `engine.core()` is
DELETED.** Rounds 3a-0 (config channel), 3a-2 (MValue->ParamId array), 3a-3 (DeckLayout query
replacing the apply-pass/pot-queue mode reads), 3a-3b (seeding via pre-seeded `param()`), 3a-4 (delete
`core()`, ctor now `IEngine&`). The platform no longer touches `Core` — grep finds no `.core()`/`_core`
in src/ui, src/memory, app.cpp. Full per-round detail in `docs/item3-plan.md`. **Remaining:** 3a-1
`render(DisplayModel)` (deferred, OFF the critical path — LED queries already route through IEngine);
then item 3(b) (promote `PassthroughEngine` to a real 2nd variant + the engine-select mechanism in
`docs/architecture.md`). The "Still coupled / Categories 2-3" section below is now historical.

**Historical resume notes (item 2 era, kept for context):**
1. Item 2 is committed (verified 2026-06-02, see commit-state note above) — no action needed.
1. Item 2 is committed (verified 2026-06-02, see commit-state note above) — no action needed.
2. **SRAM lever is spent (verified 2026-06-02).** `-Os` was applied to `core.ui.midi.cpp` and
   reclaimed only **16 B** (424 -> 440 B free) — MIDI parsing isn't a meaningful size contributor, so
   the doc's "remaining lever" is effectively exhausted. **TU-level `-Os` sweeps are no longer a viable
   funding source.** Item 3 must instead be **net-neutral-or-negative on SRAM by deleting as it adds** —
   the real headroom is the intrinsic reduction from removing the `*_leds`/`render_ring` query wrapper
   bodies + the platform's color/blink interpretation once the engine fills `DisplayModel` directly.
3. Scope item 3(a) (`render(DisplayModel)` + `MValue`->`ParamId` toolkit, which absorbs `core()`
   Categories 2-3) vs 3(b) (promote `PassthroughEngine`, forces `Driver` relocation) — decide order.

## Goal

Turn this firmware into a **fixed hardware/UI platform** that hosts a swappable DSP
**engine**, so future firmware variants reuse the board + the whole interaction language
(multi-function knobs, pickup, ring feedback, pad gestures, transport, storage) and only swap
the parameters and DSP. (Organelle/Axoloti/plugin-host model on fixed hardware.) The granular
looper is the first/reference engine.

## Where it stands — IEngine interface lifted (item 2 complete)

The platform now drives the engine **through the abstract `IEngine` interface** for input
(params/MIDI/pads/CV/gate), storage, transport, CV-out, and the LED queries. `CoreUI` holds an
`IEngine&` and `Storage` an `IEngine*`. The audio lifecycle stays pure-virtual; the rest of the
platform-driven surface was lifted onto `IEngine` with no-op default bodies (Strategy A), so an
engine overrides only what it supports and `capabilities()` advertises which regions are live.
Item 1 moved transport off the `core()` hatch; item 2 (sub-rounds 2a-2d, all flash-verified
2026-06-02) lifted the whole surface. **The only residual concrete coupling left is Categories 2-3**
(switch-config writes + deck-state readbacks): `CoreUI`'s ctor still takes a `GranularEngine&` to
extract `Core&` for them, and `app.cpp` holds the concrete engine instance. Those await the item-3
toolkit (see "Still coupled" + roadmap). Everything builds, flashes, and runs; every step was
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

### Still coupled — the `engine.core()` escape hatch [RESOLVED 2026-06-02 — section now historical]

> **UPDATE: `engine.core()` is DELETED (item 3a, builds clean, flash-verify pending).** Categories 2-3
> below were absorbed by the item-3 rounds: Category 2 by the `set_config`/`tempo_to_fit`/
> `toggle_grit_mode` config channel (3a-0); Category 3 by the `DeckLayout`/`size_sets_tempo` queries
> (3a-3) and the pre-seeded `param()` seeding (3a-3b). The text below describes the pre-item-3 state.

A call-site audit (2026-06-02) found the hatch carried three coupling categories, not the "transport
actions only" earlier claimed. **Category 1 (transport) is migrated (item 1); items 2a-2d lifted the
entire rest of the platform-driven surface onto `IEngine`.** The only thing still reaching `Core`
through the concrete `GranularEngine` is Categories 2-3 below: `CoreUI`'s ctor takes a `GranularEngine&`
to bind `Core& _core` (`core.ui.cpp:24`), and that handle serves both. Absorbing them is item 3.

- **Category 1 — transport / `Driver` — DONE (item 1, flash-verified 2026-06-02).** The 10 `Driver`
  methods the platform used are `transport_*` forwards (now virtual on `IEngine`, item 2a); the 8 call
  sites route through `_engine.transport_*`. Faithful stopgap (B1): the platform still owns clock-source
  selection + edge detection in `tick()`. `Driver` still lives in `Core`; relocating it to a platform
  transport service was **deferred past item 2** (kept contained as forwards) and will likely be forced
  by the 2nd engine's `CapTransport`. (For the record, the pre-migration "3 methods
  `tick`/`toggle_play`/`reset`" estimate was wrong: it was 10, and `toggle_play` is engine-internal.)
- **Category 2 — switch-config writes** (`_process_switches`, `core.ui.cpp:541-626`), NOT transport:
  `set_route(...)`, `mod(ref).set_type()/set_lfo_type()`, `deck.set_mode()` + `infer_panner_mode()`,
  `deck.set_start_mod_on()/set_size_mod_on()`, `deck.fx().switch_grit_mode()` + grit readback. These
  are enum/topology configs the scalar `set_param(ParamId, …)` API never absorbed.
- **Category 3 — deck-state readbacks** (apply pass + pot queue), NOT transport: `deck.mode()` for
  `is_chord`/`Slice`/`Drift` dispatch (`core.ui.cpp:119-120,166,178,374,407,451,483`), `deck.is_empty()`
  (`380,457`), `deck.norm_start()`/`deck.fx().grit_*`/`flux_*` for initial `MValue` seeding (`64-85`),
  `deck.tempo_to_fit()` (`671`).

Categories 2-3 are exactly what roadmap item 3's `MValue` → `ParamId` toolkit is meant to generalize
(engine declares its modes/configs; platform stops reading `deck.mode()`), so absorbing them now as
bespoke wrappers would build throwaway scaffolding — the same trap noted for the UI A/B consolidation.
They stay on `core()` until then; once they migrate, `core()` can be removed.

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
region, currently **99.78% full (190048 B used, ~416 B free)** as of 2026-06-02. Trajectory:
~912 B baseline -> +~448 B (#2 init table) -> +~56 B (#5 storage dedup) = ~1416 B; then item 1
spent ~104 B (transport forwards) = ~1312 B; then item 2's Strategy-A virtualisation spent it down
across 2a (~592 B: vtable + ~35 methods virtual, also un-inlining item 1's transport forwards), 2b
(~80 B: 8 LED query virtuals), 2c (~168 B: `process_cv` + body), 2d (~56 B: `IEngine&` flip makes all
UI->engine calls genuinely virtual) = **~416 B free**. The Strategy-A vtable cost is the true
end-state cost (no devirtualization once `CoreUI` holds `IEngine&`), not recoverable. ITCMRAM
relocation stays rejected (under BOOT_SRAM the single-blob load can't cleanly leave SRAM_EXEC). The
three non-RT UI TUs `core.ui.leds.cpp`/`core.ui.pads.cpp`/`core.ui.cpp` are `-Os`; the audio DSP stays
`-O2 -funroll-loops`. **Item 3 will need headroom from the start** — the remaining lever is `-Os` on
`core.ui.midi.cpp`, then other non-RT TUs; item 3 also *deletes* the `*_leds`/`render_ring` query
bodies when it moves LEDs to `render(DisplayModel)`, which should give some back.

## Resume roadmap — remaining tasks

The input side, CV/gate/storage, and all LED rendering are decoupled. What remains is the
platform-ization tail. Each item is its own behavior-preserving, flash-verified round.

1. **Transport off `core()` — Category 1 only — DONE (flash-verified 2026-06-02).** Absorbed the
   transport/`Driver` category (the 10 methods under "Still coupled" Category 1) behind inline
   `transport_*` forwards on `GranularEngine`, repointing the 8 call sites in `core.ui.cpp`/`pads`/
   `midi` from `_core.driver()`; `core.ui.midi.cpp` is now fully `_core`-free. Faithful stopgap (A2 +
   B1): wrapped methods (matching the `*_leds` idiom), and the platform kept clock-source selection +
   edge detection in `tick()`. As planned, this did **not** remove `engine.core()` — Categories 2-3
   still hold it. The architectural fork (keep `Driver` in `Core` vs. move it to a platform transport
   service) was **deferred to item 2** per the stopgap recommendation; `Driver` still lives in `Core`.
2. **Lift the shared `IEngine` interface — DONE (sub-rounds 2a-2d, flash-verified 2026-06-02).**
   Strategy A: the platform-driven surface (`set_param`/`param`, `handle_midi_*`, `set_fx`/`on_*_pad`,
   `cv_*`/`on_gate_*`, `audio_*`, the `*_leds`/`render_ring` queries, `transport_*`, `process_cv`) is
   now declared on `IEngine` with no-op default bodies; `GranularEngine` overrides them; `CoreUI` holds
   `IEngine&` and `Storage` an `IEngine*`. **2b pivot:** the planned `render(DisplayModel&)` migration
   was dropped after reading `_draw_ring` — the LED code is ~90% irreducible platform interaction-
   grammar (`MValue` value-displays, storage/tape, `_touched`, blink, palette), so the cheap path was
   to lift the 8 LED *queries* to `IEngine` (no `_draw_ring` rewrite); `render(DisplayModel&)` + the
   `MValue`->`ParamId` toolkit defer to item 3, unified. **2c:** `process_cv` (DAC mod outputs) lifted
   block-rate (no per-sample virtual on the ISR). Side moves: `Route` `core.h`->`core/mode.h`; the LED
   POD structs -> `engine/engine_leds.h`. Deferred (as planned): `Driver` relocation (still forwards),
   and Categories 2-3, which keep the residual `GranularEngine&`-in-ctor hatch.
3. **2nd engine (Phase 4) — now also owns the unified display round (NEXT).** Two coupled pieces:
   (a) the **`MValue` -> engine-agnostic toolkit keyed by `ParamId`** + **`render(DisplayModel&)`**
   migration (deferred from 2b) — this is what lets a non-granular engine draw its own display and
   absorbs `engine.core()` Categories 2-3, after which `core()` and the ctor's `GranularEngine&` can
   go; (b) promoting the `PassthroughEngine` sketch (or a simple delay) to a real firmware variant,
   `Capabilities = {Transport}` only, which forces the `Driver` relocation deferred from items 1-2 and
   flushes remaining hidden coupling. Watch SRAM from the start (~416 B free; see budget).
4. **Build/boundary enforcement + DSP libraries (Phase 5).** Split into compile units / static libs
   (`libgranular`/`libtransport`/`libfx`/`libseq`), drop the blanket `-Isrc/`, give platform and
   engine separate include roots. *Optional:* template `Buffer` on sample format to retire the
   `LOFI_INT16` switch; make the remaining `config.h` sample-rate constants functions of `sample_rate`.

**Standing watch-item — SRAM_EXEC.** **~416 B free** (2026-06-02, after item 2's Strategy-A
virtualisation). This is now the binding constraint: item 3 must reclaim before it adds. Next levers:
`-Os` on `core.ui.midi.cpp` (and other non-RT TUs), plus item 3 *deleting* the `*_leds`/`render_ring`
query bodies as it moves to `render(DisplayModel)`. ITCMRAM stays rejected (BOOT_SRAM single-blob load).

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
