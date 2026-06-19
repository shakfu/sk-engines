# Dev notes — ChucK as an sk-engines engine (`ENGINE=chuck`) — ROADMAP

> **Status: M0 DONE (2026-06-19) — `libchuck.a` cross-builds + links for cortex-m7. M1/M2 CODE
> LANDED + LINKS (2026-06-19); on-target flash/tone/CPU not yet done (no hardware in the loop).**
> `scripts/fetch_chuck.sh` fetches ChucK, cross-compiles the core into
> `thirdparty/chuck/Daisy/lib/libchuck.a` (≈1.1 MB `.text`), and self-verifies with an on-target link
> probe. `ChuckEngine : IEngine` (`src/engine/chuck/`) now wraps it: it compiles + links both as the
> Pod harness (`pod/harness_chuck.cpp`, `make -f Makefile.chuck` → a 1.32 MB BOOT_QSPI image) and as
> the full firmware (`make engine-chuck` → `spotykach.bin`, SRAM 65% / QSPIFLASH 17% / SDRAM 94%).
> What remains for M1/M2 is the **hardware** half: flash a Pod, confirm tone out of QSPI, and run
> `METER=1` for the double-precision/XIP CPU headroom (risk #1). See **M1/M2 — results** below.

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
| Alt+PITCH live patch swap | `removeAllShreds()` + `compileCode(newText)` behind the `ReloadGate` | `ReloadGate` + `csoundDestroy` + recompile |
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

**M1 — Pod tone from QSPI (retire risk #1 + #2). — CODE DONE / HARDWARE PENDING (2026-06-19).** The
Pod vehicle is built: `pod/harness_chuck.cpp` + `pod/Makefile.chuck` stand in for the platform (build
an `EngineContext`, `init()` the real `ChuckEngine`, drive `process()` from the audio callback, knobs
→ `set_param()`). `make -f Makefile.chuck` cross-compiles + links a BOOT_QSPI image (≈1.32 MB) against
`libchuck.a` + the SDRAM `--wrap` pool. Unlike the Csound Pod harness it links the *real* pool
(`chuck_alloc.cpp`), so `chuck_heap_arm()` arms the `.sdram_bss` pool — the M2 heap model, on the Pod.
**Still to do (needs a board):** flash it, confirm a tone, and **run `METER=1` for CPU headroom** —
the make-or-break double-precision/QSPI-XIP measurement (risk #1). If a moderate UGen graph glitches,
the perf mitigations come up front.

**M2 — `IEngine` skeleton on the spotykach. — CODE DONE / HARDWARE PENDING (2026-06-19).**
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
harness point at it, and the stock `alt_qspi.lds` (csound) is byte-unchanged. **Still to do (needs a board):**
flash, confirm the knobs drive the drone and the rings meter the output. SD bank/MIDI are M3/M4.

**M3 — SD `.ck` patch bank + live swap.** Port `csound_patch.h` (numbered `/chuck/<n>.ck` slots, the
Alt+PITCH quantizer, read+validate+BOM/CRLF-strip, built-in fallback) and `csound_reload.h`
(`ReloadGate`: `removeAllShreds()` + `compileCode()` behind the lock-free handoff). Boot auto-load of
the lowest slot.

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
- `src/engine/chuck/chuck_patch.h` — *M3.* numbered `/chuck/<n>.ck` bank: path / scan / Alt+PITCH
  quantizer / read+validate with built-in fallback (header-only, host-tested).
- `src/engine/chuck/chuck_reload.h` — *M3.* `ReloadGate` live-instance handoff (likely a thin rename of
  the shared gate).
- `src/engine/chuck/chuck_midi.h` — *M4.* note→global mapping + the lock-free SPSC note ring.

---

## See also

- [`docs/dev/csound-impl.md`](csound-impl.md) — the shipped sibling engine; the primary reference for
  every reused mechanism (heap, QSPI, reload gate, SD bank, MIDI ring, debugging).
- [`docs/engines/csound.md`](../engines/csound.md) — the user-facing model `docs/engines/chuck.md`
  should follow.
- [ccrma/chuck](https://github.com/ccrma/chuck) · [WebChucK](https://github.com/ccrma/webchuck)
  (the bare-metal-shaped build template) · [ChucK docs](https://chuck.stanford.edu).
