# ChucK on the Daisy Pod — debugging the boot crash (session handoff)

> **Pick-up doc (2026-06-19).** The `ENGINE=chuck` spotykach firmware builds + links but **does not
> boot on hardware** — the panel goes solid-white. We localized the fault to **ChucK's runtime init**
> (`new ChucK()` / `ChucK::init()`), and the plan is to reproduce + debug it on a **bare Daisy Pod**
> (visible onboard LED + accessible SWD pads + a minimal pure-ChucK harness). This doc is the state +
> the exact next steps. Background: [`chuck-impl.md`](chuck-impl.md) (roadmap), [`csound-impl.md`](csound-impl.md)
> (the proven sibling QSPI engine — the model for everything here).

## Where we are

M0–M2 **code** is written and **links** (engine, Pod harness, Makefile wiring, linker script). The
gap is purely **on-hardware boot of the QSPI app**. See `chuck-impl.md` for the full M0–M2 summary.

Files added/changed this session:
- `src/engine/chuck/chuck_engine.{h,cpp}` — `ChuckEngine : IEngine` (built-in `kProgram` drone reading
  `speedA`/`mixA`/`sizeA`; interleave→`ck->run()`→de-interleave; level meter render). Has a bring-up
  **bisection** (`CHUCK_RUNTIME_LEVEL` 0..3) and a `_ready` gate so partial-init builds stay silent.
- `src/engine/chuck/chuck_alloc.cpp` — 12 MB `.sdram_bss` `--wrap` pool (reuses `CsoundPool`).
- `pod/harness_chuck.cpp` + `pod/Makefile.chuck` — the bare-Pod vehicle, now with a **visible
  onboard-LED heartbeat** (blink 2 before `engine.init()`, 3 after, steady ~1.4 Hz once audio runs).
- `src/app.cpp` — ChucK now uses **block 256** (was 96); `CHUCK_BRINGUP` LED checkpoints (useless on the
  cased spotykach — its Daisy LED is hidden).
- `Makefile` — `ENGINE=chuck` branch, `engine-chuck`/`program-chuck`, `engine_select.h` entry,
  bring-up flags `BRINGUP=1` / `NOCHUCK=1` / `CHUCKLVL=N`.
- `alt_qspi_chuck.lds` — ChucK-only linker script (the stock `alt_qspi.lds`, used by csound, is
  unchanged). Two ChucK-specific changes: (1) reclaims the unused 186 KB `SRAM_EXEC` into `SRAM`
  (full 512 KB AXI SRAM — needed or `.bss` overflows 326 KB); (2) **moves the main stack** to the top
  of AXI SRAM (`_estack = 0x24080000`, ~169 KB) instead of the ~31 KB free in DTCMRAM.

## What we proved on the spotykach (in order)

1. **Flashing + the QSPI/bootloader path work** — csound (a QSPI app) runs on this unit; `dfu-util`
   writes fine (the earlier `LIBUSB_ERROR_ACCESS` was a udev/`uaccess` gap, fixed with a `plugdev`
   rule: `/etc/udev/rules.d/70-st-dfu.rules` `…0483/df11 MODE="0660" GROUP="plugdev"`).
2. **Platform + `alt_qspi_chuck.lds` + bootloader are fine.** `make engine-chuck NOCHUCK=1` (skips the
   whole ChucK runtime) **boots** — the panel renders (idle state: deck-B play pad red/blinking). So the
   SRAM-reclaim linker script and the QSPI handoff are NOT the problem.
3. **The crash is in ChucK's runtime init.** `make engine-chuck CHUCKLVL=2` (`new ChucK()` +
   `ChucK::init()`, NO `compileCode()`) **crashes** (partial/garbage panel). `CHUCKLVL=3` (full) →
   solid white. → the fault is in **`new ChucK()` or `ChucK::init()`**, not the compiler.
4. **Not a stack overflow (by itself).** Moving the stack from ~31 KB (DTCMRAM) to ~169 KB (AXI SRAM,
   `_estack=0x24080000`) did **not** fix it. (`_estack` verified at `0x24080000` via `nm`.)
5. Level-2 and level-3 crash with **different** LED states — the layout-dependent signature of memory
   corruption / an early fault, not a fixed deterministic point.

**Leading hypotheses now** (ChucK init, on bare metal, that the host link-probe didn't exercise):
- an **uncaught C++ exception** thrown in `init()` propagating into the `-fno-exceptions` platform →
  `std::terminate` (only `chuck_engine.o` has `-fexceptions -frtti`);
- a **null-deref hardfault** — `init()` touching a resource our POSIX/sndfile **stubs** return null for
  (e.g. a built-in file/dir/`sf_*` path) and dereferencing it;
- less likely: **pool exhaustion** during `init()` (12 MB) → `bad_alloc`. (STK rawwave file loads no-op
  with no filesystem, so init's heap need is probably modest — but unconfirmed.)
- **NOT yet confirmed:** is it `new ChucK()` or `ChucK::init()`? We never got the `CHUCKLVL=1` result.

## Open question we still owe an answer

`CHUCKLVL=1` (just `new ChucK()` + `setParam`): does it boot? → splits **construction** vs **`init()`**.

## Next steps — on the Daisy Pod (the plan we stopped at)

Why the Pod: onboard LED **visible**, SWD pads **accessible**, harness is **pure ChucK** (no CoreUI),
and a **different memory map** (no 48 MB granular arena → ChucK gets a roomy pool/stack). Key tell: if
the Pod harness **works**, ChucK is fine and the spotykach crash is its *memory layout* (the 48 MB
arena starving ChucK) — a different, easier fix. If it **crashes the same**, we have a clean repro.

### 1. Flash the harness (full build) and read the onboard LED
```
cd pod
make -f Makefile.chuck
make -f Makefile.chuck program-dfu      # Pod in DFU; needs a QSPI-capable Daisy bootloader
```
- **2 → 3 blinks → steady ~1.4 Hz + audible drone** = ChucK WORKS on the Pod → focus on the spotykach
  memory map (shrink the unused 48 MB granular arena for the chuck build, give ChucK a big pool).
- **2 blinks then dark** = `engine.init()` crashes on the Pod too = minimal repro → debug it here.

### 2a. If you have an SWD probe (fastest — ~5 min)
QSPI execute-in-place rules (from `csound-impl.md`): **attach, don't flash**; **hardware breakpoints
only**; attach **after** the bootloader maps QSPI. Flash via DFU, then attach and load symbols from
`pod/build/harness_chuck.elf`, break at `spotykach::ChuckEngine::init`, and step through
`new ChucK()` → `init()` → `compileCode()` to the faulting line / return code. (Resume here: produce
the OpenOCD + arm-none-eabi-gdb command lines for ST-Link.)

### 2b. If no probe — bisect with the visible LED
```
make -f Makefile.chuck CHUCKLVL=2 && make -f Makefile.chuck program-dfu   # new ChucK + init
make -f Makefile.chuck CHUCKLVL=1 && make -f Makefile.chuck program-dfu   # new ChucK only
```
First level reaching **3 blinks** is fine; the one that stops at **2** is the culprit call.

### 3. Likely fixes to try once localized
- **If `init()` throws:** wrap `new ChucK()`/`init()`/`compileCode()` in `try/catch` in
  `chuck_engine.cpp` (leave `_ready=false` on failure) — converts a crash into a graceful silent boot
  AND confirms "it was an exception." (Catch works only if every frame from throw→catch has unwind
  tables; libchuck does, `chuck_engine.o` does.)
- **If null-deref:** find which stub `init()` derefs (the SWD backtrace names it); make the stub return
  a benign non-null or guard the ChucK call. Candidates: dir scan, working-dir/realpath, `sf_*`.
- **If pool exhaustion:** the chuck build wastes 48 MB on the granular arena ChucK never uses
  (`buffer.sdram`, `.sdram_bss`). Conditionally shrink it for `SPK_ENGINE_CHUCK` and grow the chuck
  pool (`kPoolBytes` in `chuck_alloc.cpp`) well past 12 MB. (This is also the prime suspect if the Pod
  works but the spotykach doesn't.)

## Build/flash cheatsheet
```
make engine-chuck                  # spotykach: clean build + flash (full)
make engine-chuck NOCHUCK=1        #   skip ChucK runtime  -> BOOTS (platform proven)
make engine-chuck CHUCKLVL=2       #   new ChucK + init, no compile  -> CRASHES
make engine-chuck CHUCKLVL=1       #   new ChucK only  -> ??? (the open question)
make engine-chuck BRINGUP=1        #   onboard-LED checkpoints (hidden on the cased spotykach)
cd pod && make -f Makefile.chuck   # bare Pod harness (visible LED heartbeat); same CHUCKLVL/NOCHUCK
```
DFU access (Linux): `/etc/udev/rules.d/70-st-dfu.rules` → `…0483/df11 MODE="0660" GROUP="plugdev"`,
`udevadm control --reload-rules && udevadm trigger`, replug. Effective flash cmd:
`dfu-util -a 0 -s 0x90040000:leave -D <bin> -d ,0483:df11`.

## Toolchain / facts
- `/opt/daisy-compiler` arm-none-eabi-gcc 10.2.1. `libchuck.a` built by `scripts/fetch_chuck.sh`
  (exceptions+RTTI ON; `chuck_engine.o` must match — `-fexceptions -frtti`, scoped to that one TU).
- STM32H750: 512 K AXI SRAM @`0x24000000`, 128 K DTCMRAM @`0x20000000`, 64 M SDRAM @`0xc0000000`,
  QSPI app @`0x90040000`.
- ChucK feature defines in the engine TU MUST match `fetch_chuck.sh` (ABI). See the `ENGINE=chuck`
  Makefile branch.
