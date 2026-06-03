# Phase 5 plan — enforced platform/engine build boundaries

Goal: make the platform/engine separation **structural and compiler-enforced**, not just
conventional. End state: the contract headers are self-contained (no transitive `core/` includes),
the granular DSP is a separable unit under `src/engine/granular/`, and the platform physically
*cannot* include granular code (separate include roots, no blanket `-Isrc/`). Companion to
`docs/engine-layout.md` (the target layout) and `docs/architecture.md`.

Scope chosen 2026-06-03: **full (5a-5d)**. `EngineBuffers` *shape* generalization stays deferred to
the first real second engine (chicken-and-egg: doing it well needs a non-granular consumer to
validate against); Phase 5 only strips the granular *type dependency* from the contract.

## Coupling inventory (measured 2026-06-03)

The contract/platform still names `core/` types — these are what Phase 5 must relocate:

| Type | ~uses | Source header | Leaks via |
|---|---|---|---|
| `Deck::Ref` / `Deck::A/B/Count` | ~375 | `core/deck.h` | every `IEngine` method arg; `storage.h`/`core.ui.h` include `deck.h`/`core.h` |
| `Mode`, `Modulator::Type` | ~8 | `core/mode.h`, `modulator.h` | the `engine_leds.h` query structs (`DeckLeds.mode`, `.mod_type`) |
| `Route` | 3 | `core/mode.h` | `IEngine::route()` return |
| `Driver::Source` | 6 | `core/driver.h` | `IEngine::transport_source()` return |
| `Buffer::Frame`, `Event` | few | `core/buffer.h`, `track.h` | `EngineBuffers` fields; `storage.h` `sizeof` |

Platform include lines into `core/`: `app.cpp` (`core.h`,`itimesource.h`), `core.ui.h` (`core.h`,
`lutsinosc.h`), `storage.h` (`deck.h`). The contract leaks: `iengine.h`->`driver.h`,
`engine_context.h`->`deck.h`, `display_model.h`->`ui/led.ring.h`.

## The decision this hinges on: `Deck::Ref` -> a contract type

`Deck::Ref` (the A/B channel selector) is ~375 sites and is the dominant cost. Recommended:
define **`enum class DeckRef : uint8_t { A, B, Count }`** in a small contract header
(`src/engine/deck_ref.h`, or fold into `engine_params.h`); global mechanical rename
`Deck::Ref`->`DeckRef`, `Deck::A/B/Count`->`DeckRef::A/B/Count` across platform + engine (perl, same
technique as the 3a-2 MValue rename). The granular `class Deck` keeps its name and takes `DeckRef`.
Keeping the `Deck::Ref` *spelling* was rejected: a contract `Deck` shim collides with the granular
`class Deck`, and renaming the granular class is more invasive than the enumerator rename. This is
one big behavior-preserving diff; verify by build (codegen identical — it's a type/spelling change).

## Rounds (execution order chosen to minimize re-touching platform files)

Do the **contract type cleanup before the file relocation**: once the platform stops including
`core/` for types, moving `src/core/` becomes granular-internal (platform untouched), so platform
files are touched once, not twice.

### Round 1 — clean the contract types (`5b`)

- **DeckRef extraction — DONE 2026-06-03 (all 4 targets build; flash-verify pending, very low risk).**
  Created `src/engine/deck_ref.h` (`struct DeckRef { enum Ref : uint8_t { A,B,Count,None=0xff }; }` —
  UNSCOPED to keep implicit-int for array indexing; my earlier `enum class` recommendation was
  corrected). Renamed `Deck::{Ref,A,B,Count,None}` -> `DeckRef::...` across ~563 sites / 21 files;
  `class Deck` keeps `using Ref = DeckRef::Ref`. Granular 190152 B (codegen-identical, was 190160);
  passthrough + `make -C host` + host test all green.
- **Rest of R1 — DONE 2026-06-03 (all 4 targets build; flash-verify pending, low risk).**
  - EngineBuffers type-strip: `void* source/track` + counts; `engine_context.h` now `core/`-free
    (consumer casts in `core.cpp`). Shape still granular (arena generalization deferred to engine #2).
  - `Mode`/`Route` -> `engine/mode.h` (contract); `core/mode.h` redirects there for granular.
  - `Modulator::Type`/`Fx::GritMode`/`Deck::Source` -> contract `ModType`/`GritMode`/`DeckSource` in
    `engine/mode.h`; granular classes alias them (`using Type = ModType;` etc.); `engine_leds.h`
    structs + the platform color code use the contract enums. Removing `core.h`'s duplicate `ModType`
    unified two identical enums.
  - **Contract headers now `core/`-free except the `Driver::Source` leak** (`iengine.h` +
    `engine_leds.h`), which is Round 2. Granular 190160 (codegen-neutral); passthrough/host green.
  **=> ROUND 1 COMPLETE.**
- Relocate the leaked enums so the contract owns them: `Mode`, `Route` (`core/mode.h`),
  `Modulator::Type`, `LFO::Type` — move the enums the `engine_leds.h` structs + `route()` expose
  into contract headers (granular code `using`-aliases them so its internals are unchanged), OR
  restructure the LED query structs to carry contract-owned enums. Pick per-enum.
- `EngineBuffers`: strip the granular *types* — replace `Buffer::Frame*`/`Event*`/`size_t*` fields
  with raw `void*`/`uint8_t*` + element counts, so `engine_context.h` no longer includes `deck.h`.
  The granular `init` casts back. **Shape stays granular (deferred); only the type dep is removed.**
- Outcome: `iengine.h`/`engine_context.h`/`engine_leds.h`/`display_model.h` self-contained except
  the `Driver::Source` leak (Round 2) and the `LEDRing` leak (handle here or Round 2).
- Verify: granular + passthrough build; host test; flash granular (behavior-preserving rename).

### Round 2 — RESCOPED to "R2-lite" + DONE 2026-06-03 (all 4 targets build; flash-verify pending)

**Reframed:** the full Driver-class relocation is NOT needed for the boundary goal (the only
contract->Driver leak is the `Driver::Source` enum; the platform drives transport via the
`transport_*` virtuals, never including `driver.h`). So R2-lite did just the boundary-relevant part;
the Driver-class move to a platform transport service is deferred to a transport-capable 2nd engine.

- `Driver::Source` -> contract `ClockSource` (`struct ClockSource { enum Source ... }`, unscoped -
  values are PPQN used as ints); `Driver` aliases it. Removed `core/driver.h` from `iengine.h` +
  `engine_leds.h` -> **`engine_leds.h` fully `core/`-free**.
- Lifted `LEDRing` + `Color` (`led.ring.{h,cpp}` + `color.{h,cpp}`) `src/ui/` -> `src/engine/`
  (they're contract components the engine draws into via render()); `display_model.h` now includes
  `engine/led.ring.h`, no `ui/` dep. Makefile: added them as explicit shared `CPP_SOURCES`; host
  Makefile drops its `UI_SRC` (the `src/engine/*.cpp` wildcard now covers them).
- **Contract is now type-clean AND ui-clean.** Only residual: `iengine.h` includes the (type-clean)
  `core/engine_context.h` by path - its physical move to `engine/` happens in R3. Granular 190048,
  passthrough 149988, host main + test green. **=> ROUND 2 COMPLETE.**

### Round 3 — relocate the granular DSP (`5a`) -- DONE 2026-06-03

- `git mv src/core` -> `src/engine/granular/`; the two contract headers (`engine_context.h`,
  `itimesource.h`) then moved up to `src/engine/` (the contract root). Repathed every external
  `"core/..."`/`"../core/..."` include -> `"engine/granular/..."` (and the two contract headers ->
  `"engine/..."`) across the tree (app/hw/ui/memory/engine/host) + both Makefiles. Two gotchas hit:
  (1) granular files reaching `src/` root via `../common.h`/`../nocopy.h` went one level deeper, so
  those became `../../`; (2) `iengine.h` had been getting `DeckRef` transitively via
  `engine_context.h` and now includes `engine/deck_ref.h` directly. Granular internals were already
  fully relative, so the bulk move needed no edits inside them.
- Result: `src/core` gone; `src/engine/` = contract headers at top + `granular/`, `delay/`,
  `passthrough/` subdirs. Git tracked it as 64 renames + 14 include edits, 0 untracked.
  SRAM/SDRAM byte-identical to pre-move (185928 B / 75%) => codegen-identical, paths only. All four
  targets build (granular/passthrough/delay) + host main + test green. **=> ROUND 3 COMPLETE.**
  Flash-verify is a formality (no codegen change) but worth a granular boot smoke-check.

### Round 4 — cut the couplings + enforce -- DONE 2026-06-03

Done in two parts. **R4a (cut):** the remaining platform->granular includes were all dead, stale, or
misplaced code, not real DSP couplings (Stage-2's arena work had already removed the hard one). Fixes:
`common.h` moved repo-root -> `src/` (it was wrongly at root); the tempo BPM<->norm range + helpers
moved off granular `Tempo` into `config.h` (`kTempoMin/MaxBpm`, `tempo_abs_to_norm`); `kKeyInterval`
lifted to the contract `engine/mode.h` (the `kKeyIntervals[]` array stays granular-private);
`lutsinosc.h` and the PCM trio (`pcm_loader`/`pcm_convert`/`sample16`) relocated to a shared
`src/dsp/` tier and `src/memory/` respectively; `storage.h` -> `engine/deck_ref.h`; dead `core.h`/
`granular_engine.h` includes dropped. Result: **`grep` confirms zero `engine/granular/` includes in
`hw/`/`ui/`/`memory/`** - only `app.cpp` (composition root, via `engine_select.h`) sees the concrete
engine. New `src/dsp/` shared-primitive tier seeded (`lutsinosc`, `smooth`, `deline`, `hann`); the
`.cpp`-bearing primitives + a delay-engine refactor to use them are in `TODO.md`. SRAM 185912 B (neutral).

**R4b (enforce):** rather than the originally-planned static-lib + separate-include-roots + drop-`-Isrc/`
(disproportionate churn for a single-binary firmware, with `app.cpp`/`granular_engine` straddling roots),
a `check-boundary` Make target greps `hw/ui/memory` for any `engine/granular/` include and fails the
build; it is wired as a prerequisite of `all`, so every `make` enforces it. Verified: passes clean,
fails on an injected violation, all three engine builds + host green. The static-lib/separate-roots
approach remains a future option if a true multi-engine-in-one-binary/plugin model is ever needed.
**=> ROUND 4 COMPLETE. PHASE 5 COMPLETE.** (Flash-verify the granular path - tempo/key-interval ring/
sample-load/LED-breathe - since R4a touched it, though codegen-identical.)

## Deferred (not Phase 5)

- **`EngineBuffers` shape generalization** (opaque arena the engine sub-allocates) — wants the real
  2nd engine to validate; Round 1 only removes the type dependency.
- Optional `config.h` sample-rate constants as functions; `Buffer` sample-format templating.

## Risks / watch-items

- **Churn + behavior preservation:** Rounds 1 and 3 are large mechanical diffs over hardware-only-
  verified code; keep each behavior-preserving and flash-verify. The `DeckRef` rename is a pure
  type/spelling change (codegen-identical) — lowest-risk despite size.
- **SRAM:** renames + relocation are ~neutral; the lib split should be neutral (same objects). Measure.
  Current granular headroom: ~304 B free.
- **Order discipline:** contract types (R1) before file move (R3) before enforcement (R4); transport
  (R2) can slot before or after R3 but its `Driver::Source` removal belongs with the contract cleanup.
- **Host harness:** keep `make -C host`/`test` green through each round (just fixed; it exercises the
  contract).
