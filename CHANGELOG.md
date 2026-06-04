# Changelog

All notable changes to the Spotykach firmware are recorded here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project does not yet expose a version constant in the firmware; entries are grouped
by the Git tag / release they ship in.

## [Unreleased]

### Changed

- **`.cpp`-bearing primitives moved into the `src/dsp/` shared tier.** `git mv` of `biquad`,
  `follower`, `adenv`, and `cpattern` (`.h` + `.cpp`) from `src/engine/granular/` into `src/dsp/`,
  joining the header-only batch seeded in Phase 5 R4. Their `.cpp` now compile from a new global
  `$(wildcard src/dsp/*.cpp)` in `CPP_SOURCES` so every engine links them, and the granular consumers
  reach them through the `dsp/` include prefix (`vox.h`, `fx.h`, `echo.h`, `modulator.h`), matching
  the platform/engine boundary convention. `biquad.cpp`'s `#include "../../common.h"` becomes
  `"common.h"` (same `src/common.h`, via `-Isrc`). Granular codegen is unaffected (identical TU set;
  `SRAM_EXEC` 185920 B, +8 B object-reorder alignment); the non-granular engines now compile these
  TUs but `--gc-sections` strips every unused symbol, so passthrough/delay are size-unaffected and
  link cleanly. Host build (`host/Makefile`) wired with the same `src/dsp/*.cpp` glob. (`src/dsp/`,
  `src/engine/granular/{vox,fx,echo,modulator}.h`, `Makefile`, `host/Makefile`)

### Fixed

- **Restored the host unit-test harness (`test/`).** `test/Makefile` had been broken since the
  Phase-5 R3 `src/core` -> `src/engine/granular` move: it still pointed at `../src/core/*` sources and
  `-I../src/core`, none of which exist, so `make -C test` could not build. Repathed the unit sources
  to their current homes (`divider`/`synclock` under `src/engine/granular/`, `follower` under
  `src/dsp/`) and corrected the include roots. The suite builds and passes again (116 assertions).
  (`test/Makefile`)

- **App-composition tier moved into `src/` (root hygiene).** `git mv` of `app.{h,cpp}` and `meter.h`
  from the repository root into `src/`, leaving the root as just the entry point (`main.cpp`). Same
  motivation as the earlier `common.h` root->`src/` move: a coherent "the library lives in `src/`"
  rule, and it clears the latent `-Isrc` fallback smell (`meter.h`'s `#include "nocopy.h"` now
  resolves as a `src/` sibling rather than via the include-path fallback). `main.cpp` stays at root as
  the thin entry; its `#include "app.h"` resolves through `-Isrc`. The `build/app.o` engine-stamp rule
  is unaffected (libDaisy flattens objects to `build/` by basename), and `check-boundary` is unaffected
  (`src/app.cpp`, the composition root, is exempt and not under the scanned `hw/ui/memory` dirs). Purely
  mechanical (3 git renames; `SRAM_EXEC` byte-identical at 185912 B / 97.61% before and after).
  (`src/app.{h,cpp}`, `src/meter.h`, `Makefile`)

## [0.2.0]

### Added

- **Stereo delay engine (second full engine).** `make engine-delay` builds a dual-tap stereo delay
  (deck A = left, deck B = right) with fractional-interpolated delay lines: SIZE sets delay time, POS
  the feedback, the mix knob the wet/dry, and PITCH transposes the delay taps +/-1 octave via a
  crossfading two-head pitch shifter. Advertises `CapOwnDisplay` (blue pitch rings). Proves the
  platform hosts a non-granular engine end to end. (`src/engine/delay/`)

- **Engine-drawn panels via `render(DisplayModel)` + the `CapOwnDisplay` capability.** An engine that
  advertises `CapOwnDisplay` fills the hardware-agnostic `DisplayModel` (rings + named indicators) in
  `render()`; the platform calls it from the main loop and blits it in the LED ISR (`_blit_display`),
  bypassing the granular `*_leds`/`render_ring` query path. The granular engine (no `CapOwnDisplay`)
  keeps its query+interpret path unchanged. The bundled `PassthroughEngine` now draws a live input
  level meter + lit play indicators instead of blank rings. (`src/engine/iengine.h`,
  `src/engine/engine_params.h`, `src/ui/core.ui.cpp`, `src/ui/core.ui.leds.cpp`)

- **One-shot variant flash targets.** `make engine-granular`, `make engine-delay`, and
  `make engine-passthrough` do clean -> build -> flash (`program-dfu`) in a single command (device in
  DFU mode first). (`Makefile`)

- **`compile_commands.json` for clangd.** Generate it from a real build with `bear -- make` so the
  editor sees the actual include paths (the libDaisy header set, `-Isrc`), the `-std=c++17`, and the
  build-time `-DSPK_ENGINE_*` define. It is git-ignored and is a snapshot of whichever engine was built
  (granular by default); regenerate after structural changes. Tooling only - does not affect the
  firmware. (`.gitignore`, README)

### Changed

- **Platform/engine boundary cut and enforced (Phase 5, round 4).** The platform (`hw/`, `ui/`,
  `memory/`) now reaches the engine only through the contract headers in `src/engine/` - it includes
  zero granular DSP headers. The remaining couplings were dead/stale/misplaced code, not real DSP
  dependencies: the tempo BPM<->normalized range moved off granular `Tempo` into `config.h`
  (`kTempoMin/MaxBpm` + `tempo_abs_to_norm`/`tempo_norm_to_abs`); the `kKeyInterval` enum was lifted to
  the contract (`engine/mode.h`); the self-contained PCM utilities (`pcm_loader`/`pcm_convert`/
  `sample16`) moved to `src/memory/`; `storage.h` now includes `engine/deck_ref.h`; and stale
  `core.h`/`granular_engine.h` includes were dropped. A `make check-boundary` target (wired as a
  prerequisite of `all`) fails the build if any `hw/`/`ui/`/`memory/` translation unit reintroduces an
  `engine/granular/` include, so the boundary is enforced by the build rather than by review. `app.cpp`
  is exempt as the composition root. (`src/config.h`, `src/engine/mode.h`, `src/memory/`, `src/ui/`,
  `src/hw/`, `Makefile`)

- **New `src/dsp/` shared-primitive tier.** Engine-agnostic DSP primitives that more than one consumer
  needs now live in `src/dsp/`, with the dependency flowing platform/engine -> dsp and never the
  reverse. Seeded with `lutsinosc.h` (sine LUT, used by the UI's LED rendering and the granular LFO)
  plus the header-only `smooth.h`, `deline.h`, and `hann.h`. The `common.h` shared header, previously
  sitting at the repository root by mistake, also moved into `src/`. (`src/dsp/`, `src/common.h`)

- **Granular DSP relocated to `src/engine/granular/` (Phase 5, round 3).** `git mv src/core` ->
  `src/engine/granular/`, with the two contract headers (`engine_context.h`, `itimesource.h`) moved up
  to the contract root `src/engine/`. All `#include "core/..."` references repathed across the tree and
  both Makefiles. The source tree now reads as one platform plus per-engine subdirectories
  (`granular/`, `delay/`, `passthrough/`). Purely mechanical (64 git renames; SRAM/SDRAM byte-identical
  before and after). (`src/engine/granular/`, `src/engine/`)

- **`EngineBuffers` generalized to an opaque SDRAM arena.** `EngineContext` now carries a single
  `EngineArena {base, bytes}` instead of granular-shaped buffer pointers; each engine sub-allocates its
  own buffers with a bump allocator (`src/engine/arena.h`). The SDRAM pool (`hw/buffer.sdram`) hands
  out one 48 MB block and no longer references any granular buffer (`source/detect/delay/slices/track`)
  - removing the last hardware-layer dependency on the granular engine. Granular's `Core::init`
  sub-allocates from the arena preserving the exact sizes, 32 KB alignment, and zeroing (behaviour-
  identical). Reclaimed ~4.2 KB of `SRAM_EXEC` as a side effect. (`src/engine/arena.h`,
  `src/hw/buffer.sdram.{h,cpp}`, `src/engine/engine_context.h`, `src/engine/granular/core.cpp`)

- **Compiler-enforceable platform/engine boundary (Phase 5, rounds 1-2).** The `IEngine` contract no
  longer pulls any granular `core/` types: the A/B selector (`DeckRef`), `Mode`/`Route`, the LED-query
  enums (`ModType`/`GritMode`/`DeckSource`), and the transport clock source (`ClockSource`) were
  relocated into contract headers (`src/engine/deck_ref.h`, `src/engine/mode.h`); the granular classes
  alias them, so their internals are unchanged (a duplicate `ModType` in `core.h` was unified away).
  `EngineBuffers` was type-stripped (`void*` + counts, casts in `Core::init`) so `engine_context.h`
  carries no granular types; `LEDRing` + `Color` moved `src/ui/` -> `src/engine/` so `DisplayModel`
  has no `ui/` dependency. All behaviour-preserving (codegen-neutral) and verified across the granular,
  passthrough, and host builds. The granular DSP `Driver` stays inside the granular engine; relocating
  it to a platform transport service is deferred to a transport-capable engine. (`src/engine/`,
  `src/ui/`)

- **Engine file layout + build selection.** Engines live under `src/engine/<name>/`
  (`passthrough/passthrough_engine.h`); the `ENGINE` Makefile variable now selects the engine sources
  so a non-granular build doesn't compile the granular DSP at all. A `build/.engine-stamp` dependency
  rebuilds the engine-dependent object when `ENGINE` changes, so switching variants needs no
  `make clean`. See `docs/engine-layout.md`. (`Makefile`, `src/engine/engine_select.h`)

### Fixed

- **Host harness restored.** `make -C host` and `make -C host test` had stopped compiling when the
  `engine.core()` escape hatch was removed (both reached the graph through it). Rewritten onto the
  public `IEngine` API (`transport_*`, `set_config`, `on_*_pad`); the one white-box assertion that
  read internal fx state was dropped (no longer observable by design). (`host/`)

## [0.1.3]

### Fixed

- **Granular slice fade-out at high pitch/speed (V6).** `Vox::_set_decay_start()` assigned
  the grain decay-start position only when the fade region was shorter than the slice; in
  the high-increment, small-slice corner (e.g. playhead increment 8.0 with the minimum
  slice of 1344 samples, where the fade region 1536 exceeds the slice) it left a stale,
  history-dependent value. That produced an inconsistent fade trigger - grains cut short,
  or ending without their ~4 ms Hann fade and clicking at the boundary. The function now
  always assigns `_decay_start = std::max(0.f, _size - decay_length)`, so when the fade is
  longer than the slice the whole grain becomes the fade. Deterministic and bounded.
  Consistent with the manual's "slice-mode overdub may glitch when synced to MIDI clock."
  This is the only change in this release that alters runtime audio behaviour; validate by
  ear at maximum pitch on the smallest slice. (`src/core/vox.cpp`)

- **Stack buffer overflows in SD-card path building.** Five `char` buffers used to build
  tape/file paths were each one byte too small for their string plus the null terminator
  (`tape_dir_path[4]` for `"SK/G"`, three `audio_path[11]` for `"/SK/G/1.WAV"`, two
  `name[5]` for `"1.WAV"`). The `sprintf` calls wrote the terminating null one byte past the
  end - benign-by-luck via stack padding, but undefined behaviour. Buffers resized to fit;
  surfaced while replacing `sprintf` (see Changed). (`src/hw/card.cpp`, `src/memory/storage.cpp`)

### Changed

- **Platform/engine decoupling: the UI/HW platform no longer touches the DSP core.** A
  multi-round, behaviour-preserving, hardware-verified refactor inserted an `IEngine` seam
  (`src/engine/iengine.h`) between the fixed platform (`CoreUI`/`Storage`/`Hardware`) and a
  swappable DSP engine (the granular looper, now `GranularEngine`). The platform drives the
  engine entirely through `IEngine`: parameters (`set_param`/`param` keyed by `ParamId`),
  categorical config (`set_config` for route / deck mode / LFO shape / mod flags, plus
  `toggle_grit_mode` and `tempo_to_fit`), knob layout (`deck_layout`/`size_sets_tempo`, so the
  platform branches the SIZE/ENV knobs without reading the engine's `Mode`), MIDI, pads,
  CV/gate, the tape-storage audio port, transport, and the DAC mod-CV output. The
  `engine.core()` escape hatch and the concrete `GranularEngine&` constructor are gone -
  `CoreUI` holds only `IEngine&`, `Storage` an `IEngine*` - and `src/core/` is now
  platform-independent (no libDaisy include; sample rate, block size, buffers and the clock are
  injected via `EngineContext`/`ITimeSource`). The per-control MValue pickup state was rekeyed
  by `ParamId` (one array replacing ~21 named members). `capabilities()` gained its first real
  consumer: `Storage` skips tape save/load/preload unless the engine advertises `CapTapeStorage`.
  No runtime audio behaviour change in the granular build (verified by flashing); the seam is
  additive, so `SRAM_EXEC` rose to ~99.95% used (~88 B free) - the cost is amortised across
  future engines that reuse the whole platform. (`src/engine/`, `src/ui/`, `src/memory/`,
  `src/core/`)

- **SD path building uses manual string joins instead of `sprintf`.** All five `sprintf`
  calls (plain `%s` concatenations) were replaced with `strcpy`/`strcat`. This lets the
  linker drop the `sprintf`/`fprintf` objects, recovering ~648 B of the nearly-full
  `SRAM_EXEC` region (99.05% -> 98.71% used). Note: the core `vfprintf`/`dtoa` machinery
  (~2.3 KB) remains, pulled in by libDaisy's `logger.o` via `vsnprintf` - reclaiming it
  would require a libDaisy-side change. (`src/hw/card.cpp`, `src/memory/storage.cpp`)

These are correctness/intent fixes that do not change current runtime behaviour; they
harden the code against future changes.

- **`Config::_is_loaded` default-initialised (V1).** Added `= false` to the flag backing
  `Config::is_loaded()`. In current usage `Config` exists only as the zero-initialised
  static singleton (`Config::dynamic()`), so the flag was already reliably `false`; this
  guards against any future non-static instantiation reading an indeterminate value.
  (`src/config.h`)

- **Tape navigation uses `kStorageTapeCount` (V2).** `DeckStorage::next_tape`/
  `previous_tape` and the `kTapeName[]` array were sized/wrapped on `kStorageSlotCount`.
  Both constants are 6 today, so the generated code is unchanged; the fix uses the
  semantically correct constant so tape handling stays correct if tape count ever diverges
  from slot count. (`src/memory/storage.cpp`)

### Added

- **Swappable DSP engine, selected at build time.** The firmware is now a fixed hardware/UI
  platform that hosts one `IEngine` implementation per build. `make` builds the granular looper
  (default); `make ENGINE=passthrough` builds a minimal stereo-passthrough variant
  (`capabilities() == 0`, ~150 KB vs the granular ~186 KB as the granular DSP is dropped).
  Selection is one build-time choice: the `ENGINE` Makefile variable emits a single
  `-DSPK_ENGINE_*`, which `src/engine/engine_select.h` maps to the concrete `ActiveEngine`
  that `app.cpp` instantiates as a static member; the platform only ever sees `IEngine` (no
  per-feature `#ifdef`s). A stamp dependency (`build/.engine-stamp`) rebuilds the engine-dependent
  object when `ENGINE` changes, so switching variants needs no `make clean`. Both variants
  verified on hardware via `make program-dfu`. (`src/engine/engine_select.h`,
  `src/engine/passthrough_engine.h`, `Makefile`, `app.cpp`)

- **Off-target engine harness** (`make -C host`): runs the real `Core`/`GranularEngine` over WAV
  through the `IEngine` interface, with `make -C host test` asserting the parameter API across all
  deck modes. Does not build or affect the firmware; the pot/pad/LED/MIDI hardware paths remain
  hardware-verified only. (`host/`)

- **In-repo developer documentation** under `docs/`: `architecture.md`, `source-guide.md`,
  a firmware-tracked `manual.md`, and `review-260529.md` (a code/architecture review with
  verified and re-validated findings).

- **Host-only test harness** under `test/` (`make -C test test`). Compiles the pure-logic
  timing classes natively (no ARM toolchain, no hardware) and asserts their invariants. It
  does not build or affect the firmware. Initial coverage locks the two findings that
  required manual tracing: `Divider` triplet timing does not drift over long runs (S1), and
  `SynClock` emits exactly `_ticks_per_clock` internal ticks per external clock pulse for
  24->48 and 12->48 PPQN, including when the internal timer is over-driven (S3). Also covers
  the `Config` text parser (S5): valid parse, the `is_loaded()` default (V1), and the
  documented brittleness (mid-token whitespace stripping, unknown/overlong names ignored);
  the `wav.h` header builder/parser (canonical 44-byte layout, build->serialize->parse
  round-trip, rejection of short/non-RIFF input); and `SpeedMap` (semitone->speed table:
  octave/fifth anchors, end clamping, monotonicity). A `test/stubs/daisysp.h` shim supplies
  the few DaisySP symbols referenced by otherwise-pure headers (`fmap`, `fclamp`); and
  `Follower` (mean-square envelope follower: reset, steady-state convergence, output
  clamping, amp scaling, attack-faster-than-release); the `sample16` float<->int16
  conversion (round-trip, clamping, quantization step); `convert_pcm_block` (the load
  shim: f32<->i16 block conversion, clamping, and float->i16->float round-trip within a
  quantization step); and `PcmLoader` - a simulation of the SD audio-load accounting
  (frame counts, truncation to capacity, termination, contiguous placement) across every
  width combo and many chunk sizes, exercising the exact code card.cpp runs. 116 checks
  across 9 classes.

- **`LOFI_INT16` 16-bit loop buffer (default off, opt-in via `make LOFI_INT16=1`).** Stores
  the loop buffer as 16-bit PCM instead of 32-bit float, **doubling record time to 84 s in
  the same SDRAM** (verified: SDRAM footprint identical at both depths; `SRAM_EXEC` +304 B
  for the conversion code). Pieces:
  - `src/core/sample16.h` - pure, host-tested float<->int16 convert with hard clip.
  - `Buffer::Frame` becomes `int16_t` under the flag (`Buffer::Sample` alias); conversion
    is confined to the two element-wise sites, `Buffer::_read` and `Buffer::write` (the
    overdub mix stays in float, then clamps + quantizes). Identity / byte-identical codegen
    when the flag is off.
  - `kSourceMaxSeconds` 42 -> 84 under the flag; `wav.h` emits a 16-bit PCM header; the SD
    load path (`card.cpp`) accepts the matching depth and computes frames from the correct
    byte width.
  - `Makefile` gains a `LOFI_INT16=1` build option (mirrors `DEBUG=1`).
  Audio storage behaviour in the default build is byte-identical (the `int16` paths compile
  out); the default save format is still 32-bit float.

- **Convert-on-load shim (`src/core/pcm_convert.h`, wired into `card.cpp`).** The SD load
  path now accepts BOTH 16-bit PCM and 32-bit float files in either build and converts to
  the buffer's storage width on the fly (`convert_pcm_block`, pure + host-tested). So a
  16-bit firmware loads legacy float tapes and a float firmware loads 16-bit tapes -
  resolving the prior "rejects mismatched tapes" limitation; tapes are portable across
  builds. `kChunk` (32 KB) is a multiple of both sample widths and WAV data is
  sample-aligned, so chunks never straddle a sample (no carry buffer needed). Loading a
  longer tape than the buffer holds truncates to capacity. The streaming size/offset/
  termination math lives in a pure, host-tested helper (`src/core/pcm_loader.h`,
  `PcmLoader`) that card.cpp delegates to, so the accounting is validated off-target rather
  than by reasoning alone. Note: this adds the dual-format acceptance + converter to the
  **default** build too (~+150 B `SRAM_EXEC`), so the default firmware now also reads 16-bit
  tapes - a small, additive behaviour change. The audible overdub-quantization character of
  the 16-bit buffer remains unverified by ear. See `docs/lofi-int16-scope.md`.

- **Design scope docs** `docs/lofi-path-b-scope.md` (half-rate buffer) and
  `docs/lofi-int16-scope.md` (16-bit storage): inventories of the frame<->time/tempo
  coupling and the change sites for each lo-fi record-time approach.

### Fixed (portability)

- **`divider.h` now includes `<cmath>` for `std::round`** (was relying on `<math.h>`, which
  only places `round` in `std::` on the ARM toolchain, not in standard libc++). Surfaced by
  the new host harness; no effect on the firmware build. (`src/core/divider.h`)

- **`WavHeader::size` retyped `size_t` -> `uint32_t`.** The RIFF chunk-size field is a
  32-bit field per the WAV spec; `size_t` is 4 bytes only on the 32-bit target, so the
  44-byte layout (and its `static_assert`) held there but broke on a 64-bit host and would
  emit a malformed header on any 64-bit build. Layout-identical on-target (verified: the
  firmware binary is byte-for-byte unchanged in size); `wav.h` also now includes the
  `<cstdint>`/`<cstddef>`/`<cstring>` it uses rather than relying on its includers.
  Surfaced by the host harness. (`src/memory/wav.h`)

- **`follower.cpp` now includes `<cmath>` for `std::exp`** (was relying on `<math.h>`; same
  std-namespace portability issue as `divider.h`). Surfaced by the host harness; no effect
  on the firmware build. (`src/core/follower.cpp`)

## [1.0.2]

Baseline release prior to this changelog. See Git history for details.
