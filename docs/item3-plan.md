# Item 3(a) plan — MValue->ParamId toolkit + render(DisplayModel), remove core()

Written 2026-06-02 after reading the actual code (set_param dispatch, MValue, the 44-site
coupling map, DisplayModel, iengine). Companion to `docs/refactor-status.md` and
`docs/item2-interface-lift-plan.md`. **Scope is 3(a) only**; 3(b) (the 2nd engine + Driver
relocation) is sequenced after.

## Objective

Remove the last concrete coupling so `CoreUI` holds **only** `IEngine&` (no `GranularEngine&`
in the ctor, `engine.core()` deleted). Two coupled pieces, both currently blocked on the same
`_core` member bound at `core.ui.h:24` (`_core { engine.core() }`):

- **Param toolkit** — absorb Categories 2-3 (the 44 `_core.` sites in `core.ui.cpp`).
- **render(DisplayModel)** — retire the `*_leds`/`render_ring` query path (deferred from 2b).

## Coupling inventory (verified call-site map, 2026-06-02)

`core.ui.pads.cpp`, `core.ui.midi.cpp`, `core.ui.leds.cpp` are already `_core.`-free. **All 44
remaining sites are in `core.ui.cpp`**, in five functions:

| Site group | Lines | Category | What it is |
|---|---|---|---|
| `_init_values` seeding | 64-65, 80-85 | 3 | `deck.norm_start()` + `fx.grit_*`/`flux_*` -> initial MValue seed |
| apply pass mode cache | 117-120 | 3 | `deck.mode()==Drift` -> `is_chord_a/b` |
| apply pass SIZE fanout | 166, 178 | 3 | `deck.mode()==Slice && Alt` -> PolySlice vs Size |
| pot queue mode switch | 346-347, 374, 407, 451, 483 | 3 | `deck.mode()` -> which MValue(s) a pot tracks |
| pot queue is_empty gate | 380, 457 | 3 | `deck.is_empty()` gates the Slice tap-hold tempo-fit |
| `_process_switches` config | 541-597 | 2 | `set_route`, `mod().set_type/set_lfo_type`, `deck.set_mode`, `infer_panner_mode`, `set_start/size_mod_on` |
| grit-mode toggle+readback | 619-626 | 2 | `fx().switch_grit_mode()` + reseed `_grit_*` MValues |
| `_set_tempo_by_size` | 670 | 3 | `deck.tempo_to_fit(fraction)` -> BPM |

### What actually couples (the diagnosis)

The mode-dependent **leaf dispatch** is already inside the engine — `set_param` branches
Reel/Slice/Drift internally (Size->win_spread in Drift; Speed->pitch in Slice;
granular_engine.cpp:21-54). The platform's residual coupling is a *different* thing:

1. **Physical-control -> {ParamId set}, gated by mode + Alt.** One knob feeds different
   *combinations* of MValues per mode. E.g. CTRL_SIZE: Reel tracks `_size`; Slice tracks
   `_size`+`_poly_slice` (Alt picks which is "active") or `_size_quarters` (tap-hold, non-empty);
   Drift tracks `_size`+`_win`. CTRL_ENV: Drift tracks `_env`+`_env_size`, else just `_env`. The
   platform reads `deck.mode()` to know the fanout. **This is the coupling** — the engine, not the
   platform, should declare the fanout.
2. **Config enums (Category 2)** are written straight into Core and have **no ParamId channel** —
   `ParamId` is an all-float scalar set; route/mode/lfo-type/mod-flags are enums/topology.
3. **Engine-state queries** the platform branches on: `is_empty()` (tempo-fit gate),
   `tempo_to_fit()` (a computation), and the seed reads (`norm_start`, grit/flux).

## The central abstraction — ParamId-keyed MValues + engine-declared bindings

**MValue storage.** Replace the ~25 named per-deck MValue members (`_size[2]`, `_win[2]`,
`_poly_slice[2]`, `_env[2]`, `_env_size[2]`, `_grit_*`, `_flux_*`, ...) with an array indexed by
`ParamId`: `MValue _mv[ParamId::Count][Deck::Count]`, plus the globals that have a ParamId
(`_tempo`->Tempo, `_key_interval`->KeyInterval, `_click_mix`->ClickMix, `_pan_*`->PanSpeed/Range).
This is what "toolkit keyed by ParamId" means and is the mechanical core of the round.

**Binding query. DECIDED (direction): the query method, NOT a static table. Exact signature
locked at 3a-3, not now.** The engine declares, per physical control + deck + modifier state,
which ParamId(s) the control drives and which is "active" (the Alt gate):

```
struct Binding { ParamId id; bool active; };          // up to ~2 per control
// engine fills caller-owned span; returns count. No allocation, no Core leak.
virtual uint8_t bindings(ControlId, Deck::Ref, Modifiers, Binding* out) const;
```

- **Pot queue** (`_process_ui_queue`): for each binding, `_mv[id][deck].process(val, b.active, id)`.
- **Apply pass** (`process`): for each `_apply.test(ctrl)` binding,
  `engine.set_param(b.id, deck, _mv[b.id][deck].value())`.

Both lose their `deck.mode()` reads — the engine's `bindings()` encodes the mode fanout
internally. `ControlId`/`Modifiers` are platform enums passed *to* the engine (the engine maps
them to its meaning), so they don't re-leak Core.

**Why not the static const table.** A table the platform indexes by `(control, mode, modifier)`
forces the platform to know the *mode axis* — i.e. it re-reads `deck.mode()`, the exact coupling
this round removes. Only the query keeps mode inside the engine. The objection to the query (a
virtual call on the apply path) is a non-issue: `set_param` is **already** virtual and called right
there; the apply pass runs at block rate (~500 Hz) over a handful of touched controls, each call
returning ~2 bindings — noise next to the audio DSP. The defer (to 3a-3) is about confirming the
exact signature against real code, **not** about whether to keep mode in the engine (it stays).

This keeps MValue pickup mechanics (`process`, threshold, `_is_tracking`, `id`) **on the
platform** — only the binding decision moves to the engine, matching the doc's rule that the
interaction grammar is platform-owned.

### Hard cases that resist the pure table (must be designed, not hand-waved)

1. **Config enums (Category 2). DECIDED: separate `set_config(ConfigId,int)` + readback
   `config(ConfigId)`.** Not floats; no ParamId. Rejected extending ParamId with float-encoded
   enums: the MValue apparatus (pickup/threshold/value-display) is meaningless for a categorical,
   so every consumer would special-case "is this id an enum," defeating the uniform param API.
   ConfigIds: Route/ModType/LfoType/StartMod/SizeMod/GritMode. The platform reads the switch
   shift-registers (hardware, stays platform) and calls `set_config`. The **readback earns its
   place**: `_init_values` seeding and the grit-mode reseed (619-626) must read engine config/param
   state back — without `config()` they would reintroduce a `core()` read.
2. **`_size_quarters` + tempo-fit (Slice tap-hold).** Not a ParamId — a platform tempo gesture
   conditioned on engine state. Express as a binding to a pseudo-id (`TempoFit`) the platform
   recognizes, or a narrow `engine.wants_tempo_fit(deck)` query replacing the `is_empty()` read.
   `tempo_to_fit()` (line 670) becomes `engine.tempo_to_fit(deck, fraction)` on IEngine.
3. **grit-mode readback-after-write (619-626).** `switch_grit_mode()` mutates engine state and the
   platform reseeds `_grit_*` MValues from the new state. After migration: `set_config(GritMode)`
   then reseed via `_mv[GritMix/GritIntensity][deck].set(engine.param(...))`. Already expressible
   through `param()`.
4. **Initial seeding (`_init_values`).** Most defaults are platform literals (size=1, speed=.5,
   ...). Only `norm_start` + grit/flux are read from the engine. After migration: seed every
   `_mv[id][deck]` from `engine.param(id, deck)` — but `param()` returns the cache (0 until first
   set). Need the engine to expose **defaults** (`param()` pre-seeded at init, or a
   `param_default(id)`), else the seed reads change. Decide: pre-seed `_param_cache` in
   `GranularEngine::init` so `param()` is authoritative from boot.

## render(DisplayModel) sub-round — the LED half

The 2b analysis found `_draw_ring` is ~90% platform interaction-grammar (MValue value-displays,
storage/tape, `_touched`, blink, palette). So this is **not** "engine draws, platform blits" —
it is: **engine fills the steady-state base** (rings + base indicators) into `DisplayModel`;
**platform composites its transient overlays on top** (the MValue value-display, size arc,
overdub head, blink, storage/tape). `render_ring` already returns `RingGeometry` for exactly this
overlay compositing, so the split already exists — this round moves the steady-state + indicator
base into `engine.render(DisplayModel&)` and deletes the `*_leds`/`render_ring`/`mix`/`route`
query methods.

**SRAM reality check.** The reduction here is **weaker than the doc's optimistic framing**.
Deleting the query bodies reclaims some, but `render(DisplayModel)` re-adds equivalent fill code,
and the platform's overlay/palette/blink code (the ~90%) stays. Expect **near-net-neutral**, not
strongly negative. This must be measured, not assumed (the `-Os` lever just taught us that —
it reclaimed 16 B, not the hoped-for headroom).

## Recommended sub-round sequence (each behavior-preserving, flash-verified)

The two pieces are separable; sequence to fund SRAM and de-risk:

- **3a-0 — config channel (Category 2 first). DONE + FLASH-VERIFIED 2026-06-02.** Added `set_config(ConfigId, Deck::Ref, int)->bool` (returns changed; only Mode uses
  it, to re-apply size), `tempo_to_fit(deck, fraction)`, and `toggle_grit_mode(deck)->GritReseed`
  to IEngine; `GranularEngine` overrides them (set_config is `optimize("Os")`-tagged in the -O2 TU).
  `_process_switches` is now fully `_core`-free (route/mod-type/lfo-shape/mode/start-size-mod/grit
  all go through the engine; the platform passes raw switch-bit selectors and the engine owns the
  enum mapping + panner inference + the per-deck LFO palette). `_set_tempo_by_size` routes through
  `engine.tempo_to_fit`. **Deviations from the original plan:** (1) did NOT add the `config()`
  readback — 3a-0 doesn't use it (grit reseed comes via `toggle_grit_mode`'s return; seeding
  migration is 3a-2/3a-3), so it would be dead vtable surface on tight SRAM; add it when first
  needed. (2) did NOT add `wants_tempo_fit` / touch the `is_empty()` gate at core.ui.cpp:380/457 —
  that lives in the pot-queue mode dispatch and belongs with 3a-3 (avoids entangling 3a-0 with the
  mode-fanout code). **SRAM: +336 B (free 440 -> 104 B).** Expected additive seam cost; reclaimed by
  3a-2. The 5 residual `_core.` sites (seeding :64, apply pass :117-118, pot queue :346-347) are
  exactly the ones deferred to 3a-2/3a-3.
- **3a-1 — render(DisplayModel).** Do the LED half next: it's independent of the param toolkit and
  is the SRAM-funding step (measure it). Deletes the query methods.
- **3a-2 — ParamId-keyed MValue array. DONE + FLASH-VERIFIED 2026-06-02.** Replaced the ~21 named members with
  `std::array<std::array<MValue, Deck::Count>, ParamId::Count> _mv` + an inline `mv(ParamId)`
  accessor; `_size_quarters` kept named (not a ParamId). Mechanically renamed 152 sites
  (`_X[ref]` -> `mv(ParamId::X)[ref]`; globals -> `mv(ParamId::X)[Deck::A]`) across core.ui.cpp (109)
  and core.ui.leds.cpp (43). Behavior-identical by construction (pure storage rename, no logic/engine
  change; keeps the existing `deck.mode()` reads). **SRAM: reclaimed 296 B (free 104 -> 400 B)** -
  ~24 B less than the raw-cast spike's 320 B, the cost of the cleaner accessor/std::array form.
  Net of 3a-0+3a-2 over the pre-3a-0 baseline: **+40 B** (the seam + array refactor are ~free).
  **SPIKE DONE (2026-06-02): the array form RECLAIMS ~320 B** — the opposite of the feared
  regression. Throwaway spike collapsed the ~21 named per-deck/global MValue members into
  `MValue _mv[ParamId::Count][Deck::Count]` (globals in slot [0], `_size_quarters` kept named),
  mechanically renamed all 178 call sites in core.ui.cpp/leds.cpp via word-boundaried perl, built
  clean. SRAM_EXEC 190016 -> 189696 B used (free 448 -> **768 B**). Collapsing 21 separate
  `std::array<MValue,2>` members into one 2D array with compile-time-constant indices generates
  *less* code (fewer distinct member-address computations / std::array operator[] wrappers). So
  **3a-2 is net-negative on SRAM and the sequence does not stall on headroom** — it funds itself
  (~320 B) before 3a-1's render migration even runs. Spike reverted; tree clean.
- **3a-3 — engine knob-layout query. DONE 2026-06-02 (builds clean; flash-verify pending).**
  **Signature changed from the `bindings()->{ParamId,active}` sketch after reading the real code**
  (as the plan said to). Why: the pot-queue's `active` flags are platform modifiers (`_touched`,
  `_tap_hold`, `_storage`) the engine can't compute, and the SIZE fanout includes non-`ParamId`
  gestures (`_size_quarters`, `_set_tempo_by_size`). A `{ParamId,active}` list can't capture that
  without dragging the whole interaction grammar into the engine - the exact trap the docs warn of.
  Instead used the **faithful-query-substitution** pattern item 2 used for LEDs: the engine answers
  the few semantic questions the platform branches on. Added `DeckLayout{single,slice,chord,none}`
  (engine-declared knob layout; granular maps Reel/Slice/Drift/None; modeless engine -> single) +
  `deck_layout(Deck::Ref)` and `size_sets_tempo(Deck::Ref)` (folds the Slice `is_empty` gate) to
  IEngine. Both non-const (forward to non-const Core, like `transport_*`). Rewrote the apply pass
  (`is_chord`/Slice checks) and the pot-queue CTRL_SIZE/CTRL_ENV branches to switch on `DeckLayout`
  instead of `deck.mode()`; layouts hoisted once per function (mode is constant within a call).
  **core.ui.cpp now has ONE residual `_core.` site: `_init_values` seeding (:64).** SRAM +168 B
  (free 400 -> 232 B). Faithful behavior-preserving port (each mode branch maps 1:1, incl. None->none).
- **3a-3b — seeding migration. DONE 2026-06-02 (builds clean; flash-verify pending).** Resolved the
  seed-authority question with the **pre-seed approach, scoped**: `GranularEngine::init` now pre-seeds
  `_param_cache` for the 6 engine-derived seeds (Pos<-norm_start, GritMix/GritIntensity/FluxMix/
  FluxIntensity/FluxFb<-fx), and `_init_values` reads those via `engine.param(...)`. The platform
  keeps its UI-default literals for the rest (Size/Speed/Mix/... and the globals Tempo/Pan/etc.) -
  moving those into the engine is a deeper change not needed to delete core(), deferred. `init` moved
  to the .cpp tagged `optimize("Os")` (the inline header version overflowed SRAM_EXEC by 40 B). After
  this, **core.ui.cpp is fully `_core`-free**. SRAM net +96 B (free 232 -> 136 B).
- **3a-4 — delete core() + the GranularEngine& ctor. DONE 2026-06-02 (builds clean; flash-verify
  pending).** `CoreUI`'s ctor takes `IEngine&` (was `GranularEngine&`); the `Core& _core` member +
  its `engine.core()` binding are gone; `GranularEngine::core()` is deleted. Verified: no `.core()` or
  `_core` anywhere in src/ui, src/memory, or app.cpp. **The platform is fully decoupled from Core; it
  drives the engine only through IEngine.** SRAM net-zero on SRAM_EXEC (the removed reference member
  is regular SRAM; the inline core() emitted no code). **Item 3(a) core goal COMPLETE.**

After 3a-4: `engine.core()` and `core.ui.h:24`'s `_core` member are gone; `CoreUI` is engine-agnostic.

## SRAM (binding constraint — ~440 B free)

TU-level `-Os` is spent (midi.cpp gave 16 B). **Headroom worry is RESOLVED for 3a-2:** the spike
(above) showed the MValue-array collapse reclaims ~320 B (free 448 -> 768 B), so the riskiest
sub-round funds itself rather than stalling on the ceiling. 3a-1 (render) is still expected only
near-neutral (the ~90% platform overlay grammar stays), so its value is coupling-reduction, not
bytes. Continue to measure SRAM_EXEC after every sub-round, but the binding constraint is no longer
the gating risk it was at the start of item 3.

## Decisions (locked 2026-06-02)

1. **Config enums — DECIDED:** separate `set_config(ConfigId,int)` + readback `config(ConfigId)`.
   Not extend ParamId. (See "Hard cases" #1.)
2. **bindings() mechanism — DECIDED (direction):** the query method (mode stays in the engine), not
   a static table (which re-couples). Exact signature locked at 3a-3 against real code. (See "Why
   not the static const table.")
3. **3a-2 — SPIKE FIRST:** measure SRAM_EXEC on a throwaway spike before committing the sequence.

### Sub-round order — RESOLVED by the 3a-0 SRAM result

3a-0 left only **104 B free**. Doing 3a-1 (render, ~neutral) next risks overflow, so the order is
now **3a-0 (done) -> 3a-2 (the ~320 B reclaim) -> 3a-1 (render) -> 3a-3 (bindings) -> 3a-4 (delete
core())**. Pull the MValue-array reclaim forward to restore headroom before adding any more surface.
3a-2 is safe to do now: it's a pure platform refactor (named members -> `_mv[ParamId][2]`), needs
no engine change, and the spike already proved it builds and reclaims.

### Still open

- Whether to pre-seed `GranularEngine::_param_cache` at init so `param()` is the seed authority
  for `_init_values` (vs. a separate `param_default(id)`). Resolve when writing 3a-2/3a-3.
