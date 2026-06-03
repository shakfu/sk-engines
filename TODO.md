# TODO

Deferred work, newest first. See `docs/` for the platform/engine design and `CHANGELOG.md` for done work.

## Move the app-composition tier into `src/` (root hygiene)

The repo root currently holds `main.cpp`, `app.h`, `app.cpp`, and `meter.h`; everything else is under `src/`. Tighten this so the root is *just* the entry point: move `app.h`, `app.cpp`, and `meter.h` into `src/` (keep `main.cpp` at root as the thin entry, or move it too and repoint the Makefile - decide then). Same motivation as the `common.h` root->`src/` move: a coherent "library lives in `src/`" rule, and it removes the latent `-Isrc` fallback smell (e.g. `meter.h` includes `"nocopy.h"`, which lives in `src/` and currently only resolves from root via `-Isrc`; masked today because `METER` is normally off).

Touches: Makefile `CPP_SOURCES` (`app.cpp` -> `src/app.cpp`); `main.cpp`'s `#include "app.h"` (resolves to `src/app.h` via `-Isrc`, or make it explicit). No behaviour change - pure relocation; verify by build.

Notes:

- `build/app.o` (the engine-stamp dependency) is unaffected - libDaisy flattens objects to `build/` by basename regardless of source directory.

- `app.cpp` stays the composition root (it includes `engine_select.h` -> the concrete engine) and stays exempt from `check-boundary`, which greps only `src/hw src/ui src/memory` - `src/app.cpp` is not in those, so the guard is unaffected.

## Evaluate converting the build to CMake (Make as a thin frontend)

Nothing requires Make - the only hard dependency is libDaisy's build infrastructure, and the `bleeptools` fork ships both (`lib/libDaisy/cmake/DaisyProject.cmake`, `lib/DaisySP/CMakeLists.txt`).

So this is a cost/benefit choice, to be done as a deliberate spike on a branch, NOT a blind migration of a working hardware-critical boot path.

What CMake would buy this project specifically:

- `compile_commands.json` natively (currently produced via `bear -- make`; see below).

- **Per-target include roots** (`target_include_directories(... PRIVATE)`) - the compiler-enforced version of the platform/engine boundary the R4b grep-guard now enforces by convention. This is CMake's native model; in libDaisy's monolithic `core/Makefile` (one global `C_INCLUDES`) it is a fight.

- Native static-lib granular (`add_library(... STATIC)`) and a clean multi-engine build matrix (one build dir per engine, cached, side-by-side) - retiring the `.engine-stamp` clean/rebuild hack.

- One build system for firmware + host (currently two separate Makefiles).

Gating unknowns / gaps to resolve in the spike (do NOT migrate before confirming):

- **Boot path parity.** This project uses `APP_TYPE=BOOT_SRAM` + the custom `alt_sram.lds` + the SRAM bootloader. `DaisyProject.cmake` supports a custom linker script (`CUSTOM_LINKER_SCRIPT`) and SRAM storage (`DAISY_STORAGE=sram`), so the linker side looks covered - but `BOOT_SRAM` startup/vector parity vs `DAISY_STORAGE=sram` is unverified.

- **No `program-dfu`/`program-boot` in the CMake** - recreate as `add_custom_target`s wrapping `dfu-util`.

- The fork's CMake path is not exercised today (Make is the proven path).

Acceptance for adopting it: a CMake build whose `.map` / `arm-none-eabi-objdump` output matches the Make build's (codegen parity, since correctness is only verifiable by flashing), then a hardware flash test. If it passes, adopt with a thin wrapper `Makefile` so `make` / `make program-dfu` muscle memory survives, and unify the host build. If the boot path fights it, stay on Make.

Worth doing only if committing to the compiler-enforced boundary + multi-engine growth; not worth it for aesthetics alone (the grep-guard already enforces the boundary, `bear` already gives clangd its flags).

## Grow `src/dsp/` with the remaining shared primitives

`src/dsp/` (the engine-agnostic primitive tier, dependency flows platform/engine -> dsp, never the reverse) was seeded in Phase 5 R4 with the header-only batch: `lutsinosc.h`, `smooth.h`, `deline.h`, `hann.h`. Two follow-ups remain:

- **Move the `.cpp`-bearing generic primitives** currently under `src/engine/granular/`: `biquad.{h,cpp}` (filter), `follower.{h,cpp}` (envelope follower), `adenv.{h,cpp}` (AD envelope), `cpattern.{h,cpp}`.

  They're self-contained (no granular siblings) but deferred because each needs build wiring: their `.cpp` must move from granular's `$(wildcard src/engine/granular/*.cpp)` into the **global** `CPP_SOURCES` (a new `$(wildcard src/dsp/*.cpp)`) so every engine links them. Verify that doesn't
  regress SRAM and that non-granular builds still link cleanly.

- **Refactor the delay engine to use the shared primitives** instead of its hand-rolled copies: the delay reimplemented one-pole smoothing and a fractional delay line, which now live in `dsp/smooth.h` and `dsp/deline.h`. This is the concrete second consumer that justified the tier - but it CHANGES the delay's DSP (its smoothing/interpolation may not be bit-identical to the shared versions), so do it deliberately with a hardware flash test, not as a silent swap.

Principle going forward: a primitive earns its place in `dsp/` when it gets a real second consumer, not preemptively.

## Mono-input normalization (left -> right when right is unused)

When only the left input is patched, mirror it to the right so a mono source feeds both channels (e.g. the stereo delay's two taps both get signal instead of the right tap going silent).

Open question — how to detect "right input not used":

- **Hardware normalling** (preferred if the board supports it): the right input jack normals to the left when nothing is plugged in, so it's automatic and the firmware does nothing. Check whether the Spotykach audio input jacks are physically normalled; if so, this TODO is moot.

- **Software fallback** (if not normalled): detect a near-silent right input (peak below a small threshold over a window) and copy left -> right. Needs hysteresis/timing so it doesn't flap, and a decision on where it lives — it's a *platform* input concern (applies to any engine), so it likely belongs in the platform's audio path (e.g. `AppImpl::ProcessAudio` before `engine.process`), not in an individual engine. Caveat: silence-detection can't tell "cable plugged but quiet" from "no cable".

Raised while testing the stereo delay (engine #2): a mono source into the left input left the right delay tap silent.
