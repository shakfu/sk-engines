# TODO

Deferred work, in priority order (highest first). See `docs/` for the platform/engine design and `CHANGELOG.md` for done work.

Priority is driven less by size than by what unblocks/gates what, and by whether an item is **build-verifiable on the host** vs. **hardware-gated** (needs a flash to verify). Two natural batches fall out: do the build-only items now; batch the hardware-gated items into one bench session.

| # | Item | Effort | Risk | Verify | Gating |
|---|------|--------|------|--------|--------|
| P1 | Mono-input: answer the normalling question | trivial | n/a | a fact | unblocks/kills P1-code |
| P2 | Faust DSP on hardware: CPU + voicing (reverb J-A, tape FX) | low | med | **hardware flash** | gates reverb + tape-FX merge to `main` |
| P3 | Refactor delay engine onto shared primitives | med | med-high | **hardware flash** | none (primitives now in `dsp/`) |
| P4 | Convert the build to CMake (spike done; flash-gated) | high | high | flash (host parity met) | strategic intent |
| P5 | Fix tape engine GenerativeStereo re-roll-every-loop bug | trivial | low | host build + **flash** (by ear) | none (do now with build-only batch) |
| P6 | Tape wow/flutter rate: try quadratic curve + lower maximums | trivial | low | **flash** (by ear) | none (optional voicing experiment) |

Hardware-batch note: the Faust DSP CPU/voicing check (P2), the delay refactor (P3), the mono-input *software fallback* (if P1 says "not normalled"), and the still-outstanding Phase-5 R4a granular-path flash-verify are all hardware-gated - do them together in one bench session.

---

## P1 - Mono-input normalization (left -> right when right is unused)

Highest-leverage *decision* before any code: answering one hardware fact either kills this item or scopes it. The fix itself (raised while testing the stereo delay, engine #2: a mono source into the left input left the right delay tap silent) is to mirror left -> right so a mono source feeds both channels.

**Resolve first - is the right input jack physically normalled to the left?** (Not answerable from the repo; needs the board schematic or a bench check.)

- **Hardware normalling** (preferred if the board supports it): the right input jack normals to the left when nothing is plugged in - automatic, firmware does nothing, and **this whole item is moot** (delete it).

- **Software fallback** (only if NOT normalled - and then this code is hardware-gated, batch with P3): detect a near-silent right input (peak below a small threshold over a window) and copy left -> right. Needs hysteresis/timing so it doesn't flap, and it's a *platform* input concern (applies to any engine), so it belongs in the platform's audio path (e.g. `AppImpl::ProcessAudio` before `engine.process`), not in an individual engine. Caveat: silence-detection can't tell "cable plugged but quiet" from "no cable".

## P2 - Faust DSP on hardware: CPU + voicing (HARDWARE-GATED)

The reverb (`reverb`) and tape-FX (`tape`) engines are built, host-tested, and fit, but their Jiles-Atherton hysteresis and FDN/plate reverb DSP have **never run on the H7** - the host tests confirm wiring/bounds/finiteness, not real-time CPU or musicality. This gates merging that work to `main`. In one bench session:

- **CPU.** Flash `ENGINE=reverb` and `ENGINE=tape` and read `Meter::cpu` for the stereo paths. The J-A runs 4 substeps/sample (~1 `tanh` + ~4 divisions each) x 2 voices/decks; estimated ~10-25% of 480 MHz but unmeasured. If too hot, the levers are: swap `ma.tanh` for a polynomial Langevin approx, or fall back to an ADAA-tanh saturator (tape). For reverb, only add a third algorithm after measuring (`SRAM_EXEC` is ~92% with two).

- **Voicing (by ear).** Tape saturation is intentionally subtle (the lib's -50 dB calibration); confirm the `drive*54` dB range + the `Ms/a/alpha/k` ferromagnetic params give a usable clean->crunch sweep, and that wow/flutter rate/depth feel right. Reverb: confirm the Dattorro plate / Zita hall sound correct and the Alt+PITCH algorithm switch is click-free.

Voicing levers live in the `.dsp` sources (`src/engine/{reverb,tape}/*.dsp`); re-tune and `make faust-gen`. See `docs/engines/{reverb,tape}.md`. On pass, this unblocks the reverb + tape-FX merge to `main`.

## P3 - Refactor the delay engine onto the shared primitives (HARDWARE-GATED)

The shared primitives are now in `dsp/` (the `.cpp` tier move is done), so the prerequisite is satisfied. This is the concrete second consumer that justified the tier: the delay reimplemented one-pole smoothing and a fractional delay line, which now live in `src/dsp/smooth.h` and `src/dsp/deline.h`. The recent "restructured delay engine" commits reorganized the engine internals but deliberately kept both primitives inline (`delay_engine.cpp`: smoothing at `103-107`, linear fractional read at `123-125`; no `dsp/` include). It **CHANGES the delay's DSP** - the shared versions are *not* bit-identical drop-ins, confirmed by inspection:

- **Smoothing divergence.** The inline glide (`s_delay += (target - s_delay) * 0.0015f`, every sample, no dead-zone, never snaps) differs from `OnePoleSmoother` (`smooth.h`), which adds a `.002f` dead-zone short-circuit (`:26`) and a snap-to-target within `.002f` (`:31-34`). The coefficient is matchable (`_kof = 1/(time_s*sr)` -> `time_s ~= 0.0139 s` to equal `0.0015` at 48 kHz) but the dead-zone/snap changes the trajectory. Since the delay smooths the *delay time itself*, that snap is audible as a different pitch-glide on knob moves.

- **Structural mismatch on the delay line.** `DeLine` (`deline.h`) uses the same `a + (b-a)*frac` interpolation but is a **fixed-size template** (`max_size` is a compile-time `size_t`) with a decrementing write pointer + modulo wrap. The delay engine allocates a **runtime-sized** buffer from the arena (`len`, `max_d`, `kReadHeadroom`, `delay_engine.cpp:64`) with a forward-indexed read pointer. Adopting `DeLine` requires resolving fixed-vs-runtime sizing, not just a numeric swap.

So do it deliberately with a hardware flash test (judge by ear, not by bit-identity - see P3's note), not a silent swap. Batch with the other hardware-gated items (see top note).

## P4 - Evaluate converting the build to CMake (Make as a thin frontend)

Lowest priority: a deliberate spike, not a task, and explicitly *not worth it for aesthetics alone*

- the grep-guard already enforces the boundary and `bear` already gives clangd its flags. Worth doing only if committing to the compiler-enforced boundary + multi-engine growth. Do it as a spike on a branch, NOT a blind migration of a working hardware-critical boot path.

**Status: spike done 2026-06-05 (branch `spike/cmake-build`, unmerged, host-only).** A working CMake build was produced for the granular and passthrough engines; the boot-path risk collapsed from "unknown" to one known define. The only remaining gate is the hardware flash. See "Spike results" below.

Nothing requires Make - the only hard dependency is libDaisy's build infrastructure, which ships both CMake and Make paths (`lib/libDaisy/cmake/DaisyProject.cmake`, `lib/DaisySP/CMakeLists.txt`). That CMake support is inherited from electro-smith upstream, not a `bleeptools`/infrasonicaudio fork addition (the fork delta is MIDI/logger/storage/QSPI, not the build system).

What CMake would buy this project specifically:

- `compile_commands.json` natively (currently produced via `bear -- make`; see below).

- **Per-target include roots** (`target_include_directories(... PRIVATE)`) - the compiler-enforced version of the platform/engine boundary the R4b grep-guard now enforces by convention. This is CMake's native model; in libDaisy's monolithic `core/Makefile` (one global `C_INCLUDES`) it is a fight.

- Native static-lib granular (`add_library(... STATIC)`) and a clean multi-engine build matrix (one build dir per engine, cached, side-by-side) - retiring the `.engine-stamp` clean/rebuild hack.

- One build system for firmware + host (currently two separate Makefiles).

### Spike results (2026-06-05, branch `spike/cmake-build`)

The spike is a single root `CMakeLists.txt` that drives libDaisy's upstream `DaisyProject.cmake`. Each former gating unknown is now resolved or characterized:

- **Boot path: resolved, and smaller than feared.** `BOOT_APP` is the only boot-relevant compile define and it appears in exactly one file - `startup_stm32h750xx.c:1550`, where `#ifndef BOOT_APP` gates the `SystemInit()` call. The Make build defines it (via `APP_TYPE=BOOT_SRAM`) so a bootloaded app skips the second `SystemInit()` the SRAM bootloader already ran. `DaisyProject.cmake` does **not** define it when `CUSTOM_LINKER_SCRIPT` is set, so a naive port re-runs `SystemInit()` and likely will not boot. Fix is one line: `target_compile_definitions(daisy PRIVATE BOOT_APP)` (safe - the macro is referenced nowhere else). `DAISY_STORAGE=sram` is not used; we pass `CUSTOM_LINKER_SCRIPT=alt_sram.lds` directly.

- **Two more traps a blind migration hits (both fixed in the spike):**

  - `USE_HAL_DRIVER`/`USE_FULL_LL_DRIVER` are global `C_DEFS` in Make but only **PRIVATE** on the daisy CMake lib, so they never reach app TUs. Without `USE_HAL_DRIVER` the force-included `stm32h7xx.h` skips the HAL chain that drops `<stddef.h>` (`::size_t`) into global scope, and headers using bare `size_t` (e.g. `detector.h:11`) fail to compile. Must add both to the firmware target.

  - `hid/midi_util.cpp` (`daisy::MidiTxMessage`) is in libDaisy's **Make** module list but absent from its `CMakeLists.txt` `add_library(daisy ...)` - the fork's CMake path was never exercised, so the app fails to link. Patched via `target_sources(daisy ...)` without editing the submodule. (`per/pwm.cpp` is likewise Make-only but unused - the app drives PWM through the HAL TIM API, not `daisy::Pwm` - so it is correctly omitted.)

- **`program-dfu`/`program-boot`: done** as `add_custom_target`s wrapping `dfu-util`, emitting byte-identical invocations to Make (QSPI `0x90040000:leave`, PID `df11`). `program-dfu` also DEPENDS on the firmware target (build-then-flash in one step, slightly better than Make), and a `POST_BUILD` objcopy emits the `.bin` (DaisyProject does not by default). It cannot put the board into DFU mode - same physical hold-Reset limitation as Make.

- **Parity achieved: memory map exact, codegen ~1%, byte-identical objdump NOT reachable.** With opt levels matched per domain (libDaisy/DaisySP at `-O3/gnu++14`, app at `-O2/c++17 -fshort-enums`), every boot-critical address is identical (vector table `0x24000000`, `.text` start `0x24000298`, `.bss` `0x2402e800`, all sram1/sdram/qspi regions) and `.bss` is byte-identical. `.text` lands within +1.0% (186420 vs 184556 B) and `.data` +972 B. The residual cannot be closed without patching the vendored libDaisy `CMakeLists.txt` (Make's lib TUs also carry codegen-neutral `-fasm`/`-Wno-register` and compile C at `gnu11`). **Note:** `-fshort-enums` is app-only in the Make build (libDaisy/DaisySP do not use it) - an ABI asymmetry that ships and is hardware-proven, replicated here for parity.

- **Other claimed benefits validated:** `compile_commands.json` falls out natively (no `bear`); the multi-engine matrix works as separate cached build dirs (`build-cmake/<engine>/`), retiring the `.engine-stamp` hack.

- **Thin Make frontend prototyped** (`Makefile.cmake`): preserves the muscle-memory commands (`make`, `make ENGINE=passthrough`, `make program-dfu`, `make engine-edrums`, `make clean`, `make DEBUG=1`) by delegating to CMake, with one cached build dir per engine and the grep boundary-guard carried over. On adoption it is renamed to `Makefile`. Verified end-to-end host-side (build + engine switch + DFU command); kept as a separate file on the spike branch so the proven `Makefile` stays intact.

- **All four engines build host-side.** `granular`, `passthrough`, `delay`, and `edrums` each compile and link under CMake with no source changes (the `CMakeLists` engine switch mirrors the Makefile's). Host build success only - none are flash-verified.

- **Incidental Make bug found (resolved on `main`):** `Makefile:52` set `C_USR_FLAGS` but libDaisy's core Makefile reads `C_USER_FLAGS` (with the E), so `-ffast-math -funroll-loops` were silently dropped

  - the live firmware had neither. Fixed on `main` (dead line removed with a documenting comment, verified a host no-op), separately from this branch.

**Revised acceptance** (the original "`.map`/objdump matches" bar is unreachable across two build systems that compile the library halves with different flags; do not chase byte-identity): adopt iff a hardware flash of the CMake `.bin` boots and passes an audio/IO smoke test on the bench, with the memory-map + bss parity above as the host-side precondition (met). If the boot path fights it on hardware, stay on Make. On pass, work the adoption tail below.

### Adoption tail (do NOT call this "adopted" until these are closed)

A green flash is necessary but not sufficient. Items 1-4 are independent of the flash; item 5 is the flash gate itself. The spike branch must NOT be merged with all three build-system files straddling `main` - decide adoption first.

1. **Collapse the engine-list duplication.** The engine list lives in three places today: the old `Makefile`, `CMakeLists.txt`, and `Makefile.cmake`. On adoption, delete the old `Makefile`, rename `Makefile.cmake` -> `Makefile`; the list then lives only in `CMakeLists.txt` and the wrapper just forwards `ENGINE=`. Leaving all three on `main` is the main reason not to merge this branch as-is.

2. **Decide the `midi_util.cpp` fix: upstream vs local patch.** The spike papers over libDaisy's CMake gap with `target_sources(daisy ...)` in our root `CMakeLists`. Either PR the missing source into the bleeptools libDaisy fork's `CMakeLists.txt` (clean, but a submodule/upstream change) or keep the local patch (self-contained, but a future libDaisy bump could add the file and double-compile, or move it and break the patch). Same call for `per/pwm.cpp` if any engine ever uses `daisy::Pwm`.

3. **Unify the host build.** `host/` still has its own Makefile; folding the desktop harness into CMake delivers the stated "one build system for firmware + host" benefit. Not attempted in the spike.

4. **Build the compiler-enforced boundary (the actual headline justification).** The spike carries the grep-guard verbatim; it does NOT yet implement the per-target include roots (`target_include_directories(... PRIVATE)`) that turn a platform->engine include into a compile error instead of a grep hit. That needs the engine and platform split into separate targets with private includes - the real reason to adopt at all (see the top of this item); without it, CMake is only the aesthetic win this item explicitly says is not worth it.

5. **Flash-verify each image you intend to run.** All four engines build host-side, but only a bench flash confirms boot + audio/IO per image. This is the acceptance gate above.

## P5 - Tape engine GenerativeStereo re-rolls its random pans every loop

`TapeEngine::set_config` (`src/engine/tape/tape_engine.cpp:148-156`) re-rolls the random pans on **every** call while in GenerativeStereo:

```cpp
if (_route == Route::GenerativeStereo) _roll_random_pans();
```

But the platform calls `set_config(ConfigId::Route, ...)` **every loop** (`src/ui/core.ui.cpp:632`), so the "random pan per deck" is re-randomized continuously instead of once on entry - contradicting the engine's own doc comment ("re-rolled on entering the mode", `tape_engine.h:29`). The result is jittering pans rather than a stable random placement.

**Fix:** guard the action on an actual route transition, exactly as the shuttle engine now does (`src/engine/shuttle/shuttle_engine.cpp` `set_config` - track the previous `Route` and only re-roll / recompute when it changes). Found 2026-06-10 while wiring the routing switch + per-track pan into the shuttle engine; shuttle was written with the guard, tape was left as-is to keep the change scoped.

Build-verifiable (compiles + the logic is inspectable), but the audible "pans no longer jitter" confirmation needs a flash - fold the code fix into the build-only batch now, confirm by ear in the next bench session.

## P6 - Tape wow/flutter rate: experiment with a quadratic curve and lower maximums

Optional voicing tweak, not a defect. The MODFREQ ("cycle") knob -> wow/flutter rate map in `src/engine/tape/tapefx.dsp` was changed 2026-06-11 from a linear map with too-high a floor (`0.5 .. 2.5 Hz` wow / `6 .. 12 Hz` flutter) to a **cubic** curve with a low floor:

```
rc    = rate * rate * rate; // favor very low frequencies, increase slowly
wowHz = 0.1 + rc * 2.4;     // 0.1 .. 2.5 Hz
fltHz = 0.5 + rc * 11.5;    // 0.5 .. 12 Hz
```

This is good enough as-is (the lowest levels are now a slow drift rather than a fast warble), but it's worth experimenting with two softer variants:

- **Quadratic instead of cubic** (`rc = rate * rate`): a gentler favor-low. Cubic keeps the rate very slow until ~0.7 of knob travel, which may push the usable fast-wobble range too far up; quadratic spreads it out more evenly.

- **Lower the maximums somewhat**: drop the `2.4` / `11.5` multipliers so the top of the knob tops out below the current 2.5 Hz wow / 12 Hz flutter, if even the maxima feel too fast.

Levers are the three lines above; re-tune and `make faust-gen` (regenerates `faust_kernel_tapefx.h`), then evaluate by ear on hardware. Purely subjective, so it's flash-gated (no host test can judge it) and low priority - fold into the next bench session alongside the P2 tape voicing pass. See `docs/engines/tape.md`.
