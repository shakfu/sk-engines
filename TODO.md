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
| P2 | Move `.cpp` primitives into `src/dsp/` | low-med | low-med | build + `check-boundary` | none |
| P3 | Refactor delay engine onto shared primitives | med | med-high | **hardware flash** | needs P2 |
| P4 | Evaluate converting the build to CMake | high | high | parity + flash | strategic intent |

Hardware-batch note: P3, the mono-input *software fallback* (if P1 says "not normalled"), and the
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
  P3): detect a near-silent right input (peak below a small threshold over a window) and copy
  left -> right. Needs hysteresis/timing so it doesn't flap, and it's a *platform* input concern
  (applies to any engine), so it belongs in the platform's audio path (e.g. `AppImpl::ProcessAudio`
  before `engine.process`), not in an individual engine. Caveat: silence-detection can't tell "cable
  plugged but quiet" from "no cable".

## P2 - Grow `src/dsp/` with the remaining shared primitives (move .cpp tier)

Build-verifiable, continues committed Phase-5 work, and is the prerequisite for P3.

`src/dsp/` (the engine-agnostic primitive tier, dependency flows platform/engine -> dsp, never the
reverse) was seeded in Phase 5 R4 with the header-only batch: `lutsinosc.h`, `smooth.h`, `deline.h`,
`hann.h`.

**Move the `.cpp`-bearing generic primitives** currently under `src/engine/granular/`:
`biquad.{h,cpp}` (filter), `follower.{h,cpp}` (envelope follower), `adenv.{h,cpp}` (AD envelope),
`cpattern.{h,cpp}`. They're self-contained (no granular siblings) but deferred because each needs
build wiring: their `.cpp` must move from granular's `$(wildcard src/engine/granular/*.cpp)` into
the **global** `CPP_SOURCES` (a new `$(wildcard src/dsp/*.cpp)`) so every engine links them. The
granular build's codegen should be unchanged (pure relocation); verify that the **non-granular**
builds (passthrough/delay) still link cleanly and don't regress SRAM now that they also link these.

Principle going forward: a primitive earns its place in `dsp/` when it gets a real second consumer,
not preemptively.

## P3 - Refactor the delay engine onto the shared primitives (HARDWARE-GATED)

Depends on P2 (primitives must be in `dsp/`). This is the concrete second consumer that justified
the tier: the delay reimplemented one-pole smoothing and a fractional delay line, which now live in
`dsp/smooth.h` and `dsp/deline.h`. But it **CHANGES the delay's DSP** (its smoothing/interpolation
may not be bit-identical to the shared versions), so do it deliberately with a hardware flash test,
not a silent swap. Batch with the other hardware-gated items (see top note).

## P4 - Evaluate converting the build to CMake (Make as a thin frontend)

Lowest priority: a deliberate spike, not a task, and explicitly *not worth it for aesthetics alone*
- the grep-guard already enforces the boundary and `bear` already gives clangd its flags. Worth
doing only if committing to the compiler-enforced boundary + multi-engine growth. Do it as a spike
on a branch, NOT a blind migration of a working hardware-critical boot path.

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

Gating unknowns / gaps to resolve in the spike (do NOT migrate before confirming):

- **Boot path parity.** This project uses `APP_TYPE=BOOT_SRAM` + the custom `alt_sram.lds` + the
  SRAM bootloader. `DaisyProject.cmake` supports a custom linker script (`CUSTOM_LINKER_SCRIPT`) and
  SRAM storage (`DAISY_STORAGE=sram`), so the linker side looks covered - but `BOOT_SRAM`
  startup/vector parity vs `DAISY_STORAGE=sram` is unverified.

- **No `program-dfu`/`program-boot` in the CMake** - recreate as `add_custom_target`s wrapping
  `dfu-util`.

- The fork's CMake path is not exercised today (Make is the proven path).

Acceptance for adopting it: a CMake build whose `.map` / `arm-none-eabi-objdump` output matches the
Make build's (codegen parity, since correctness is only verifiable by flashing), then a hardware
flash test. If it passes, adopt with a thin wrapper `Makefile` so `make` / `make program-dfu` muscle
memory survives, and unify the host build. If the boot path fights it, stay on Make.
