# Dev notes — ChucK as an sk-engines engine (`ENGINE=chuck`) — ROADMAP

> **Status: M0–M2 DONE (WORKING ON HARDWARE), M3 CODE-DONE/BUILD-VERIFIED (2026-06-21).** ChucK boots,
> compiles, runs the VM, makes sound, and responds to the knobs on a bare Daisy Pod (verified over SWD
> + by ear). `M0`: `libchuck.a` cross-builds + links (`scripts/fetch_chuck.sh`, pinned `chuck-1.5.5.8`).
> `M1`: Pod tone from QSPI — the `SawOsc => LPF => dac` drone plays, knobs drive `speedA`/`mixA`. `M2`:
> `ChuckEngine : IEngine` (`src/engine/chuck/`) wired into both the Pod harness (`make -f Makefile.chuck`)
> and the full firmware (`make engine-chuck`). **`M2`+`M3` are now HARDWARE-VERIFIED ON THE SPOTYKACH
> (2026-06-21):** the first flash of the cased unit (`make engine-chuck METER=1`, DFU) booted, ran the
> VM, and made sound. **`M3` is HARDWARE-VERIFIED too:** the SD `chuck/<n>.ck` patch bank + Alt+PITCH
> live swap (`chuck_patch.h` + the gated single-VM `CK_MSG_CLEARVM` reset + `compileCode` recompile) — boot
> auto-load, the selector, and live switching all confirmed on the spotykach with a card of
> `examples/chuck/*.ck` (after two fixes: the `ENGINE=chuck` branch was missing `-DSPK_USE_STREAM` so the
> bank saw no card, and the 48 MB engine arena is now opted out via `-DSPK_NO_ENGINE_ARENA` since ChucK
> brings its own pool — SDRAM 97% -> 22%). An **on-panel CPU/shred meter** (`METER=1`) makes risk #1 readable without a probe
> (ring A = CPU load, ring B = live shred count; production build's rings = stereo dB output meter). See
> the M3 roadmap entry for the three deliberate deltas from the Csound original.

---

## ⚠ OPEN BUG — patch-swap resource leak (UNRESOLVED, handoff 2026-06-21)

**Symptom.** Cycling patches with the Alt+PITCH selector (SD slot → SD slot, repeatedly) degrades the
unit: patches that played fine individually start failing after several swaps — audio glitches / the
CPU-overrun safeguard latches (the double-red panic rings). It is **cumulative** across swaps and
recovers only on power-cycle. **Confirmed still present in the PRODUCTION build** (`make engine-chuck`,
no METER) — so it is a genuine per-swap leak, not only the instrumentation bug below.

**This is the thing to fix next session.** Everything else (boot, single-patch playback, knobs, the
bank selector, the PRNG fix, the arena shrink, the CPU-overrun safeguard) works on hardware.

### What was tried (in order) and the outcome
1. **Original M3 reload:** `removeAllShreds()` + `compileCode()` on one persistent VM. Leaked. (Expected
   in hindsight — `removeAllShreds` frees shreds but NOT the user type system `compileCode` accumulates.)
2. **CLEARVM reset** (current code): `reset_vm()` = `Chuck_VM::process_msg(CK_MSG_CLEARVM)` (synchronous,
   under the gate) then `compileCode()`. CLEARVM does `removeAll()` + `env()->clear_user_namespace()` +
   `cleanup_global_variables()`. This is the canonical reset (matches the user's chuck-max `reset`,
   `~/projects/personal/chuck-max/source/projects/chuck_tilde/chuck_tilde.cpp` `ck_reset`). **Still leaks.**
3. **Pool 12 MB → 40 MB** (`chuck_alloc.cpp` `kPoolBytes`). Buys more swaps before failure; does not stop
   the leak. Kept (free headroom now that the arena is shrunk).
4. **Observer-effect meter bug (FIXED, separate):** a pool-memory meter I added walked the whole pool
   under PRIMASK every render → starved the audio ISR → false overrun, worse as the pool filled. Removed.
   It *amplified* the symptom in METER builds but is NOT the underlying leak (production leaks without it).
5. **VM destroy+recreate** (`delete _ck; new ChucK()` per swap): briefly implemented, then **discarded**
   in favour of CLEARVM. **NOT in the current code.** This is the most promising untried-on-hardware fix
   (see below) — it is the only approach that fully reclaims, mirroring Csound's `csoundDestroy`+create.

### Leading hypotheses for next session
- **ChucK accumulates beyond what CLEARVM frees.** `clear_user_namespace()` clears user *types*, but the
  compiler's emitted bytecode (`Chuck_VM_Code`), compilation contexts, and string/const pools per
  `compileCode` may persist. Known-ish ChucK behaviour. → only a full VM teardown reclaims it.
- **`dac` graph not disconnected.** `=> dac` adds a UGen to `dac`'s source list (with a ref). When a
  shred is removed, was mid-investigating whether `removeAll()`/`free_shred()` (`chuck_vm.cpp` ~1314 /
  ~1356) actually sever the `dac` connection. If not, old patches' UGens linger AND keep **ticking** →
  this would be a CPU leak (matches the overrun symptom), not just memory. NB `all_detach()` is commented
  out in the `CK_MSG_EXIT` handler (`chuck_vm.cpp` ~1011), a hint ChucK doesn't auto-detach the dac graph.

### Recommended next steps
1. **First, classify the leak: CPU vs memory vs both.** Add a NON-destructive memory readout — an O(1)
   running `_used` counter in `CsoundPool` (increment on alloc, decrement on free), **NOT** a walk, and
   surface it (panel or USB CDC). Likewise watch ring A (CPU). This tells us which resource grows per
   swap. (Do not re-introduce the `used_bytes()` walk in any hot/ISR/PRIMASK path.)
2. **Check the `dac` graph.** Instrument/inspect `m_dac`'s source-list length across swaps (or just try:
   in `do_reload`, before recompiling, explicitly disconnect everything from `dac`). If the source list
   grows per swap, that's the leak; disconnect it on reset.
3. **If CLEARVM genuinely can't reclaim, switch `do_reload` to VM destroy+recreate** — `delete _ck` then
   `build_vm(newprog)` (the `build_vm` helper already exists and is used at init; `do_reload` would call
   it instead of `reset_vm`+`compileCode`). Delete-then-build keeps one VM's footprint at a time. Cost:
   the type-system + STK re-registration per swap (a bigger silence gap), but it fully reclaims. This is
   the safe fallback and mirrors the proven Csound engine exactly.
4. **Sanity-check against chuck-max:** does it sustain many `reset`+recompile cycles without growth? If
   chuck-max also grows, the leak is inherent to ChucK's reset path and destroy+recreate is the answer.

### Current code state (all uncommitted on branch `dev`)
- `src/engine/chuck/chuck_engine.{h,cpp}` — M3 bank + selector; `build_vm()` (init-time VM create+compile);
  `reset_vm()` (CK_MSG_CLEARVM); `do_reload()` (gated take → reset_vm → compileCode → reseed → publish);
  CPU-overrun safeguard (`_panic` mute-and-wait, always-on, DWT-timed in `process()`); METER meter
  (ring A CPU / ring B shred-count); production stereo dB output meter; bring-up capture (`g_chuck_init_*`).
- `src/engine/chuck/chuck_alloc.cpp` — 40 MB `--wrap` SDRAM pool (`CsoundPool`) + PRIMASK `CritSec`.
- `src/engine/chuck/chuck_patch.h` — SD `chuck/<n>.ck` selector (host-tested).
- `scripts/fetch_chuck.sh` + `thirdparty/chuck/Daisy/shim/chuck_posix_stubs.c` — `random()` xorshift PRNG
  seeded from DWT CYCCNT (fixes frozen `Math.random*`). `libchuck.a` was rebuilt with this (`ar r`).
- `Makefile` `ENGINE=chuck` branch — `-DSPK_USE_STREAM` (SD bank) + `-DSPK_NO_ENGINE_ARENA` (arena shrink,
  shared with csound). `src/hw/buffer.sdram.cpp` — arena → 64 KB token under `SPK_NO_ENGINE_ARENA`.
- `examples/chuck/0..6.ck` + `README.md` (desktop-`chuck`-compile-checked); `host/test_chuck_patch.cpp`.
- Nothing committed this whole effort — the user commits.

---
>
> **Bring-up took four bug fixes** (`-u _printf_float`, `_ck->start()` before audio, a PRIMASK guard on
> the non-reentrant SDRAM pool, and a sane knob cadence) — none ChucK-on-Cortex-M incompatibilities. The
> full story, the SWD flash/inspect tooling, and the open items live in
> [`chuck-pod-poc.md`](chuck-pod-poc.md) (read it for current state). The roadmap below (M3 SD bank, M4
> MIDI) and the risk analysis are still accurate; the `METER=1` CPU-headroom pass (risk #1) is the main
> open measurement — `ck->run(256)` is CPU-heavy (~near the block budget).

> This is a design + roadmap for embedding
> the [ChucK](https://chuck.stanford.edu) language as a swappable engine, modelled directly on the
> already-shipping **Csound** engine ([`docs/dev/csound-impl.md`](csound-impl.md)). ChucK is the same
> *class* of problem — embed a runtime audio compiler/VM as a QSPI app behind `IEngine` — so most of
> the hard, already-solved Csound machinery (QSPI boot, the dual-heap SDRAM allocator, the lock-free
> live-recompile handoff, the SD patch bank) is reused, not re-invented. Read the Csound doc first;
> this one is written as a **delta** against it.

[ChucK](https://github.com/ccrma/chuck) is a strongly-timed audio programming language with a runtime
compiler + virtual machine: a program (`.ck` text) is compiled at runtime into shreds (concurrent
strongly-timed processes) that the VM schedules sample-accurately against a UGen graph. Like Csound,
**the patch — not the firmware — defines the sound**: you load `.ck` programs from the SD card, switch
between them live, and play them from the panel knobs and over MIDI.

---

## Why this is feasible (the short version)

1. **ChucK is designed to be embedded.** Its source is split into `src/core/` (the language, VM,
   UGens — no I/O assumptions) and `src/host/` (the CLI + RtAudio driver). You compile `core/`, drop
   `host/`, and drive the `ChucK` host class yourself — the *same* core/host split that made the
   Csound port tractable.

2. **The host API maps almost 1:1 onto `IEngine`** (see the table below).

3. **The audio I/O boundary is single-precision.** `chuck_def.h` defines `#define SAMPLE float`
   (the 64-bit sample path is a commented-out opt-in), so `run()` exchanges `float` buffers — no
   double marshalling at the block boundary, FPU-friendly on the Cortex-M7.

4. **WebChucK is the proof of concept.** [WebChucK](https://github.com/ccrma/webchuck) compiles
   `core/` to WebAssembly and drives it from a **single-threaded** Web Audio worklet — no OS, no
   filesystem, no chugins, no on-the-fly (OTF) server, `compileCode()` from a string, `run()` per
   block. That is almost exactly the bare-metal contract we impose here, so WebChucK's emscripten
   build config is effectively a ready-made template for *which* `core/` files and `#define`s survive
   without an OS. **Mine the WebChucK Makefile/CMake before writing our own.**

---

## How ChucK maps onto IEngine

`ChuckEngine : public IEngine` (`src/engine/chuck/chuck_engine.{h,cpp}`) would override only what it
uses, exactly like `CsoundEngine`. The host class is `ChucK` (`src/core/chuck.h`).

| `IEngine` | ChucK host API | Csound equivalent (already shipped) |
|---|---|---|
| `init(ctx)` | `new ChucK()` → `setParam(CHUCK_PARAM_SAMPLE_RATE, sr)` / `..._INPUT_CHANNELS` / `..._OUTPUT_CHANNELS` → `init()` → `compileCode(text)` | `csoundCreate` / options / `csoundCompileCSD` / `csoundStart` |
| `process(in,out,n)` | `chuck->run(in, out, n)` — **`SAMPLE = float`**, interleaved by channel-count; one call per block | de-interleave → `csoundPerformKsmps` → de-interleave |
| `set_param(id,deck,v)` | `chuck->globals()->setGlobalFloat("speedA", v)` (the program reads the global) | `csoundSetControlChannel(name, v)` |
| `handle_midi_note(ch,note)` | enqueue → in `process()` `setGlobalInt("midiNote", n)` + `broadcastGlobalEvent("noteOn")` (a `MidiNote`-style shred sporks a voice) | `csoundEvent(CS_INSTR_EVENT, …)` |
| Alt+PITCH live patch swap | `reset_vm()` (`CK_MSG_CLEARVM`) + `compileCode(newText)` behind the `ReloadGate` | `ReloadGate` + `csoundDestroy` + recompile |
| `render(m)` | output-level meter + patch selector + run-state | identical |
| `capabilities()` | `CapAux \| CapOwnDisplay` | identical |

**The program is the param vocabulary.** Swap the `.ck`, swap the synth, no C++ change — same
principle as the CSD `chnget` convention. The platform writes a fixed set of named globals
(`speedA`/`mixA`/`sizeA`/`envA`/… + the `B` deck); the program opts in by declaring
`global float speedA;` etc. and reading them in its synthesis loop.

### Built-in fallback program

Ship a compiled-in `kProgram` string (a `SinOsc`/`SawOsc => LPF => dac` drone reading the panel
globals, plus a `MidiNote` shred sporked on the `noteOn` event) so a card-less unit always sounds and
a bad SD program falls back — mirroring Csound's `kOrchestra`.

---

## What transfers verbatim from the Csound engine

Nearly everything hard about Csound is reusable with only names changed:

- **QSPI boot model.** `BOOT_QSPI` + `alt_qspi.lds` (code in QSPI flash `0x90040000`, data/bss in
  SRAM/DTCM) + the `init_priority(101)` `SCB->VTOR` inject (`spotykach_qspi_vtor.cpp`). ChucK's linked
  code is also far over the 186 KB `SRAM_EXEC` budget. The verified bootloader fact (the v2 bootloader
  runs QSPI apps; no bootloader change needed) carries over.

- **Dual heap.** Platform heap stays in SRAM (global ctors `malloc` before `_hw.Init()` powers the
  FMC); ChucK's heap is the 12 MB SDRAM pool reached via `--wrap=malloc/free/calloc/realloc`, armed
  *after* `_hw.Init()`. The free-capable coalescing allocator (`CsoundPool` in `csound_pool.h`) is
  engine-agnostic — rename to a shared `SdramPool` and reuse for sustained patch swapping. ChucK is
  STL/heap-heavy, so this is mandatory, not optional.

- **Lock-free handoffs.** `ReloadGate` (ISR↔main live-instance swap, seq_cst) and the SPSC
  `NoteQueue` (MIDI main→ISR) are reusable as-is — the live-recompile and MIDI-into-ISR hazards are
  identical.

- **SD patch bank + Alt+PITCH selector + boot auto-load + built-in fallback** — same UX and the same
  FatFs gotchas (relative paths, no leading slash; strip BOM/CRLF on load). `.ck` files instead of
  `.csd`, under `/chuck/0.ck` … `/chuck/7.ck`.

- **`scripts/fetch_chuck.sh`** modelled on `fetch_csound.sh`: fetch a pinned `ccrma/chuck` tag (the
  `~MB` source is gitignored, reproduced from upstream), cross-build `libchuck.a` + headers for
  cortex-m7 single-precision bare metal, install to `thirdparty/chuck/Daisy/{lib,include}` where the
  `ENGINE=chuck` Makefile branch points `-I` / link. The Makefile branch needs `APP_TYPE=BOOT_QSPI
  LDSCRIPT=alt_qspi.lds` and `-DSPK_USE_STREAM` (for SD), exactly like the csound branch.

---

## The ChucK-specific risks (where it diverges from Csound)

These are the unknowns to retire on the Pod **before** committing to the spotykach build. Ordered by
how much they could sink the effort.

1. **Double-precision UGen math (CPU headroom — downgraded after M0).** The I/O `SAMPLE` is `float`,
   but ChucK's language-level `float` is `t_CKFLOAT = double`, and many UGen `tick()` functions compute
   in double internally (phase, gain, freq). **Correction from M0:** the STM32H7 Cortex-M7 has a
   *hardware double-precision FPU* — the Daisy build flags are `-mfpu=fpv5-d16` (the `-d16`
   double-capable unit, not `-sp-`), confirmed in `lib/libDaisy/core/Makefile`. So ChucK's double math
   is hardware-accelerated, *not* software-emulated. It is still ~2× single-precision and uses more
   registers, and it stacks on the QSPI execute-in-place penalty (Csound's roadmap #4) — so CPU
   headroom is still the **first thing M1 must measure** (`METER=1`), but it is a tuning question, not
   a likely showstopper. Mitigations if it bites: larger block size, lighter UGens / capped shred
   count, pinning hot `.text` into SRAM.

2. **Threading / mutexes in the Globals Manager. — LARGELY RETIRED by M0.** ChucK's host↔VM global
   exchange uses a mutex, and `core/` references `pthread_t`/`pthread_mutex_t` (the OTF server, the
   main-thread hook, the globals queue). The fix is WebChucK's `-D__DISABLE_THREADS__`, which compiles
   those paths out — with it, the whole core compiles and links for cortex-m7 with **no pthread
   symbols** (M0 verified). Still disable OTF via params (`CHUCK_PARAM_OTF_ENABLE = 0`). The remaining
   question is runtime: with threads disabled, the globals queue is drained on the audio thread, so the
   host must enqueue global writes (mirroring the Csound `set_param`→channel pattern) rather than
   touching VM state across the ISR boundary — an M2 wiring detail, not a build blocker.

3. **Feature exclusions (structural, like Csound's libsndfile-off).**
   - **Chugins** (dynamically `dlopen`'d UGen plugins) — gone; bare metal has no dynamic loading.
     Only statically-linked UGens exist. (Biggest synthesis limit, same as Csound's plugin opcodes.)
   - **`SndBuf` / `WvIn` / `WvOut`** (disk sample I/O) — either wired to the platform `IStreamDeck`
     (a real project, deferred) or disabled. Synthesis UGens (oscillators, the STK instruments,
     filters, delays, the FFT/analysis UGens) remain.
   - **OSC / HID / serial / MIDI-device** UGens — gone; the host feeds I/O.

4. **Code size + opcode/UGen set.** Likely smaller than Csound's ~2 MB, but confirm `libchuck.a`
   links into the 8 MB QSPI image alongside `SPK_USE_STREAM`'s ~2 MB of SDRAM rings. Audit the
   statically-registered UGen set for this build and document it.

5. **Strongly-timed semantics vs a fixed block.** ChucK schedules shreds at sample accuracy *inside*
   `run(n)`; `n` = our platform block (256). This is fine (WebChucK does the same), but very fine
   `1::samp` timing across a 256-sample block is computed within the one `run()` call — verify dense
   per-sample shreds don't blow the ISR budget (related to risk #1).

---

## Memory & boot model (same as Csound)

- Code in **QSPI flash** (`0x90040000`, 8 MB), executed in place. Data/bss in SRAM/DTCM; ChucK heap in
  the 12 MB SDRAM pool; platform heap in SRAM.
- `alt_qspi.lds` (code + `.data` load-address moved `SRAM_EXEC → QSPIFLASH`).
- `SCB->VTOR = 0x90040000` inject (global ctor) — needed because the startup skips `SystemInit` under
  `BOOT_APP`; without it SysTick + audio DMA are dead.
- No console in the QSPI build — use audio as the debug signal (a known tone = boot/output OK; a
  distinct pitch = "compile failed"), or wire USB-CDC / SWO as Csound's notes describe. A SWD probe
  (attach-don't-flash, hardware breakpoints only — QSPI is read-only to the debugger) is the
  high-leverage tool for pre-`main` ctor crashes and `chuck->run()` return codes.

---

## Building `libchuck.a` (`scripts/fetch_chuck.sh` — DONE, M0)

`thirdparty/chuck/` is ChucK (pinned default `CHUCK_REF=chuck-1.5.5.8` of
[`ccrma/chuck`](https://github.com/ccrma/chuck)), **gitignored**, fetched + cross-built by one script
(modelled on `fetch_csound.sh`):

```
scripts/fetch_chuck.sh             # fetch source + cross-build libchuck.a + self-verify with a link probe
scripts/fetch_chuck.sh --no-build  # just fetch + write the shim sysroot (build later)
```

Unlike Csound, ChucK has **no official Daisy port**, so the script *is* the port. What M0 established:

- **Source subset = WebChucK's** `EMSCRIPTENSRCS` (src/makefile), minus `host-web/chuck_emscripten.cpp`
  (we bring our own host glue) and minus `util_sndfile.c` (the bundled libsndfile won't build
  bare-metal; we stub `sf_*` instead). 36 `.cpp` + 3 `.c` TUs.
- **No flex/bison needed.** `-D__USE_CHUCK_YACC__` selects the checked-in pre-generated parser
  (`core/chuck_yacc.h`) over the bison-generated `chuck.tab.h`. `core/chuck_yacc.c` compiles standalone.
- **Feature defines = WebChucK's emscripten set** (`__DISABLE_MIDI__`, `__DISABLE_HID__`,
  `__DISABLE_SERIAL__`, `__DISABLE_OTF_SERVER__`, `__DISABLE_NETWORK__`, **`__DISABLE_THREADS__`**,
  `__DISABLE_SHELL__`, `__OLDSCHOOL_RANDOM__`, …) plus `-D__PLATFORM_LINUX__` (picks a platform branch;
  also dodges a literal `#elif`-with-no-expression bug in `util_platforms.h` that fires when no
  platform is defined) and the little-endian / `sf_count_t` macros for the vendored `util_sndfile.h`.
- **The port surface = a shim sysroot, not a code patch.** ChucK's core assumes a hosted POSIX
  userland with no bare-metal branch (`#ifndef __PLATFORM_WINDOWS__ #include <dirent.h>/<netinet/in.h>/
  <unistd.h>`, `va_list` without `<cstdarg>`, `sys/ioctl.h`, `poll.h`, BSD `random`/`usleep`,
  `getline`, `realpath`, `glob`/`wordexp`). The script writes `Daisy/shim/` (minimal stub headers) +
  a force-included `ck_prelude.h` (declares the POSIX functions ChucK uses without including their
  headers) + `chuck_posix_stubs.c` (resolves the disabled-feature symbols at link: chugin `dlopen`,
  directory scan, tty, BSD random, and libsndfile `sf_*` — none are exercised; `SndBuf`/`LiSa` file
  loads just fail gracefully, which is fine with no filesystem). The stub TU is folded into
  `libchuck.a`, so the firmware link needs only the one archive.
- **MCU flags match `lib/libDaisy/core/Makefile`** (`-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16
  -mfloat-abi=hard`) so the archive is ABI-compatible (and gets the hardware double FPU — see risk #1).
  Built at `-Os` with exceptions + RTTI **on** (ChucK uses both; `-fno-exceptions` was not validated).
- **Result:** `Daisy/lib/libchuck.a` (~5.9 MB archive, ~1.1 MB `.text`). The script's link probe
  (`new ChucK()` → `setParam` → `init` → `compileCode` → `run`) links against libc/libstdc++ + the
  folded stubs with `--specs=nano.specs --specs=nosys.specs`. >186 KB of code ⇒ confirms the **QSPI**
  build; ~1.1 MB fits the 8 MB flash easily.

Headers come straight from `thirdparty/chuck/src/core` (no copy step); the firmware `-I`s that dir
plus `Daisy/shim`, and force-includes `Daisy/shim/ck_prelude.h`, with the same `-D` set above scoped
to the ChucK engine TU (M2 Makefile wiring).

---

## Roadmap (ordered by leverage)

**M0 — Cross-build `libchuck.a`. — DONE (2026-06-19).** `scripts/fetch_chuck.sh` builds the
bare-metal core-only archive from WebChucK's source subset + feature flags, generating the shim
sysroot/prelude/stubs it needs, and **self-verifies** by compiling + linking a probe TU
(`new ChucK()` → `compileCode("SinOsc s => dac;")` → `run()`) for cortex-m7. All 39 core TUs compile;
the probe links (text ≈ 1.1 MB). The threading risk (#2) was retired by `-D__DISABLE_THREADS__`. See
"Building `libchuck.a`" above for the full recipe and findings. *Gate cleared.*

**M1 — Pod tone from QSPI (retire risk #1 + #2). — DONE, WORKING (2026-06-19).** The Pod vehicle
(`pod/harness_chuck.cpp` + `pod/Makefile.chuck`) builds an `EngineContext`, `init()`s the real
`ChuckEngine`, drives `process()` from the audio callback, and reads the knobs → `set_param()`. The
BOOT_QSPI image (≈1.32 MB) links against `libchuck.a` + the real SDRAM `--wrap` pool (`chuck_alloc.cpp`,
so `chuck_heap_arm()` arms the `.sdram_bss` pool — the M2 heap model). **Flashed and confirmed on a Pod:
the drone plays, knobs drive it.** Four bugs had to be fixed first (see [`chuck-pod-poc.md`](chuck-pod-poc.md)).
**Still open:** `METER=1` CPU headroom — `ck->run(256)` is heavy (~near the block budget at block 256);
quantify before adding voices.

**M2 — `IEngine` skeleton on the spotykach. — HARDWARE-VERIFIED (2026-06-21).**
`ChuckEngine : IEngine` (`src/engine/chuck/chuck_engine.{h,cpp}`) implements `init`/`prepare`/`process`/
`set_param`→`setGlobalFloat`/`param`/`render`/`capabilities` + the built-in `kProgram` (a `SawOsc => LPF
=> dac` drone reading `speedA`/`mixA`/`sizeA`). `process()` interleaves the platform's de-interleaved
buffers into `ck->run()` (SAMPLE=float) and back; the run() scratch is malloc'd from the SDRAM pool so
the SRAM-tight platform pays nothing for it. Wired in: the `ENGINE=chuck` Makefile branch (BOOT_QSPI,
the ChucK feature defines matching `libchuck.a`'s ABI, `-fexceptions -frtti` scoped to just the engine
TU, `--wrap` malloc), `engine_select.h` (`SPK_ENGINE_CHUCK → ChuckEngine`), `chuck_alloc.cpp` (reuses
the engine-agnostic `CsoundPool`), and the shared `spotykach_qspi_vtor.cpp`. `make engine-chuck` links
the full firmware. **One platform fix this surfaced:** the QSPI build reserved 186 KB of AXI SRAM for
`SRAM_EXEC` that holds no code (it executes in place from QSPIFLASH), so `CoreUI + libDaisy .bss`
(~340 KB) overflowed the 326 KB `SRAM` region. Rather than touch the proven Csound QSPI path, ChucK
gets its own `alt_qspi_chuck.lds` (identical to `alt_qspi.lds` except it reclaims the unused 186 KB
`SRAM_EXEC` into `SRAM`, giving data the full 512 KB AXI SRAM); the `ENGINE=chuck` branch and the Pod
harness point at it, and the stock `alt_qspi.lds` (csound) is byte-unchanged. The four Pod fixes are all
in the spotykach build (`-u _printf_float` in the `ENGINE=chuck` branch; `start()` + the pool guard via
the shared `chuck_engine.cpp`/`chuck_alloc.cpp`; the knob-cadence fix is n/a — the spotykach UI is
event-driven). **Flashed the cased unit (2026-06-21): boots, runs the VM, makes sound.** Remaining
hardware checks: read the CPU/shred meter for the actual headroom, and test the M3 bank with an SD card.
MIDI is M4.

**M3 — SD `.ck` patch bank + live swap. — CODE DONE, BUILD-VERIFIED (2026-06-21).** `chuck_patch.h`
(numbered `chuck/<n>.ck` slots, the Alt+PITCH quantizer, read+normalize+BOM/CRLF-strip, built-in
fallback) + the bank/selector wiring in `chuck_engine.{h,cpp}` (rescan, boot auto-load of the lowest
slot, Alt+PITCH preview/commit, the cyan/white source LED, the gated live recompile). The host
selector test (`make -C host test-chuck-patch`) passes and both firmware targets link clean
(`pod/Makefile.chuck`, `make engine-chuck`). **Hardware test pending** (needs a card with `chuck/*.ck`).
Two deliberate deltas from the Csound original:
- **No `chuck_reload.h`.** The `ReloadGate` is engine-agnostic (`void*`, no Csound deps), so the ChucK
  engine reuses `engine/csound/csound_reload.h` directly - the same reuse pattern `chuck_alloc.cpp`
  already uses for `CsoundPool`. One gate, one host test (`test-csound-reload` covers it).
- **One persistent VM, reset (not rebuilt), gated.** Csound's `do_reload` destroys + recreates the
  `CSOUND`; ChucK keeps one VM (its built-in type/UGen registration is expensive) and *resets* it: a
  reload is `reset_vm()` then `compileCode(newText)` on `_ck`, with the `ReloadGate` taking the VM out
  of the audio path for the swap. `reset_vm()` sends `CK_MSG_CLEARVM` via `Chuck_VM::process_msg`
  (synchronous: `removeAll` shreds + `clear_user_namespace` + `cleanup_global_variables`). **This is the
  patch-swap leak fix:** `removeAllShreds()` alone does NOT free the user type system that `compileCode`
  accumulates per compile, so the original `removeAllShreds`+recompile leaked a program's footprint per
  swap and exhausted the 12 MB pool after a few switches (previously-fine patches then overran). The
  reset must be SYNCHRONOUS and BEFORE the compile (a queued `CLEARVM` via
  `execute_chuck_msg_with_globals` would be drained inside the next `run()` and wipe the just-compiled
  types). The gate is still required: `compileCode` mutates VM state and, under `__DISABLE_THREADS__`,
  nothing locks it against a concurrent `run()`. A brief audio gap on a user-initiated swap is the cost.
- **No structural pre-validation.** A `.csd` must begin with `<CsoundSynthesizer>`; a `.ck` has no
  required header, so `read_program` only normalizes (BOM/CRLF) and rejects empty/whitespace-only
  files. The real validation is `compileCode()` returning false -> fall back to the built-in.

**M4 — MIDI in.** Enqueue NoteOn on the SPSC `NoteQueue`; `process()` drains it in the ISR and drives a
`global Event noteOn; global int midiNote;` that the program's `MidiNote` shred sporks a finite voice
on. NoteOn-only / no velocity / no NoteOff (platform constraint) → finite plucks, same as Csound.

**M5 — Control/UI depth + UGen audit.** Expand the global vocabulary to the full 7 params/deck, draw
the Alt+PITCH selector, document the available UGen set, and write the user-facing
`docs/engines/chuck.md` + `examples/chuck/` programs (a drone, a bass, a poly MIDI lead — the
templates that name the platform globals).

**Host-testable off-target (no `libchuck`):** the patch bank scan/quantizer, the `ReloadGate`
handoff, and the MIDI note→global mapping + SPSC ring — exactly the units Csound already host-tests
(`make -C host test-csound-{patch,reload,midi,alloc}`). Mirror them as `test-chuck-*`. The
`compileCode`/`run` calls are QSPI-only.

---

## Anticipated gotchas (carried from Csound; verify for ChucK)

- **FatFs paths must be relative** (no leading slash) — `"chuck/%d.ck"`, not `"/chuck/…"`.
- **SD text vs a compiled-in string**: strip a UTF-8 BOM and normalize CRLF→LF before `compileCode`
  (the Csound parser choked on both; ChucK's lexer likely tolerates more, but normalize anyway).
- **The SDRAM-heap-vs-ctor crash**: keep the platform heap in SRAM; arm the ChucK SDRAM pool *after*
  `_hw.Init()`. This is the central reusable lesson.
- **`-DSPK_USE_STREAM`** must be in the Makefile chuck branch or `ctx.stream` is null and SD loading
  silently no-ops (the bank shows only the built-in).
- **Large block / shred budget**: keep the block at 256; watch dense per-sample shreds (risk #5).

---

## Files (mirrors the csound layout)

- `src/engine/chuck/chuck_engine.{h,cpp}` — **DONE (M2).** `ChuckEngine : IEngine` (init / compile /
  process / set_param→`setGlobalFloat` / render + the built-in `kProgram` + the global-name param map).
  Compiles only for `ENGINE=chuck`.
- `src/engine/chuck/chuck_alloc.cpp` — **DONE (M2).** The `--wrap` malloc shim over a 12 MB
  `.sdram_bss` pool. Reuses the engine-agnostic `CsoundPool` (`engine/csound/csound_pool.h`) rather than
  duplicating it (roadmap: promote that to a shared `SdramPool`; reused as-is to keep csound byte-identical).
- `pod/harness_chuck.cpp` + `pod/Makefile.chuck` — **DONE (M1).** The standalone Pod proof-of-life.
- VTOR inject — **shared:** the `ENGINE=chuck` branch links `src/engine/csound/spotykach_qspi_vtor.cpp`
  directly (the BOOT_QSPI vector-table fix is engine-agnostic); no chuck copy.
- `src/engine/chuck/chuck_patch.h` — **DONE (M3).** numbered `chuck/<n>.ck` bank: path / scan / Alt+PITCH
  quantizer / read+normalize with built-in fallback (header-only, host-tested via `test_chuck_patch.cpp`).
- `chuck_reload.h` — **not created (M3).** The `ReloadGate` is engine-agnostic; `chuck_engine.h` reuses
  `engine/csound/csound_reload.h` directly (like `chuck_alloc.cpp` reuses `CsoundPool`). No duplicate
  file or test. (Roadmap: promote both out of `csound/` into a shared location.)
- `src/engine/chuck/chuck_midi.h` — *M4.* note→global mapping + the lock-free SPSC note ring.

---

## See also

- [`docs/dev/csound-impl.md`](csound-impl.md) — the shipped sibling engine; the primary reference for
  every reused mechanism (heap, QSPI, reload gate, SD bank, MIDI ring, debugging).
- [`docs/engines/csound.md`](../engines/csound.md) — the user-facing model `docs/engines/chuck.md`
  should follow.
- [ccrma/chuck](https://github.com/ccrma/chuck) · [WebChucK](https://github.com/ccrma/webchuck)
  (the bare-metal-shaped build template) · [ChucK docs](https://chuck.stanford.edu).
