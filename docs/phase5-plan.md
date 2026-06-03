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

### Round 2 — transport relocation (`5c`, = the deferred 3b-2b)
- Move `Driver` out of `Core` to a platform transport service; remove `Driver::Source` from the
  contract (replace with a contract-owned transport-source enum). This is the heavy, isolated round
  the earlier items deferred. Also lift `LEDRing`'s drawing half so `display_model.h` stops including
  `ui/led.ring.h` if not done in Round 1.
- Verify: both variants build; flash granular (transport behavior is hardware-only) + passthrough.

### Round 3 — relocate the granular DSP (`5a`)
- `git mv src/core/*` -> `src/engine/granular/` (minus the contract headers already moved out);
  update the granular-internal `#include "core/..."` -> `"engine/granular/..."` and
  `granular_engine.{h,cpp}`. Platform is already `core/`-free (Round 1), so it's untouched.
- Verify: both variants build; host; flash granular.

### Round 4 — enforce (`5d`)
- Build the granular DSP as a static lib (`libgranular`); give the platform, the contract, and each
  engine separate include roots; drop the blanket `-Isrc/` so the platform physically cannot include
  granular headers (the compiler enforces the boundary). Optionally `libfx`/`libseq`/`libtransport`
  splits. Wire into the existing `ENGINE`-selected Makefile.
- Verify: both variants build + link; flash both.

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
