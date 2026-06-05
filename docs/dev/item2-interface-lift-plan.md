# Item 2 — lift the shared `IEngine` interface (plan)

Resume plan for roadmap item 2 in `docs/dev/refactor-status.md`. Decisions locked 2026-06-02: **Strategy A** (virtual no-op defaults), **display pulled into item 2**, **`Driver` relocation deferred**.

## Full concrete-coupling inventory (everyone reaching past `IEngine` into `GranularEngine`/`Core`)

| Holder | Reaches concrete for | Cluster |
|--------|---------------------|---------|
| `CoreUI._engine` (`GranularEngine&`) | `set_param`/`param`/`set_mod_speed`, `handle_midi_*`, `set_fx`/`toggle_fx_lock`/`on_*_pad`/`on_seq_*`/`clear_*`/`disarm_track`, `cv_*`, `on_gate_*`, the 10 `transport_*` | 1. Input control |
| `CoreUI._core` (`Core&`, bound to `engine.core()`) | Category 2 (switch-config writes) + Category 3 (deck-state readbacks) | residual hatch |
| `CoreUI._engine` | `fx_leds`/`play_leds`/`alt_leds`/`transport_leds`/`deck_leds`/`mix`/`route`/`render_ring` | 2. Output display (LEDs) |
| `Storage._engine` (`GranularEngine*`) | `audio_is_empty`/`audio_data`/`audio_recorded_bytes`/`audio_capacity_bytes`/`audio_apply_loaded` | 3. Storage port |
| `app.cpp` (`GranularEngine _engine`) + `mod_src` DAC callback (`app.cpp:45-46`) | `_engine.core().mod(deck).process(out)` — the two modulation/LFO CV outputs | 4. Output CV |

The `app.cpp` DAC `mod()` call (cluster 4) was previously uncatalogued; a 2nd engine needs its two CV outs too, so it must be lifted (the strawman's `processCV`).

## Central tension

The concrete surface is three kinds of method; treating it as one "make it all virtual" move is the trap:

1. **Universal** — every engine has them: `init`/`prepare`/`process` (done), `capabilities()`, and the two outputs `render(DisplayModel&)` + `process_cv(...)`. Belong on `IEngine`.

2. **Capability-gated input events** — MIDI, pads, seq, cv-in, gate, storage port, transport. A passthrough declares only `CapTransport` and implements none of the rest.

3. **Transitional scaffolding** — `*_leds`/`render_ring` are documented as retiring once the engine fills `DisplayModel` via `render()`. Lifting them to `IEngine` ossifies code item 3 deletes.

Consequence: "`CoreUI` holds a pure `IEngine&`" is unachievable without also migrating LEDs to `render(DisplayModel)` (item-3-flagged display work) — hence display is pulled into item 2. Even then, Categories 2-3 keep a narrow concrete handle until the item-3 toolkit.

## End-state shape (Strategy A)

- `IEngine` grows to the full contract: `init`/`prepare`/`process` stay pure; everything else gets a `{}` no-op default so an engine overrides only what it supports. `capabilities()` advertises live regions.

- `GranularEngine`'s ~35 methods become `override`s.

- `CoreUI` holds `IEngine& _engine` for input/CV/storage/display + a narrow residual concrete `GranularEngine& _gran` for Categories 2-3 only.

- Transport stays capability-gated virtuals; `GranularEngine` forwards to `Core`'s `Driver` (no relocation).

## Sub-rounds (each behavior-preserving, build + host + flash-verified)

**2a — Grow `IEngine` to the contract (additive; no call-site or type changes).**

- Add virtuals with no-op defaults for every cluster-1/2/3/4 method + `render(DisplayModel&)` + `process_cv(...)`. Mark `GranularEngine` methods `override`.

- Include-surface decision: `iengine.h` needs `ParamId`/`FxKind`/`Capabilities` (engine_params.h), `DisplayModel` (display_model.h), `Route`, and `Deck::Ref`. `Deck::Ref` drags in `core/deck.h` — confirm whether it can be hoisted to a light header / forward-declared, else accept the include.

- Verify: builds, host passes, firmware behavior-identical (only a vtable added). SRAM watch starts.

**2b — Lift the LED queries (PIVOTED 2026-06-02; DONE, build+host-verified, flash pending).** Reading `_draw_ring` in full revoked the original "migrate to `render(DisplayModel&)`" plan: the LED code is ~90% irreducible platform interaction-grammar (`MValue` value-displays, storage/tape display, `_touched` gating, blink timers, the color palette). The engine queries (`*_leds`/`render_ring`) are NOT throwaway scaffolding — they are the legitimate engine->platform state channel. `render(DisplayModel&)` would require either inverting the architecture (dragging platform state into the engine) or doing the item-3 `MValue`->`ParamId` toolkit now (large/blind/byte-faithful, under ~640 B SRAM) — and it does NOT enable a 2nd engine's display until that toolkit exists anyway. So:

- Lifted the 8 LED query methods (`fx_leds`/`play_leds`/`alt_leds`/`transport_leds`/`deck_leds`/`mix`/ `route`/`render_ring`) onto `IEngine` (virtual + inert defaults, Strategy A) — `core.ui.leds.cpp` works unchanged through `IEngine&`. NO `_draw_ring` rewrite.

- Moved the 6 POD structs to `engine/engine_leds.h`; moved `Route` from `core.h` to `core/mode.h` so the contract depends on a light header, not the whole DSP graph.

- Deferred BOTH `render(DisplayModel&)` AND the `MValue`->`ParamId` toolkit to item 3, unified (that is the real display-abstraction round and the only thing that lets a non-granular engine draw itself).

**2c — CV-out lift (DONE, flash-verified 2026-06-02; CV outputs confirmed).**

- Added `IEngine::process_cv(cv0, cv1, n)` (block-rate, default silence) + `GranularEngine` override (faithful to the old per-sample loop). `DACCallback` calls `impl.process_cv(out, size)` once per block, fills a `kDacBufSize`(48) float scratch on the ISR stack, converts to 12-bit, and caches the block's last sample via `set_lfo` (equivalent to the old per-sample at the 62 Hz LED read). No per-sample virtual on the ISR. `app.cpp` is now `core()`-free.

**2d — Flip the holder types (DONE, flash-verified 2026-06-02).**

- `CoreUI._engine`: `GranularEngine&` -> `IEngine&` (ctor still takes `GranularEngine&` to bind `Core& _core` for Cat 2-3 — that IS the residual hatch; no separate `_gran` needed). `Storage._engine`

  - `init` signatures: `GranularEngine*`/`&` -> `IEngine*`/`&`; storage.cpp includes `iengine.h` now. The only `.core()` call left is `core.ui.cpp:24`.

**2e — records (DONE).** `docs/dev/refactor-status.md`, this plan, and the project memory updated; the `core()` hatch comment in `granular_engine.h` already names Cat 2-3.

## Honest milestone / non-goals — ACHIEVED 2026-06-02

The platform drives the engine through `IEngine` for input, CV-out, storage, transport, and the LED *queries*. It does NOT yet wire a 2nd engine — Cat 2-3 + the generic value-display toolkit + `render(DisplayModel&)` remain (item 3, which then unblocks the 2nd engine). The LED display itself was NOT migrated to `render(DisplayModel&)` (2b pivot); the platform still renders via the lifted queries. `Driver` relocation is contained (forwards), not resolved; the 2nd engine's `CapTransport` forces it during 2nd-engine prep. Final SRAM_EXEC ~416 B free.

## Known risks

1. SRAM — Strategy A's vtable + lost inlining is net-additive vs ~1312 B free; `-Os` on `core.ui.midi.cpp` is the next lever. 2b retiring the query bodies may give some back.

2. 2b is a byte-faithful port of the most intricate hardware-only-verified UI code (the same caution that paused the original LED work). Stage it small.

3. 2c per-sample virtual trap on the DAC ISR — design it out, do not introduce it.

4. Host `test_engine_params.cpp` reaches `engine.core()` directly — fine to keep (host has the concrete type); add a case driving input through `IEngine&` to prove dispatch.

Realistically 5+ flash-verified commits; 2b alone matches the original multi-round LED migration.
