# Dev notes — Csound as an sk-engines engine (`ENGINE=csound`)

Running [Csound](https://csound.com) 7 as a synthesis engine on Daisy hardware. Csound is a runtime
audio compiler/interpreter: an orchestra (`.csd` / `.orc` text) is compiled at runtime and performed.
This engine wraps it behind the real sk-engines `IEngine` contract.

It is **not** an SRAM engine. Csound's linked code is ~2 MB — far over the 186 KB `SRAM_EXEC` budget —
so it runs as a **`BOOT_QSPI`** app (code executes in place from QSPI flash). Everything else about the
contract (params, display, pads) is the same as any other engine; only the memory/boot model differs.

**Status (2026-06-18): working on real hardware.** Csound synthesizes on the **spotykach** board from
QSPI, driven through `IEngine` — panel controls move the synth, the rings show an output level meter,
the Play LEDs show running state. Also runs on a **Daisy Pod** (the quick-iterate board). The whole
chain is proven: cross-compiled `libcsound.a`, QSPI execution, the dual-heap fix, audio, controls, panel.

---

## Quick start

Build + flash the spotykach (board in DFU before the build finishes; one `dfu-util`, no retry loop):

```
make engine-csound      # clean + build the QSPI image + flash
make program-csound      # re-flash the last build without rebuilding
```

Both expand to `ENGINE=csound APP_TYPE=BOOT_QSPI LDSCRIPT=alt_qspi.lds` + the core `program-dfu`
(`dfu-util -a 0 -s 0x90040000:leave -D build/spotykach.bin -d ,0483:df11`). The leading `-` on the
flash step swallows the benign `Error 74` (the `get_status` on the QSPI `:leave` — the write succeeds).

Prerequisite, once: build `libcsound.a` (see "Building libcsound.a" below). Recover the board anytime
by flashing any normal engine.

---

## How Csound maps onto IEngine

`CsoundEngine : public IEngine` (`src/engine/csound/csound_engine.{h,cpp}`) overrides only what it uses;
everything else keeps the no-op defaults.

- `init(ctx)` → arm the SDRAM allocator (see below) → `csoundCreate` / `csoundSetHostAudioIO` /
  options (`-n`, `--ksmps` from `ctx.block_size`, `-dm0`) / `csoundCompileCSD(text,1,0)` / `csoundStart`.
- `process(in, out, n)` → de-interleave `in`→`spin`, `csoundPerformKsmps`, `spout`→de-interleave `out`,
  and track an output peak for the meter. `ksmps == block`, so one k-cycle per block.
- `set_param(id, deck, v)` → `csoundSetControlChannel(name, v)`; the orchestra reads it with `chnget`.
  `channel_for()` maps `ParamId::Speed/Mix/Size/Env` (+ deck) → channel names (`speedA`, `mixA`, …).
  **The orchestra is the param vocabulary** — swap the `.csd`, swap the synth, no C++ change.
- `render(m)` → both rings show the output level meter; Play indicators show running state.

The orchestra is currently a compiled-in string (`kOrchestra` in `csound_engine.cpp`): a `vco2` saw
with knob1→pitch, knob2→level, knob3→cutoff, triggered by `schedule(1,0,100000)`.

### The heap (the thing that took the longest)

Two heaps, deliberately:
- The **platform's** default heap stays in **SRAM** (`alt_qspi.lds`). It must — global constructors
  `malloc` *before* `_hw.Init()` powers up the SDRAM controller, so a heap in SDRAM faults there.
- **Csound's** heap is a 12 MB **SDRAM bump pool** (`src/engine/csound/csound_alloc.cpp`), reached by
  linking `--wrap=malloc/free/calloc/realloc` and gating on a flag that `CsoundEngine::init()` arms
  (`csound_heap_arm()`) *after* `_hw.Init()`. Csound's MBs at create/compile land in SDRAM; the
  platform's allocations stay in SRAM. **The pool never frees** (bump allocator) — fine for one
  patch, but see the roadmap for why patch-swapping needs a real allocator.

---

## Memory & boot model

- Code in **QSPI flash** (`0x90040000`, 8 MB), executed in place. Data/bss in SRAM/DTCM; Csound heap
  in the SDRAM pool; platform heap in SRAM.
- `alt_qspi.lds` = `alt_sram.lds` with code + `.data` load-address moved `SRAM_EXEC → QSPIFLASH`. The
  abandoned `alt_qspi_csound.lds` (heap-in-SDRAM via the linker) is gone — it caused the ctor crash above.
- `csound-poc/spotykach_qspi_vtor.cpp` — an `init_priority(101)` global ctor that sets
  `SCB->VTOR = 0x90040000`. Needed because the startup skips `SystemInit` under `BOOT_APP` and the
  bootloader may not point the vector table at the QSPI app; without it SysTick + audio DMA are dead.

**Bootloader fact (verified):** the spotykach `v2` bootloader **runs QSPI apps** — proved by building
`radio` as `BOOT_QSPI` and booting it on the board. Both `BOOT_SRAM` and `BOOT_QSPI` apps flash to the
same `0x90040000` via the same Daisy DFU; the bootloader decides copy-to-SRAM vs run-in-place from the
app's vectors. So no bootloader change is needed to run Csound on the spotykach.

---

## Building `libcsound.a`

`thirdparty/csound/` is Csound 7 with the official `Daisy/` port (toolchain, `Custom.cmake`, examples,
linker script, v5.4 bootloader). Needs `arm-none-eabi-gcc` + CMake:

```
cd thirdparty/csound
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../Daisy \
         -DCUSTOM_CMAKE=../Daisy/Custom.cmake \
         -DCMAKE_TOOLCHAIN_FILE=../Daisy/crosscompile.cmake
make -j && make install
```

Installs `libcsound.a` (single precision, bare-metal) + headers to `thirdparty/csound/Daisy/{lib,include}`.
`thirdparty/csound/` is gitignored (115 MB); rebuild from this recipe. The `ENGINE=csound` Makefile
branch points `-I` / link at `thirdparty/csound/Daisy/{include,lib}`.

The **Pod** dev target (`csound-poc/`, bare `DaisySeed`, no front panel) predates the spotykach build;
keep it for quick iteration. It needs `thirdparty/csound/Daisy/{libDaisy,DaisySP}` symlinked to `lib/`.

---

## Limitations & potential

### Opcodes — limited by *category*, not count

What ships in our `libcsound.a` is the **entire statically-linked core** opcode library (oscillators,
filters, envelopes, delays, reverbs, the `pvs` spectral/FFT family, granular, waveguide/physical models,
… — well over a thousand opcodes). The exclusions are structural, from `Daisy/Custom.cmake` + bare metal:

- **No plugin / external opcodes.** Desktop Csound `dlopen`s opcode libraries at runtime; bare metal has
  no dynamic loading, so only statically-compiled opcodes exist. (Biggest limit.)
- **No soundfile I/O** (`USE_LIBSNDFILE=OFF`): `diskin`/`soundin`/`fout`, `GEN01` (sample-from-WAV) are
  out. Table-generating GENs (GEN10 sine, etc.) work.
- **OSC and deprecated opcodes disabled**; MIDI-/audio-*device* opcodes gone (the host feeds I/O).
- Real ceilings: **CPU** (QSPI execute-in-place is slower than SRAM) and **memory** (the 12 MB pool).

To get the definitive list of registered opcodes for *this* build: `csoundNewOpcodeList`.

### vs desktop Csound

Same **language and core DSP**, minus the hosted-OS layer: single precision (`float`, not `double`);
host does all I/O (`spin`/`spout` + control channels, no devices/disk); no filesystem, threads, plugins,
utilities, or CLI; CPU/memory constrained. An orchestra built from core opcodes + compiled-in tables
behaves *identically* within float precision and the CPU budget — it's real Csound, not a reduced dialect.

### Interpreter / patch-swapping — yes, with a caveat

Csound *is* a runtime interpreter: `csoundCompileOrc`/`csoundCompileCSD` compile orchestra text at
runtime, and Csound supports **live recompilation** (add/replace instruments while playing). So we can
compile a `.csd` from a string today, and from the **SD card** with a little wiring — a real patch player.

**Caveat:** Csound's heap is our bump pool (no free), so compiling a fresh patch allocates MBs and never
reclaims — a handful of swaps before the 12 MB pool exhausts. Sustained patch-swapping needs a
**free-capable SDRAM allocator** (roadmap #2). Not a wall, just the next dependency.

---

## Roadmap (next session)

Ordered by leverage; the recommendation is to do **#4 and #2 first** — they're the two unknowns that
decide whether the exciting version (an SD patch instrument played over MIDI, #1 + #3) is viable.

1. **SD-loaded `.csd` patches.** Load orchestras from the card instead of the compiled-in `kOrchestra`
   string. Wire `EngineContext::stream` (`IStreamDeck`) / FatFs to read the text, then
   `csoundCompileCSD`/`csoundCompileOrc`. Turns it into a patch instrument. *(Depends on #2 to swap.)*
   Code: orchestra is `kOrchestra` in `csound_engine.cpp`; `ctx.stream` is already in `EngineContext`.
2. **Real SDRAM allocator.** Replace the bump in `src/engine/csound/csound_alloc.cpp` with a
   free-capable allocator over the SDRAM pool (a free list, or drop in TLSF), so `csoundReset` +
   recompile reclaims memory. This is what makes #1 + #3 sustainable. Keep the `--wrap` + arm-in-init
   gating; only the pool internals change.
3. **MIDI in.** Map `IEngine::handle_midi_note(channel, note)` (currently no-op default) → Csound events
   (`csoundScoreEvent`/`schedule`) so patches are playable from a keyboard.
4. **Performance pass (do early — it sizes everything).** Measure CPU headroom from QSPI XIP with
   `make engine-csound METER=1` (the platform's CPU meter over USB serial). If a real orchestra glitches:
   raise `ksmps`, simplify, or pin hot code to ITCM/SRAM (hard for a static lib — investigate the linker
   moving Csound's hot `.text` into SRAM in the QSPI build, since SRAM_EXEC is free there).
5. **Control / UI depth.** Expand `channel_for()` (more knobs/params); use `CapAux` (Alt+PITCH) for
   **patch selection**; map pads → score events; show patch name/params in `render()` (level meter is in).
6. **Opcode audit.** Dump `csoundNewOpcodeList`, document the available set, and optionally static-link
   specific high-value plugin opcodes into the `Custom.cmake` build.

---

## Gotchas (each cost real debugging time)

- **The SDRAM-heap-vs-ctor crash.** A heap in SDRAM (`alt_qspi_csound.lds`) crashed at boot: global
  ctors `malloc` before `_hw.Init()` powers up the FMC. Fix = dual heap (platform SRAM + Csound's
  `--wrap`'d SDRAM pool armed after init). This is the central lesson; see "The heap" above.
- **Bootloader v5.4 (Pod) / v2 (spotykach), not v6.4.** v6.4 doesn't point VTOR at the QSPI app → dead
  interrupts. The `spotykach_qspi_vtor.cpp` inject makes the app bootloader-agnostic; v5.4/v2 work.
- **Bare `DaisySeed`, not `DaisyPod`** on the Pod — DaisyPod init left audio silent. (The spotykach build
  uses the platform's own `_hw` init, which is correct for that board.)
- **Trigger instruments with `schedule(...)` in the orchestra, not a score `i` event** (the score event
  didn't fire → silence).
- **Prefer table-less oscillators** (`vco2`, `oscils`); `poscil`+`ftgen` was silent (ftgen-at-init still
  unexplained — worth revisiting).
- **Large `ksmps`** (>=128; 256 proven). At `ksmps=32` Csound's per-cycle overhead overruns the audio ISR.
- **No console.** With no serial, use audio as the debug signal (synthesize a C tone to test the
  boot/output path; a distinct pitch as a "compile failed" flag).

---

## Files

- `src/engine/csound/csound_engine.{h,cpp}` — `CsoundEngine : IEngine` (init/process/set_param/param/render
  + the compiled-in orchestra + the `channel_for` param map). Compiles only for `ENGINE=csound`.
- `src/engine/csound/csound_alloc.cpp` — the `--wrap` SDRAM bump pool (roadmap #2 replaces the internals).
- `csound-poc/spotykach_qspi_vtor.cpp` — the `SCB->VTOR` inject (global ctor).
- `alt_qspi.lds` — QSPI-execution linker script (code in QSPI, heaps as above).
- `Makefile` — the `ENGINE=csound` branch (defines, `libcsound.a`, `-I`, `--wrap` LDFLAGS) +
  `engine-csound` / `program-csound` targets.
- `src/engine/engine_select.h` — `SPK_ENGINE_CSOUND → CsoundEngine` (gated).
- `csound-poc/` — the Pod dev target (`harness.cpp`, `Makefile`) for quick iteration.
- `thirdparty/csound/` — Csound 7 + the official `Daisy/` port (gitignored; rebuild via the recipe).
