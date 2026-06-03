# Engine file layout — what belongs to which engine, and where new ones go

How the firmware's files split between the fixed **platform**, the **contract**, and each swappable
**engine**, plus the convention for adding a new engine. Companion to `docs/architecture.md`
(the platform/engine design) and `docs/item3b-plan.md` (the build-time engine selection).

## The three buckets

### Contract / shared (platform + every engine depend on these)

| File | Role |
|---|---|
| `src/engine/iengine.h` | the `IEngine` interface |
| `src/engine/engine_params.h` | `ParamId`, `ConfigId`, `Capabilities`, `FxKind`, `DeckLayout`, `GritReseed` |
| `src/engine/display_model.h` | `DisplayModel` (the panel an own-display engine fills) |
| `src/engine/engine_leds.h` | LED-query POD structs (granular-flavored, but referenced by `IEngine`) |
| `src/engine/engine_select.h` | build define -> `ActiveEngine` typedef |
| `src/core/engine_context.h` | `EngineContext`/`EngineBuffers` (init injection) — *logically contract, still in core/* |
| `src/core/itimesource.h` | `ITimeSource` clock abstraction — *logically contract, still in core/* |

### Granular engine

`src/engine/granular_engine.{h,cpp}` + **all of `src/core/`** except the two contract headers above
(~58 files: `core`, `deck`, `generator`, `vox`, `buffer`, `fx*`, `driver`, `synclock`, `tempo`,
`divider`, `track`, `panner`, `modulator`, `detector`, `follower`, `biquad`, `adenv`, `click`,
`cpattern`, plus their headers and `mode.h`/`speed.map.h`/etc.). `src/core/` is the granular engine's
private DSP, kept at its legacy path for now (see "Deferred").

### Passthrough engine

`src/engine/passthrough/passthrough_engine.h` — header-only; depends only on the contract +
`nocopy.h` + `<cmath>`. The reference for how small a non-granular engine is.

## A new engine

Lives in its own subdirectory: **`src/engine/<name>/`**, holding `<name>_engine.{h,cpp}` and that
engine's private DSP. It depends only on the contract headers. Checklist (also in `architecture.md`):

1. Subclass `IEngine`; implement `init`/`prepare`/`process` (own your DSP privately — do not reuse
   `src/core/`, that is granular-specific).
2. Return a `capabilities()` bitset; override only the methods for regions you support.
3. Map `ParamId`s in `set_param`/`param`; fill `render(DisplayModel&)` and advertise `CapOwnDisplay`
   if you draw your own panel.
4. Add it to `engine_select.h` (`#elif SPK_ENGINE_<NAME>`) and the `Makefile` `ENGINE` switch
   (`ENGINE_SOURCES = src/engine/<name>/*.cpp ...`).

## Makefile pattern

The `ENGINE` variable selects the define **and** the sources, so each build compiles only its own
engine:

```make
ifeq ($(ENGINE), granular)
  C_DEFS += -DSPK_ENGINE_GRANULAR
  ENGINE_SOURCES = src/engine/granular_engine.cpp $(wildcard src/core/*.cpp)
else ifeq ($(ENGINE), passthrough)
  C_DEFS += -DSPK_ENGINE_PASSTHROUGH
  ENGINE_SOURCES =                       # header-only
endif
CPP_SOURCES = main.cpp app.cpp $(ENGINE_SOURCES) <platform: hw/ ui/ memory/>
```

`build/.engine-stamp` forces `app.o` (the only define-dependent TU) to rebuild on an `ENGINE`
switch, so swapping needs no `make clean`.

## Reorg status (2026-06-03)

**Done + verified:**
- **Per-engine subdir convention** adopted; `passthrough_engine.h` moved to `src/engine/passthrough/`.
- **Granular DSP compiled only for the granular build:** `src/core/*.cpp` moved out of the global
  `CPP_SOURCES` into the granular `ENGINE_SOURCES`. **Verified the passthrough now links with zero
  `src/core/` objects** — i.e. the platform has *no link-dependency on the granular DSP* (the 3a/3b
  decoupling is clean at the link level, not just conceptually). Non-granular builds no longer compile
  the 58 granular files at all.

**Deferred to Phase 5 (real couplings, not cheap moves):**
- **Relocating `src/core/` -> `src/engine/granular/`.** ~58 files + every `#include "core/..."`; pure
  mechanical churn with no functional gain — do it as the build-boundary round. Until then `src/core/`
  = "granular's private DSP (legacy path)."
- **Moving the contract headers** `engine_context.h`/`itimesource.h` into `src/engine/`. Looks cheap
  but `engine_context.h` includes `core/deck.h` (for `Buffer::Frame`/`Event`/`Deck::Ref` in
  `EngineBuffers`), so moving it would make `engine/` depend on `core/` — backwards. Relocate it
  *with* the `EngineBuffers` generalization below.

## Known contract leaks (the build-boundary work for Phase 5)

The contract isn't pure yet — three granular dependencies leak into it:
1. `iengine.h` -> `core/driver.h` (`Driver::Source` for `transport_source()`) — removed by the
   `Driver` relocation (3b-2b).
2. `engine_context.h` -> `core/deck.h`: **`EngineBuffers` is granular-shaped** (`source/detect/delay/
   slices/track`). A new engine with different buffer needs (e.g. a delay's delay-line) should *drive*
   how this generalizes — design it against the first real consumer, not abstractly.
3. `display_model.h` -> `ui/led.ring.h` (`DisplayModel` embeds `LEDRing`) — a UI dependency in the
   contract; lift `LEDRing`'s drawing half out, or make the dependency one-way, in Phase 5.

`Deck::Ref` (from `core/deck.h`) is also the pervasive "which of the two channels" type every
`IEngine` method takes — conceptually platform, physically granular. A future cleanup could move it
to the contract.

## Host harness — fixed 2026-06-03

The host harness had been broken since item 3a-4 deleted `engine.core()` (both `host/main_host.cpp`
and `host/test_engine_params.cpp` reached the graph via `core()`; it was never re-run). Rewritten
onto the public `IEngine` surface: `transport_set_on_quarter`/`transport_tick` (was
`core().driver()`), `set_config(ConfigId::Route/Mode, ...)` (was `core().set_route`/`deck().set_mode`),
and `on_record_pad`/`on_play_pad` (approximate the old `toggle_recording`/`disarm()`+`play()` in the
WAV runner). The one casualty: the FluxMix leaf-landing check (`core().deck().fx().flux_mix()`) was
removed — internal leaf state is no longer observable from the public API by design; the param cache
round-trip + finite-output smoke remain. Both `make -C host` and `make -C host test` build clean
(`OK: all engine param checks passed`).
