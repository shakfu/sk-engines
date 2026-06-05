# TODO

Deferred work, in priority order (highest first). See `docs/` for the platform/engine
design and `CHANGELOG.md` for done work.

Priority is driven less by size than by what unblocks/gates what, and by whether an item is
**build-verifiable on the host** vs. **hardware-gated** (needs a flash to verify). Two natural
batches fall out: do the build-only items now; batch the hardware-gated items into one bench
session.

| # | Item | Effort | Risk | Verify | Gating |
|---|------|--------|------|--------|--------|
| P1 | Mono-input: answer the normalling question | trivial | n/a | a fact | unblocks/kills P1-code |
| P2 | Refactor delay engine onto shared primitives | med | med-high | **hardware flash** | none (primitives now in `dsp/`) |
| P3 | Convert the build to CMake (spike done; flash-gated) | high | high | flash (host parity met) | strategic intent |

Hardware-batch note: P2, the mono-input *software fallback* (if P1 says "not normalled"), and the
still-outstanding Phase-5 R4a granular-path flash-verify are all hardware-gated - do them together
in one bench session.

---

## P1 - Mono-input normalization (left -> right when right is unused)

Highest-leverage *decision* before any code: answering one hardware fact either kills this item or
scopes it. The fix itself (raised while testing the stereo delay, engine #2: a mono source into the
left input left the right delay tap silent) is to mirror left -> right so a mono source feeds both
channels.

**Resolve first - is the right input jack physically normalled to the left?** (Not answerable from
the repo; needs the board schematic or a bench check.)

- **Hardware normalling** (preferred if the board supports it): the right input jack normals to the
  left when nothing is plugged in - automatic, firmware does nothing, and **this whole item is
  moot** (delete it).

- **Software fallback** (only if NOT normalled - and then this code is hardware-gated, batch with
  P2): detect a near-silent right input (peak below a small threshold over a window) and copy
  left -> right. Needs hysteresis/timing so it doesn't flap, and it's a *platform* input concern
  (applies to any engine), so it belongs in the platform's audio path (e.g. `AppImpl::ProcessAudio`
  before `engine.process`), not in an individual engine. Caveat: silence-detection can't tell "cable
  plugged but quiet" from "no cable".

## P2 - Refactor the delay engine onto the shared primitives (HARDWARE-GATED)

The shared primitives are now in `dsp/` (the `.cpp` tier move is done), so the prerequisite is satisfied. This is the concrete second consumer that justified the tier: the delay reimplemented one-pole smoothing and a fractional delay line, which now live in `dsp/smooth.h` and `dsp/deline.h`.
But it **CHANGES the delay's DSP** (its smoothing/interpolation may not be bit-identical to the shared versions), so do it deliberately with a hardware flash test, not a silent swap. Batch with the other hardware-gated items (see top note).

## P3 - Evaluate converting the build to CMake (Make as a thin frontend)

Lowest priority: a deliberate spike, not a task, and explicitly *not worth it for aesthetics alone*

- the grep-guard already enforces the boundary and `bear` already gives clangd its flags. Worth
doing only if committing to the compiler-enforced boundary + multi-engine growth. Do it as a spike
on a branch, NOT a blind migration of a working hardware-critical boot path.

**Status: spike done 2026-06-05 (branch `spike/cmake-build`, unmerged, host-only).** A working CMake
build was produced for the granular and passthrough engines; the boot-path risk collapsed from
"unknown" to one known define. The only remaining gate is the hardware flash. See "Spike results"
below.

Nothing requires Make - the only hard dependency is libDaisy's build infrastructure, which ships
both CMake and Make paths (`lib/libDaisy/cmake/DaisyProject.cmake`, `lib/DaisySP/CMakeLists.txt`).
That CMake support is inherited from electro-smith upstream, not a `bleeptools`/infrasonicaudio
fork addition (the fork delta is MIDI/logger/storage/QSPI, not the build system).

What CMake would buy this project specifically:

- `compile_commands.json` natively (currently produced via `bear -- make`; see below).

- **Per-target include roots** (`target_include_directories(... PRIVATE)`) - the compiler-enforced
  version of the platform/engine boundary the R4b grep-guard now enforces by convention. This is
  CMake's native model; in libDaisy's monolithic `core/Makefile` (one global `C_INCLUDES`) it is a
  fight.

- Native static-lib granular (`add_library(... STATIC)`) and a clean multi-engine build matrix (one
  build dir per engine, cached, side-by-side) - retiring the `.engine-stamp` clean/rebuild hack.

- One build system for firmware + host (currently two separate Makefiles).

### Spike results (2026-06-05, branch `spike/cmake-build`)

The spike is a single root `CMakeLists.txt` that drives libDaisy's upstream `DaisyProject.cmake`.
Each former gating unknown is now resolved or characterized:

- **Boot path: resolved, and smaller than feared.** `BOOT_APP` is the only boot-relevant compile
  define and it appears in exactly one file - `startup_stm32h750xx.c:1550`, where `#ifndef BOOT_APP`
  gates the `SystemInit()` call. The Make build defines it (via `APP_TYPE=BOOT_SRAM`) so a bootloaded
  app skips the second `SystemInit()` the SRAM bootloader already ran. `DaisyProject.cmake` does
  **not** define it when `CUSTOM_LINKER_SCRIPT` is set, so a naive port re-runs `SystemInit()` and
  likely will not boot. Fix is one line: `target_compile_definitions(daisy PRIVATE BOOT_APP)` (safe -
  the macro is referenced nowhere else). `DAISY_STORAGE=sram` is not used; we pass
  `CUSTOM_LINKER_SCRIPT=alt_sram.lds` directly.

- **Two more traps a blind migration hits (both fixed in the spike):**
  - `USE_HAL_DRIVER`/`USE_FULL_LL_DRIVER` are global `C_DEFS` in Make but only **PRIVATE** on the
    daisy CMake lib, so they never reach app TUs. Without `USE_HAL_DRIVER` the force-included
    `stm32h7xx.h` skips the HAL chain that drops `<stddef.h>` (`::size_t`) into global scope, and
    headers using bare `size_t` (e.g. `detector.h:11`) fail to compile. Must add both to the firmware
    target.
  - `hid/midi_util.cpp` (`daisy::MidiTxMessage`) is in libDaisy's **Make** module list but absent
    from its `CMakeLists.txt` `add_library(daisy ...)` - the fork's CMake path was never exercised, so
    the app fails to link. Patched via `target_sources(daisy ...)` without editing the submodule.
    (`per/pwm.cpp` is likewise Make-only but unused - the app drives PWM through the HAL TIM API, not
    `daisy::Pwm` - so it is correctly omitted.)

- **`program-dfu`/`program-boot`: done** as `add_custom_target`s wrapping `dfu-util`, emitting
  byte-identical invocations to Make (QSPI `0x90040000:leave`, PID `df11`). `program-dfu` also DEPENDS
  on the firmware target (build-then-flash in one step, slightly better than Make), and a `POST_BUILD`
  objcopy emits the `.bin` (DaisyProject does not by default). It cannot put the board into DFU mode -
  same physical hold-Reset limitation as Make.

- **Parity achieved: memory map exact, codegen ~1%, byte-identical objdump NOT reachable.** With opt
  levels matched per domain (libDaisy/DaisySP at `-O3/gnu++14`, app at `-O2/c++17 -fshort-enums`),
  every boot-critical address is identical (vector table `0x24000000`, `.text` start `0x24000298`,
  `.bss` `0x2402e800`, all sram1/sdram/qspi regions) and `.bss` is byte-identical. `.text` lands within
  +1.0% (186420 vs 184556 B) and `.data` +972 B. The residual cannot be closed without patching the
  vendored libDaisy `CMakeLists.txt` (Make's lib TUs also carry codegen-neutral `-fasm`/`-Wno-register`
  and compile C at `gnu11`). **Note:** `-fshort-enums` is app-only in the Make build (libDaisy/DaisySP
  do not use it) - an ABI asymmetry that ships and is hardware-proven, replicated here for parity.

- **Other claimed benefits validated:** `compile_commands.json` falls out natively (no `bear`); the
  multi-engine matrix works as separate cached build dirs (`build-cmake/<engine>/`), retiring the
  `.engine-stamp` hack.

- **Thin Make frontend prototyped** (`Makefile.cmake`): preserves the muscle-memory commands (`make`,
  `make ENGINE=passthrough`, `make program-dfu`, `make engine-edrums`, `make clean`, `make DEBUG=1`)
  by delegating to CMake, with one cached build dir per engine and the grep boundary-guard carried
  over. On adoption it is renamed to `Makefile`. Verified end-to-end host-side (build + engine switch +
  DFU command); kept as a separate file on the spike branch so the proven `Makefile` stays intact.

- **Incidental Make bug found (independent of CMake):** `Makefile:52` sets `C_USR_FLAGS` but libDaisy's
  core Makefile reads `C_USER_FLAGS` (with the E), so `-ffast-math -funroll-loops` are silently dropped
  - the live firmware has neither. Fix the typo or delete the dead config.

**Revised acceptance** (the original "`.map`/objdump matches" bar is unreachable across two build
systems that compile the library halves with different flags; do not chase byte-identity): adopt iff
a hardware flash of the CMake `.bin` boots and passes an audio/IO smoke test on the bench, with the
memory-map + bss parity above as the host-side precondition (met). On pass, adopt by renaming the
prototyped `Makefile.cmake` over `Makefile` so `make` / `make program-dfu` muscle memory survives, and
unify the host build. If the boot path fights it on hardware, stay on Make.
