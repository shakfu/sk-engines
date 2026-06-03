# Item 3(b) plan — prove the swappable engine on hardware

Drafted 2026-06-03 after reading `passthrough_engine.h`, `app.cpp`, the `Makefile`, and `Storage`.
Companion to `docs/architecture.md` (the engine-select mechanism) and `docs/item3-plan.md`
(item 3(a), complete: `core()` deleted, `CoreUI`/`Storage` hold only `IEngine`).

## Goal

Prove the platform/engine abstraction end-to-end on hardware: a second, non-granular engine
selected at build time, booting and running on the real board. This is the payoff of the whole
refactor — the first time engine #2 exists and the seam is exercised by something that is not the
granular looper.

## Staging (decided 2026-06-03)

Split 3(b) so the high-value, low-risk part lands first and the heavy `Driver` surgery stays
separate and optional:

- **3b-1 — engine-select + minimal passthrough variant.** Wire the build-time engine selection and
  promote `PassthroughEngine` to a real, buildable variant with **no capabilities** (just passes
  audio). Goal: `make ENGINE=passthrough` builds, boots, and passes stereo audio without crashing.
  No `Driver` relocation (a capability-less passthrough has no transport).
- **3b-2 — render(DisplayModel) wiring + Driver relocation (deferred, as-needed).** Make the
  platform call `engine.render(DisplayModel&)` for engines that draw their own panel (so passthrough
  shows its meter; granular keeps the query path). Relocate `Driver` out of `Core` to a platform
  transport service only when a non-granular engine wants `CapTransport`.

## Findings from the code (these shape 3b-1)

1. **`capabilities()` is currently advisory — nothing consults it.** grep finds zero
   `capabilities()`/`Cap*` reads in `src/ui`, `src/memory`, `app.cpp`. The platform calls every
   `IEngine` method unconditionally and relies on the Strategy-A no-op defaults. So a no-op engine
   "works" by default — **except** the one path below that dereferences an engine return.
2. **Storage load is the one null-deref hazard.** `DeckStorage::load()` (storage.cpp:145) sets
   `ad.body = _engine->audio_data(_ref)` (→ `nullptr` for a no-op engine) then `init_read_audio(ad)`
   reads WAV bytes into it. Reached on boot via `preload()` if the SD card holds a granular-saved
   sample. `save()` is safe (guarded by `audio_is_empty()`, which defaults true). So the fix is to
   **gate tape ops on `capabilities() & CapTapeStorage`** — which also gives `capabilities()` its
   first real consumer (flushing the advisory-only gap).
3. **`render(DisplayModel)` is not wired in.** The platform's LED path uses the query methods
   (`*_leds`/`render_ring`), not `render()`. `PassthroughEngine::render()` (its meter) is therefore
   dead until the platform calls it. So in 3b-1 the passthrough's rings/indicators are driven by the
   query defaults → **blank/dark LEDs**. That's acceptable to prove the audio swap; meaningful LEDs
   for engine #2 are exactly what motivates the 3b-2 render wiring.
4. **`app.cpp` builds a granular-shaped `EngineContext` unconditionally.** The SDRAM pool hands out
   the granular buffers (`source/detect/delay/slices/track`) regardless of engine;
   `PassthroughEngine::init` ignores them. Harmless for 3b-1 (wasted SDRAM, not SRAM_EXEC; SDRAM is
   64 MB). The `EngineBuffers` shape is the known seam impurity (architecture.md §3) — generalising
   it is out of scope here.

## 3b-1 — DONE + FLASH-VERIFIED 2026-06-03 (passthrough runs on the real board via `make program-dfu`)

> Gotcha (2026-06-03): daisy-web-programmer flashed a cached/stale granular image, which looked like
> "the looper still works" on the passthrough build. `make program-dfu` flashes the actual
> `build/spotykach.bin` and ran the real passthrough. Use `make program-dfu` (or clear the
> web-programmer cache) when swapping variants. The passthrough ELF was independently verified
> granular-free (`nm`: 0 GranularEngine symbols, audio path = PassthroughEngine::process).

Implemented all five steps below. Results:

- `make` (granular, default): builds clean, **190376 B / 88 B free** (+48 B vs the 3(a) build, from the
  Storage `CapTapeStorage` gate — granular behaviour is identical since its `_tape_storage` is true,
  but the gate is real shared code; NOT byte-unchanged as the plan first hoped). Tight; watch it.
- `make ENGINE=passthrough`: builds clean, **150916 B (79%)** — ~39 KB smaller; the granular `Core`/DSP
  is stripped by `--gc-sections` and `granular_engine.cpp` is excluded from the source set entirely
  (confirmed `build/granular_engine.o` absent). Proves the engine-select mechanism at build time.

What still needs hardware: flash `make ENGINE=passthrough` and confirm it boots and passes stereo
audio without crashing (esp. no preload null-deref even with a granular sample on the SD card); LEDs
blank is expected. Then reflash granular and confirm tape save/load/preload still works (regression
check for the capability gate).

**Engine-switch rebuild guard (added 2026-06-03):** a bare `make ENGINE=passthrough` over a stale
granular build first FAILED with `undefined reference to vtable for GranularEngine` — make can't see
the `-DSPK_ENGINE_*` flag change, so it left `app.o` stale. Fixed with a stamp: `build/app.o` (the
only TU that includes `engine_select.h`) depends on `build/.engine-stamp`, whose content is rewritten
only when `ENGINE` differs, so `app.o` rebuilds exactly on a switch. Verified both directions now
build clean with no manual `make clean`.

### Implemented steps

1. **Engine-select mechanism** (per architecture.md §4). New `src/engine/engine_select.h`:
   `#if SPK_ENGINE_GRANULAR -> using ActiveEngine = GranularEngine;` /
   `#elif SPK_ENGINE_PASSTHROUGH -> using ActiveEngine = PassthroughEngine;` / `#else #error`.
   `Makefile`: `ENGINE ?= granular`, map to `-DSPK_ENGINE_*`, and select the engine sources (replace
   the blanket `$(wildcard src/engine/*.cpp)` with the shared engine sources + only the selected
   engine's sources, so the unselected engine isn't compiled). `app.cpp`: `#include
   "engine/engine_select.h"` and `ActiveEngine _engine;` (was `GranularEngine _engine;`). Everything
   else in `app.cpp` already works through the value member + `IEngine&` (3a-4).
2. **Promote `PassthroughEngine` to a buildable variant.** It's header-only today and explicitly
   "not wired into the firmware." Keep it header-only (no .cpp needed — `init/prepare/process/
   render/capabilities` are inline). Drop the aspirational `CapTransport` → **`capabilities()`
   returns `0`** for an honest minimal variant (no transport, no tape, no recording/sequencer).
   `render()` stays (used in 3b-2).
3. **Gate tape storage on `CapTapeStorage`** (the null-safety fix + first real `capabilities()`
   consumer). In `Storage`/`DeckStorage`: if `!(_engine->capabilities() & CapTapeStorage)`, skip
   `preload()` on init and skip tape load/save processing. Granular (which advertises `CapTapeStorage`)
   is unaffected; passthrough skips all SD audio I/O, so the `audio_data()==nullptr` path is never
   reached. Keep the change minimal and localised to the entry points (`init`/`preload`/`process`).
4. **Build both variants.** `make` (default granular) must be byte-unchanged from the 3(a) build;
   `make ENGINE=passthrough` must build clean. Confirm the unselected engine is not compiled.
5. **Flash + verify the passthrough variant on hardware.** Audio in → out passes stereo cleanly;
   the board boots (no preload crash even with a granular sample on the SD card); knobs/pads/MIDI do
   nothing harmful (all no-op). LEDs blank/dark (expected — see finding 3). Then reflash granular
   (`make`) and confirm it still behaves (regression check for the Storage capability gate).

## 3b-2a — render(DisplayModel) wiring. DONE + FLASH-VERIFIED 2026-06-03

> Hardware-verified on the passthrough variant: green play indicators lit (DisplayModel indicator blit
> works) and the rings show a live level meter tracking audio input (render -> blit path works
> end-to-end). A non-granular engine now draws its own panel.

Chose the **capability-bit** mechanism: added `CapOwnDisplay` (`engine_params.h`); `PassthroughEngine`
advertises it. The platform caches `_engine_owns_display` in `CoreUI::init`. When set:

- main loop (`process()`) calls `_engine.render(_display)` (produce) instead of the granular
  `_draw_ring`/`_draw_fx`/`_draw_alt`/`_draw_play` producers — keeps `render()` off the ISR and avoids
  a cross-context ring write;
- the TIM5 ISR (`_draw_leds`) calls new `_blit_display()` (consume): blits the two ring canvases +
  maps every `DisplayModel` indicator to its `Hardware::LedId` (`play/rev/grit/flux/gate_in/cycle/alt`
  x2, `fader` x2, `mode_left/center/right`, `clock_in`, `spot`->`LED_SPOTY_PAD`).
Granular (no `CapOwnDisplay`) keeps its exact query+interpret path — behaviour unchanged. The
passthrough now draws its level meter + lit play LEDs instead of blank rings.

**SRAM:** the render-path code added ~744 B to the granular build (referenced-but-dead there);
funded by `-Os` on `led.ring.cpp` / `color.cpp` / `calibrator.cpp` (all 62 Hz-render/startup, +496 B
reclaimed), net +248 B -> **granular 304 B free** (was 552). The cheap TU-`-Os` levers are now largely
spent (storage/card/led.ring/color/calibrator + the 4 UI TUs are all `-Os`); further reclaim needs
real code reduction.

### Reclaiming the render-path tax (deferred — only if SRAM tightens)

`CapOwnDisplay` is a **runtime** check, so `_blit_display()` + the `render()`/`_draw_*` call sites are
compiled into *both* builds; granular links `_blit_display` (~744 B) even though it never runs it.
At 304 B free this is fine — do nothing. If a future round (more own-display indicators, a heavier
blit) makes granular overflow again, the options, best-first:

1. **Real code reduction elsewhere** (preferred): the cleanest headroom is shrinking genuine logic,
   not optimisation flags (TU-`-Os` is spent).
2. **Build-specific render path** (the better dedicated lever if forced): add a compile-time trait to
   `engine_select.h` (`constexpr bool kEngineOwnsDisplay`, or `static constexpr bool owns_display` on
   each engine) and branch with C++17 `if constexpr` in `core.ui.cpp`/`leds.cpp`. The discarded
   branch's calls become unreferenced and `--gc-sections` drops them, so each build links only its own
   render path (granular sheds `_blit_display`; passthrough sheds the granular `_draw_*` producers).
   Reclaim is large (~744 B) and certain. **Costs:** the platform TUs now `#include engine_select.h`
   and compile-time-couple to the engine selection (a small leak of "platform is engine-agnostic"),
   and `core.ui.o`/`core.ui.leds.o` become engine-define-dependent — so `build/.engine-stamp` must be
   widened to rebuild them on an `ENGINE` switch (else a stale LED path links, the footgun the stamp
   already fixes for `app.o`).
3. **Table-driven `_blit_display`** (NOT recommended): replace the ~21 unrolled `_hw.leds.Set` calls
   with a table + loop. Stays runtime-clean/engine-agnostic, but the payoff is uncertain and modest:
   a `static` table needs link-time-constant addresses, which `this->_display` is not, so you either
   build the table at runtime each call (construction cost eats the saving) or hand-roll a `constexpr`
   offset table + pointer arithmetic into `DisplayModel` (works, but fiddly given the array+scalar
   field mix). Trades greppable, obvious code for indirection for an unmeasured gain — skip it.

## 3b-2b — Driver relocation (deferred)

- **Driver relocation.** Only forced when a non-granular engine wants `CapTransport`. Move `Driver`
  (clock/transport, currently inside `Core`) to a platform transport service both engines can use;
  retire the `transport_*` forwards on `IEngine`. Heavy, isolated, do it when needed.

## Risks & watch-items

- **Storage gate correctness.** The capability gate must not change granular behaviour (granular has
  `CapTapeStorage`). Verify granular tape save/load/preload still works after the gate (flash).
- **SRAM.** The granular build is the constrained one (136 B free). 3b-1 adds the `capabilities()`
  gate (a few instructions in Storage, a non-RT TU) and the engine-select indirection (compile-time,
  zero runtime). Expect ~neutral; measure. The passthrough build has its own (tiny) budget.
- **Flash-verify debt.** 3a-3/3a-3b/3a-4 are still flash-unverified; verify the 3(a) milestone before
  building 3(b) on top, so any regression is attributable.
- **`#define STORAGE` in app.cpp** stays defined for both variants; the capability gate is what makes
  storage inert for passthrough (don't `#ifdef` it out per-engine — keep one code path).
