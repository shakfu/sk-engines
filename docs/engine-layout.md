# Engine file layout — what belongs to which engine, and where new ones go

How the firmware's files split between the fixed **platform**, the **contract**, and each swappable **engine**, plus the convention for adding a new engine. Companion to `docs/architecture.md` (the platform/engine design), `docs/dev/item3b-plan.md` (the build-time engine selection), and `docs/engines/` (per-engine design docs + the shared transport/knob-routing model).

## The three buckets

### Contract / shared (platform + every engine depend on these)

| File | Role |
|---|---|
| `src/engine/iengine.h` | the `IEngine` interface |
| `src/engine/engine_params.h` | `ParamId`, `ConfigId`, `Capabilities`, `FxKind`, `DeckLayout`, `GritReseed` |
| `src/engine/display_model.h` | `DisplayModel` (the panel an own-display engine fills) |
| `src/engine/engine_leds.h` | LED-query POD structs (granular-flavored, but referenced by `IEngine`) |
| `src/engine/engine_select.h` | build define -> `ActiveEngine` typedef |
| `src/engine/engine_context.h` | `EngineContext`/`EngineArena` (init injection) — contract, moved here in R3 |
| `src/engine/itimesource.h` | `ITimeSource` clock abstraction — contract, moved here in R3 |
| `src/engine/itransport.h` | `ITransport` (read-only clock view) + `TransportTick` — engine-facing transport contract |

### Platform transport (fixed; shared by every engine)

`src/transport/` — the platform clock/transport service, **compiled into every variant** (added to `CPP_SOURCES` directly, not `ENGINE_SOURCES`): `transport.{h,cpp}` (clock-source select, tap tempo, key/quarter counting, clock-out + on-quarter callbacks, fans `TransportTick` to the subscribed engine) over `synclock.{h,cpp}` + `tempo.{h,cpp}`. Split out of the old granular `Driver` so tempo-synced non-granular engines (the delay; a future Euclidean drum engine) share one clock. The platform (app + `CoreUI`) owns the concrete `Transport` and drives it; engines get the read-only `ITransport` via `EngineContext`. `src/transport/` is in the `check-boundary` `PLATFORM_DIRS` (may use only contract headers, never `engine/granular/`). The shared clock-divider primitive lives in `src/dsp/divider.{h,cpp}` (used by both `Transport` and granular `Deck`'s sequencer).

### Granular engine

**All of `src/engine/granular/`**: the `IEngine` adapter `granular_engine.{h,cpp}` (class `GranularEngine`) plus the private DSP (`core`, `deck`, `generator`, `vox`, `buffer`, `fx*`, `track`, `panner`, `modulator`, `detector`, `follower`, `biquad`, `adenv`, `click`, `cpattern`, plus their headers and `mode.h`/`speed.map.h`/etc.). The DSP was relocated from `src/core/` in Phase 5 R3 (2026-06-03); the adapter joined it under `granular/` to match the per-engine subdir convention (`delay/`, `edrums/`, `passthrough/` all keep their `*_engine.*` beside their DSP). The clock (`driver`/`synclock`/`tempo`/`divider`) was split out to `src/transport/` + `src/dsp/` when transport became a shared platform service; granular `Core` now subscribes to the platform `Transport`'s ticks.

### Reverb engine

**All of `src/engine/reverb/`**: the `IEngine` adapter `reverb_engine.{h,cpp}` (class `ReverbEngine`, a stereo reverb), the Faust DSP sources `dattorro.dsp`/`zita.dsp` (+ the retained spike `voice.dsp`), and the **generated** kernels `faust_kernel_<name>.h` (one `class mydsp` per namespace `rv_<name>`, produced by `make faust-kernels` from cyfaust's cpp backend). The kernels' delay-line state (~1 MB across both reverbs) is placement-new'd into the SDRAM arena, so `SRAM` stays flat; `SRAM_EXEC` (code) is the constraint. See [engines/reverb.md](engines/reverb.md) and `src/engine/reverb/README.md`.

**Shared Faust infrastructure**: `src/engine/faust_arch.h` is the hand-written, MIT arch shim (`dsp`/`UI`/`Meta` base types the generated kernels assume) - shared by every engine that uses a Faust kernel (reverb and tape), so Faust's GPL-with-exception architecture headers are not vendored. `make faust-kernels` generates every engine's kernel from one `FAUST_KERNELS` list (each `<dir>:<ns-prefix>:<name>`); add a kernel by dropping its `.dsp` in the engine dir and appending a spec.

The **tape engine** (`src/engine/tape/`) also carries a Faust kernel: `tapefx.dsp` -> `faust_kernel_tapefx.h` (namespace `tfx_tapefx`), the per-deck wow/flutter + Jiles-Atherton hysteresis FX, placement-new'd in the SDRAM arena. See [engines/tape.md](engines/tape.md).

### Passthrough engine

`src/engine/passthrough/passthrough_engine.h` — header-only; depends only on the contract + `nocopy.h` + `<cmath>`. The reference for how small a non-granular engine is.

### gen~ engines (gen-dsp)

**Shared gen~ infrastructure**: `src/engine/gen/` is the gen-dsp counterpart of `faust_arch.h` — shared runtime support for the **family** of gen~ engines, not a leaf engine itself. It holds `gen_engine.h` (the `GenEngine<W>` template, one `IEngine` adapter shared by every gen~ export — it forwards `process()` to the generated DSP and maps `ParamId`s to gen params) and `genlib_arena.{h,cpp}` (the `genlib` memory runtime bound to the SDRAM arena, so gen~'s allocations land in the engine arena rather than the heap). It lives in `src/engine/` — not `src/dsp/` — deliberately: it is `IEngine`-coupled (`gen_engine.h` includes `engine/iengine.h`, `GenEngine<W> : public IEngine`), so it belongs to the engine framework, whereas `src/dsp/` is the engine-*agnostic* primitive tier. This mirrors the Shared Faust infrastructure above (`faust_arch.h`): codegen-runtime support for an engine family sits in `src/engine/`, shared by every gen~ engine.

A concrete gen~ engine lives in its own `src/engine/<name>/` (e.g. `gigaverb/`) - an unprefixed plain engine dir, exactly like a native or Faust engine - holding the **generated** gen~ export under `gen/` (`gen_exported.{cpp,h}` + the genlib `gen_dsp/` runtime), the `_ext_daisy.cpp` platform bridge, and the `<name>_engine.h` trait header (e.g. `gigaverb_engine.h`) that supplies the `W` for `GenEngine<W>` — `W::process`/`set_param`/`get_param` forward to the export. The host-side generator `scripts/gen_engine.py` (`make gen-engines`) turns a gen~ export into a wired-in engine (`ENGINE=<name>`, `SPK_ENGINE_<NAME>`) and appends its `else ifeq` block to `engine_select.h` + the `Makefile`. Note the only `gen`-named directory is the shared infra `src/engine/gen/`; concrete gen~ engines carry no `gen_` prefix (the engine's gen~ provenance lives in its docs and the `manifest.json`, not its build-target name). See [engine-types/gen.md](engine-types/gen.md).

## A new engine

Lives in its own subdirectory: **`src/engine/<name>/`**, holding `<name>_engine.{h,cpp}` and that engine's private DSP. It depends only on the contract headers. Checklist (also in `architecture.md`):

1. Subclass `IEngine`; implement `init`/`prepare`/`process` (own your DSP privately — do not reuse `src/engine/granular/`, that is granular-specific).

2. Return a `capabilities()` bitset; override only the methods for regions you support.

3. Map `ParamId`s in `set_param`/`param`; fill `render(DisplayModel&)` and advertise `CapOwnDisplay` if you draw your own panel.

4. Add it to `engine_select.h` (`#elif SPK_ENGINE_<NAME>`) and the `Makefile` `ENGINE` switch (`ENGINE_SOURCES = src/engine/<name>/*.cpp ...`).

## Makefile pattern

The `ENGINE` variable selects the define **and** the sources, so each build compiles only its own engine:

```make
ifeq ($(ENGINE), granular)
  C_DEFS += -DSPK_ENGINE_GRANULAR
  ENGINE_SOURCES = $(wildcard src/engine/granular/*.cpp)   # adapter + DSP, all under granular/
else ifeq ($(ENGINE), passthrough)
  C_DEFS += -DSPK_ENGINE_PASSTHROUGH
  ENGINE_SOURCES =                       # header-only
endif
CPP_SOURCES = main.cpp app.cpp $(ENGINE_SOURCES) <platform: hw/ ui/ memory/>
```

`build/.engine-stamp` forces `app.o` (the only define-dependent TU) to rebuild on an `ENGINE` switch, so swapping needs no `make clean`.

## Reorg status (2026-06-03)

**Done + verified:**

- **Per-engine subdir convention** adopted; `passthrough_engine.h` moved to `src/engine/passthrough/`.

- **Granular DSP compiled only for the granular build:** `src/engine/granular/*.cpp` lives in the granular `ENGINE_SOURCES`, not the global `CPP_SOURCES`. Non-granular builds link with zero granular objects — the platform has *no link-dependency on the granular DSP*.

- **Relocated `src/core/` -> `src/engine/granular/`** (Phase 5 R3, 2026-06-03). 64 git renames + 14 include-repath edits; codegen-identical (SRAM/SDRAM byte-identical pre/post). The two contract headers (`engine_context.h`, `itimesource.h`) moved up to `src/engine/`.

- **`EngineBuffers` generalized to an opaque SDRAM arena** (2026-06-03). `EngineContext` now carries only an `EngineArena {base, bytes}`; each engine sub-allocates its own buffers via `engine/arena.h`. The SDRAM pool (`hw/buffer.sdram`) is now granular-DSP-free — it hands out one 48 MB byte block and knows nothing of `source/detect/delay/slices/track`. This was the coupling that blocked R4.

## Known contract leaks (the build-boundary work for Phase 5)

Resolved during R1-R3 + the arena generalization:

1. ~~`iengine.h` -> `core/driver.h` (`Driver::Source`)~~ — `ClockSource` lifted to the contract (R1/R2).

2. ~~`engine_context.h` -> `core/deck.h` (`EngineBuffers` granular-shaped)~~ — replaced by the opaque `EngineArena`; the contract no longer references any granular type.

3. ~~`display_model.h` -> `ui/led.ring.h`~~ — `LEDRing`/`Color` lifted out to `src/engine/` (R2).

**R4 (done 2026-06-03)** cut the last platform-reaches-into-granular path-includes and now enforces the boundary. They were all dead/stale/misplaced, not real DSP couplings: `ui/core.ui.h` dropped `core.h` (only needed `Tempo::abs_to_norm`, now `tempo_abs_to_norm` in `config.h`) and swapped `granular_engine.h` for `iengine.h`; `kKeyInterval` lifted to `engine/mode.h`; `lutsinosc.h` moved to the shared `src/dsp/` tier; `memory/storage.h` -> `engine/deck_ref.h`; `hw/card.h`'s `pcm_loader.h` (self-contained PCM util) moved to `src/memory/`. Enforcement is a `check-boundary` Make target (greps `hw/ui/memory` for `engine/granular/` includes, wired as a prerequisite of `all`) - chosen over the static-lib/separate-include-roots/drop-`-Isrc/` approach, which is disproportionate churn for a single-binary firmware (kept as a future option for a true multi-engine-in-one-binary model). `app.cpp` is exempt (composition root). `DeckRef` is contract-owned (`engine/deck_ref.h`).

## Host harness — fixed 2026-06-03

The host harness had been broken since item 3a-4 deleted `engine.core()` (both `host/main_host.cpp` and `host/test_engine_params.cpp` reached the graph via `core()`; it was never re-run). Rewritten onto the public `IEngine` surface: `transport_set_on_quarter`/`transport_tick` (was `core().driver()`), `set_config(ConfigId::Route/Mode, ...)` (was `core().set_route`/`deck().set_mode`), and `on_record_pad`/`on_play_pad` (approximate the old `toggle_recording`/`disarm()`+`play()` in the WAV runner). The one casualty: the FluxMix leaf-landing check (`core().deck().fx().flux_mix()`) was removed — internal leaf state is no longer observable from the public API by design; the param cache round-trip + finite-output smoke remain. Both `make -C host` and `make -C host test` build clean (`OK: all engine param checks passed`).
