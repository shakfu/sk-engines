# Dev notes — Csound as an sk-engines engine (`ENGINE=csound`)

Running [Csound](https://csound.com) 7 as a synthesis engine on Daisy hardware. Csound is a runtime audio compiler/interpreter: an orchestra (`.csd` / `.orc` text) is compiled at runtime and performed. This engine wraps it behind the real sk-engines `IEngine` contract.

It is **not** an SRAM engine. Csound's linked code is ~2 MB — far over the 186 KB `SRAM_EXEC` budget — so it runs as a **`BOOT_QSPI`** app (code executes in place from QSPI flash). Everything else about the contract (params, display, pads) is the same as any other engine; only the memory/boot model differs.

**Status (2026-06-18): working on real hardware.** Csound synthesizes on the **spotykach** board from QSPI, driven through `IEngine` — panel controls move the synth, the rings show an output level meter, the Play LEDs show running state. Also runs on a **Daisy Pod** (the quick-iterate board). The whole chain is proven: cross-compiled `libcsound.a`, QSPI execution, the dual-heap fix, audio, controls, panel.

---

## Quick start

Build + flash the spotykach (board in DFU before the build finishes; one `dfu-util`, no retry loop):

```
make engine-csound      # clean + build the QSPI image + flash
make program-csound      # re-flash the last build without rebuilding
```

Both expand to `ENGINE=csound APP_TYPE=BOOT_QSPI LDSCRIPT=alt_qspi.lds` + the core `program-dfu` (`dfu-util -a 0 -s 0x90040000:leave -D build/spotykach.bin -d ,0483:df11`). The leading `-` on the flash step swallows the benign `Error 74` (the `get_status` on the QSPI `:leave` — the write succeeds).

Prerequisite, once: build `libcsound.a` (see "Building libcsound.a" below). Recover the board anytime by flashing any normal engine.

**Loading a patch (optional).** Drop a full CSD document at `/csound/patch.csd` on the SD card and it is compiled at boot in place of the built-in orchestra (the centre mode LED turns cyan). The patch should `chnget` the engine's control channels (`speedA`/`mixA`/`sizeA`/`envA`, and the `B` deck) to be driven by the panel knobs — see `kOrchestra` in `csound_engine.cpp` for the template. No card, no file, a non-CSD file, or one that fails to compile all fall back to the built-in, so the engine always makes sound. (Constraints: a full `<CsoundSynthesizer>` document, ≤ 64 KB, core opcodes only — no plugins or soundfile I/O; see Limitations.)

---

## How Csound maps onto IEngine

`CsoundEngine : public IEngine` (`src/engine/csound/csound_engine.{h,cpp}`) overrides only what it uses; everything else keeps the no-op defaults.

- `init(ctx)` → arm the SDRAM allocator (see below) → pick the orchestra text (an SD `/csound/patch.csd` if present + valid, else the built-in `kOrchestra`; `select_orchestra()` in `csound_patch.h`) → `try_compile()`: `csoundCreate` / `csoundSetHostAudioIO` / options (`-n`, `--ksmps` from `ctx.block_size`, `-dm0`) / `csoundCompileCSD(text,1,0)` / `csoundStart`. If an SD patch fails to compile, it retries with the built-in so a bad card never costs audio.

- `process(in, out, n)` → de-interleave `in`→`spin`, `csoundPerformKsmps`, `spout`→de-interleave `out`, and track an output peak for the meter. `ksmps == block`, so one k-cycle per block.

- `set_param(id, deck, v)` → `csoundSetControlChannel(name, v)`; the orchestra reads it with `chnget`. `channel_for()` maps `ParamId::Speed/Mix/Size/Env` (+ deck) → channel names (`speedA`, `mixA`, …). **The orchestra is the param vocabulary** — swap the `.csd`, swap the synth, no C++ change.

- `handle_midi_note(channel, note)` → channel picks the deck (the platform's configured MIDI channels), `note` becomes a frequency, and a Csound event instantiates the orchestra's `MidiNote` instrument (`csoundEvent(CS_INSTR_EVENT, {instr,0,dur,freq,deck})`). The platform delivers **NoteOn only** (no NoteOff, no velocity), so notes are finite/self-terminating. Because `handle_midi_note` runs in the main loop and `csoundPerformKsmps` in the audio ISR, the handler only **enqueues** onto a lock-free SPSC ring (`csound_midi.h`); `process()` drains it inside the ISR right before performing, so all Csound allocation stays single-threaded (no race on the SDRAM pool). A patch opts into being keyboard-playable by defining `instr MidiNote`; the engine looks it up by name (`csoundGetInstrNumber`) and drops MIDI when it's absent.

- `render(m)` → both rings show the output level meter; Play indicators show running state.

The orchestra is loaded from the card when `/csound/patch.csd` is present (a full CSD document), else it is the compiled-in `kOrchestra` in `csound_engine.cpp`: a `vco2` saw drone (knob1→pitch, knob2→level, knob3→cutoff, triggered by `schedule(1,0,100000)`) plus an `instr MidiNote` (a finite `vco2` pluck reading p4=freq) so the built-in is keyboard-playable out of the box. An SD patch should name the same control channels (`speedA`/`mixA`/`sizeA`/`envA` and the `B` deck) so the platform knobs drive it, and define `instr MidiNote` to be MIDI-playable; see `channel_for()` and `kMidiInstrName`. The centre mode LED shows the source: cyan = SD patch, white = built-in.

### The heap (the thing that took the longest)

Two heaps, deliberately:
- The **platform's** default heap stays in **SRAM** (`alt_qspi.lds`). It must — global constructors `malloc` *before* `_hw.Init()` powers up the SDRAM controller, so a heap in SDRAM faults there.

- **Csound's** heap is a 12 MB **SDRAM pool** (`src/engine/csound/csound_alloc.cpp`), reached by linking `--wrap=malloc/free/calloc/realloc` and gating on a flag that `CsoundEngine::init()` arms (`csound_heap_arm()`) *after* `_hw.Init()`. Csound's MBs at create/compile land in SDRAM; the platform's allocations stay in SRAM. The pool is a **free-capable coalescing allocator** (`CsoundPool`, `src/engine/csound/csound_pool.h`) — a boundary-tag allocator with segregated free lists — so `free`/`realloc` reclaim and coalesce, and a `csoundReset` + recompile returns its megabytes to the pool (this is what makes patch-swapping sustainable; it replaced the original bump pool, which never freed). On pool exhaustion the shim falls back to the SRAM heap so a request never hard-fails, and `in_pool()` routes each pointer's `free`/`realloc` back to whichever heap it came from. The allocator is host-tested in isolation (`make -C host test-csound-alloc`), since it has no `libcsound` dependency.

---

## Memory & boot model

- Code in **QSPI flash** (`0x90040000`, 8 MB), executed in place. Data/bss in SRAM/DTCM; Csound heap in the SDRAM pool; platform heap in SRAM.

- `alt_qspi.lds` = `alt_sram.lds` with code + `.data` load-address moved `SRAM_EXEC → QSPIFLASH`. The abandoned `alt_qspi_csound.lds` (heap-in-SDRAM via the linker) is gone — it caused the ctor crash above.

- `csound-poc/spotykach_qspi_vtor.cpp` — an `init_priority(101)` global ctor that sets `SCB->VTOR = 0x90040000`. Needed because the startup skips `SystemInit` under `BOOT_APP` and the bootloader may not point the vector table at the QSPI app; without it SysTick + audio DMA are dead.

**Bootloader fact (verified):** the spotykach `v2` bootloader **runs QSPI apps** — proved by building `radio` as `BOOT_QSPI` and booting it on the board. Both `BOOT_SRAM` and `BOOT_QSPI` apps flash to the same `0x90040000` via the same Daisy DFU; the bootloader decides copy-to-SRAM vs run-in-place from the app's vectors. So no bootloader change is needed to run Csound on the spotykach.

---

## Building `libcsound.a`

`thirdparty/csound/` is Csound 7 with the official `Daisy/` port (toolchain, `Custom.cmake`, examples, linker script, v5.4 bootloader). Needs `arm-none-eabi-gcc` + CMake:

```
cd thirdparty/csound
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../Daisy \
         -DCUSTOM_CMAKE=../Daisy/Custom.cmake \
         -DCMAKE_TOOLCHAIN_FILE=../Daisy/crosscompile.cmake
make -j && make install
```

Installs `libcsound.a` (single precision, bare-metal) + headers to `thirdparty/csound/Daisy/{lib,include}`. `thirdparty/csound/` is gitignored (115 MB); rebuild from this recipe. The `ENGINE=csound` Makefile branch points `-I` / link at `thirdparty/csound/Daisy/{include,lib}`.

The **Pod** dev target (`csound-poc/`, bare `DaisySeed`, no front panel) predates the spotykach build; keep it for quick iteration. It needs `thirdparty/csound/Daisy/{libDaisy,DaisySP}` symlinked to `lib/`.

---

## Debugging (SWD / ST-Link)

There is **no console** in the QSPI build (see the Gotchas), so bring-up has been done with audio as the debug signal. A SWD probe (ST-Link V2/V3, or a J-Link) is a large upgrade for the failures that audio can't show — pre-`main` ctor crashes, hardfaults, ISR hangs, and `csound*` return codes — and it is the recommended tool for the perf pass (roadmap #4) and any allocator/patch-swap crashes. The Daisy Seed exposes **SWDIO/SWCLK** pads; ST-Link + OpenOCD + Cortex-Debug (VS Code) works.

**QSPI execute-in-place changes the workflow** — it is not the usual "probe flashes and runs":

- **Attach, don't flash.** The ST-Link internal-flash loader can't place a `BOOT_QSPI` XIP image. Keep the DFU flow (`make engine-csound` / `program-csound`) to put the image at `0x90040000`, then **attach to the running target** and load symbols from `build/spotykach.elf`. The QSPI controller must already be in memory-mapped mode for the probe to read/step that code — the Daisy bootloader does this at boot, so attach **after** boot (or teach OpenOCD to init QSPI).
- **Hardware breakpoints only.** Software breakpoints patch a `BKPT` into the instruction, but QSPI code is memory-mapped read-only flash the debugger can't write through. You are limited to the Cortex-M7 FPB comparators (~6-8) — enough, but you can't place them freely.
- **Bootloader / `VTOR`.** The probe sees the app only after the bootloader hands off; the `spotykach_qspi_vtor.cpp` inject and the v2 bootloader don't interfere with a debug session.

**Highest-leverage uses here:** halt on the fault handler and read `CFSR`/`HFSR`/`BFAR` to pin a crash in seconds (the SDRAM-heap-vs-ctor bug was exactly this shape); break after `csoundStart` to inspect the return codes that are otherwise invisible; and inspect ftable/instr state for the silent-`ftgen` mystery.

**Cheaper first step (no soldering):** the platform already has a USB-CDC path (the `METER=1` meter). Wiring that same serial as a log channel for the csound build gets you compile status + `printf` for the *non-crash* questions; the probe earns its keep specifically for failures **before serial is up** (ctors, ISRs, hardfaults). Alternatively, SWO/ITM `printf` over the trace pin gives a console through the probe.

---

## Limitations & potential

### Opcodes — limited by *category*, not count

What ships in our `libcsound.a` is the **entire statically-linked core** opcode library (oscillators, filters, envelopes, delays, reverbs, the `pvs` spectral/FFT family, granular, waveguide/physical models, … — well over a thousand opcodes). The exclusions are structural, from `Daisy/Custom.cmake` + bare metal:

- **No plugin / external opcodes.** Desktop Csound `dlopen`s opcode libraries at runtime; bare metal has no dynamic loading, so only statically-compiled opcodes exist. (Biggest limit.)

- **No soundfile I/O** (`USE_LIBSNDFILE=OFF`): `diskin`/`soundin`/`fout`, `GEN01` (sample-from-WAV) are out. Table-generating GENs (GEN10 sine, etc.) work.

- **OSC and deprecated opcodes disabled**; MIDI-/audio-*device* opcodes gone (the host feeds I/O).

- Real ceilings: **CPU** (QSPI execute-in-place is slower than SRAM) and **memory** (the 12 MB pool).

To get the definitive list of registered opcodes for *this* build: `csoundNewOpcodeList`.

### vs desktop Csound

Same **language and core DSP**, minus the hosted-OS layer: single precision (`float`, not `double`); host does all I/O (`spin`/`spout` + control channels, no devices/disk); no filesystem, threads, plugins, utilities, or CLI; CPU/memory constrained. An orchestra built from core opcodes + compiled-in tables behaves *identically* within float precision and the CPU budget — it's real Csound, not a reduced dialect.

### Interpreter / patch-swapping — yes, with a caveat

Csound *is* a runtime interpreter: `csoundCompileOrc`/`csoundCompileCSD` compile orchestra text at runtime, and Csound supports **live recompilation** (add/replace instruments while playing). So we can compile a `.csd` from a string today, and from the **SD card** with a little wiring — a real patch player.

**Caveat (resolved):** the heap used to be a bump pool with no free, so compiling a fresh patch leaked MBs — a handful of swaps before the 12 MB pool exhausted. It is now a **free-capable coalescing allocator** (`CsoundPool`, roadmap #2 below, done), so `csoundReset` + recompile reclaims its memory and sustained patch-swapping is no longer pool-bound. What remains for live patch-swapping is the wiring in #1 (load `.csd` from SD and recompile) — the allocator dependency is cleared.

---

## Roadmap (next session)

Ordered by leverage. **#1, #2, and #3 are done** (SD patches, the free-capable allocator, and MIDI-in). The remaining unknown that sizes everything is **#4** (QSPI-XIP CPU headroom) — do it next; then **#5** (live patch switching + UI depth) reaches the full version (an SD patch instrument played over MIDI). All three done items are unit-proven off-target but **pending on-hardware verification** in the real Csound path.

1. [x] **SD-loaded `.csd` patches. — DONE.** `init()` reads `/csound/patch.csd` from the card (`ctx.stream->read_text`) into a scratch buffer carved from the SDRAM pool, validates it looks like a CSD, and compiles it; absent/invalid/uncompilable falls back to the built-in `kOrchestra` (so a card-less unit always sounds). The selection logic is host-tested (`make -C host test-csound-patch`); the compile is QSPI-only. *Still ahead:* loading a `.csd` is a boot-time choice today — live **switching** between patches (re-`csoundCompileCSD` from a new file at runtime, which #2 now makes memory-safe) is roadmap #5 (the Alt+PITCH selector + a main-loop reload). The selector takes one well-known path; #5 generalises it to a set.

2. [x] **Real SDRAM allocator. — DONE.** The bump pool in `src/engine/csound/csound_alloc.cpp` is replaced by `CsoundPool` (`src/engine/csound/csound_pool.h`): a boundary-tag coalescing allocator with segregated free lists, so `free`/`realloc` reclaim and coalesce and a `csoundReset` + recompile returns its memory to the pool. The `--wrap` + arm-in-init gating is unchanged; only the pool internals changed. Host-tested off-target (`make -C host test-csound-alloc`: alignment, splitting, forward/backward coalescing, in-place grow/shrink, exhaustion, and a 4000-op churn that drains back to a single max block). *Pending: on-hardware verification that a live patch recompile reclaims as expected (needs #1 to drive it).* If fragmentation ever bites under sustained swapping, swap the internals for TLSF behind the same interface.

3. **MIDI in. — DONE.** `handle_midi_note(channel, note)` resolves channel→deck (Config MIDI channels), maps `note`→Hz, and instantiates the orchestra's `MidiNote` instrument via `csoundEvent(CS_INSTR_EVENT, {instr,0,dur,freq,deck})`. Constraints from the platform: **NoteOn only, no velocity, no NoteOff** → notes are finite (a fixed-duration pluck; no hold/sustain). The handler runs in the main loop but Csound performs in the ISR, so it enqueues onto a lock-free SPSC ring (`csound_midi.h`) that `process()` drains in the ISR before `csoundPerformKsmps` — all Csound allocation stays single-threaded (closes the pool-concurrency hazard flagged in #2). Host-tested (`make -C host test-csound-midi`: note→Hz, p-field builder, ring FIFO/drop/wraparound, and a 200k-note cross-thread SPSC run); the `csoundEvent`/`csoundGetInstrNumber` calls compile against the real `csound.h` for ARM. *Pending: on-hardware confirmation that runtime events fire (the score-`i` gotcha was about the static score; `csoundEvent` is the realtime queue, but verify).* Not yet: sustain/NoteOff (the platform delivers neither), MIDI CC→channels, per-note velocity.

4. **Performance pass (do early — it sizes everything).** Measure CPU headroom from QSPI XIP with `make engine-csound METER=1` (the platform's CPU meter over USB serial). If a real orchestra glitches: raise `ksmps`, simplify, or pin hot code to ITCM/SRAM (hard for a static lib — investigate the linker moving Csound's hot `.text` into SRAM in the QSPI build, since SRAM_EXEC is free there).

5. **Control / UI depth.** Expand `channel_for()` (more knobs/params); use `CapAux` (Alt+PITCH) for **patch selection**; map pads → score events; show patch name/params in `render()` (level meter is in).

6. **Opcode audit.** Dump `csoundNewOpcodeList`, document the available set, and optionally static-link specific high-value plugin opcodes into the `Custom.cmake` build.

---

## Gotchas (each cost real debugging time)

- **The SDRAM-heap-vs-ctor crash.** A heap in SDRAM (`alt_qspi_csound.lds`) crashed at boot: global ctors `malloc` before `_hw.Init()` powers up the FMC. Fix = dual heap (platform SRAM + Csound's `--wrap`'d SDRAM pool armed after init). This is the central lesson; see "The heap" above.

- **Bootloader v5.4 (Pod) / v2 (spotykach), not v6.4.** v6.4 doesn't point VTOR at the QSPI app → dead interrupts. The `spotykach_qspi_vtor.cpp` inject makes the app bootloader-agnostic; v5.4/v2 work. v6.4 (not well tested)

- **Bare `DaisySeed`, not `DaisyPod`** on the Pod — DaisyPod init left audio silent. (The spotykach build uses the platform's own `_hw` init, which is correct for that board.)

- **Trigger instruments with `schedule(...)` in the orchestra, not a score `i` event** (the score event didn't fire → silence).

- **Prefer table-less oscillators** (`vco2`, `oscils`); `poscil`+`ftgen` was silent (ftgen-at-init still unexplained — worth revisiting).

- **Large `ksmps`** (>=128; 256 proven). At `ksmps=32` Csound's per-cycle overhead overruns the audio ISR.

- **No console.** With no serial, use audio as the debug signal (synthesize a C tone to test the boot/output path; a distinct pitch as a "compile failed" flag).

---

## Files

- `src/engine/csound/csound_engine.{h,cpp}` — `CsoundEngine : IEngine` (init/`try_compile`/process/set_param/param/render

  + the built-in fallback orchestra + the `channel_for` param map). Compiles only for `ENGINE=csound`.

- `src/engine/csound/csound_patch.h` — `select_orchestra()`: read `/csound/patch.csd` from SD, validate, else fall back to the built-in (header-only, host-tested via `host/test_csound_patch.cpp`).
- `src/engine/csound/csound_midi.h` — MIDI note→Hz, the CS_INSTR_EVENT p-field builder, and the lock-free SPSC note ring (main loop → audio ISR) (header-only, host-tested via `host/test_csound_midi.cpp`).
- `src/engine/csound/csound_alloc.cpp` — the `--wrap` shim: the 12 MB `.sdram_bss` pool array, the arm-after-`_hw.Init()` gate, and the malloc-family routing (pool when armed, SRAM fallback otherwise).
- `src/engine/csound/csound_pool.h` — `CsoundPool`, the free-capable coalescing allocator the shim drives (header-only, host-tested via `host/test_csound_alloc.cpp`).

- `csound-poc/spotykach_qspi_vtor.cpp` — the `SCB->VTOR` inject (global ctor).

- `alt_qspi.lds` — QSPI-execution linker script (code in QSPI, heaps as above).

- `Makefile` — the `ENGINE=csound` branch (defines, `libcsound.a`, `-I`, `--wrap` LDFLAGS) + `engine-csound` / `program-csound` targets.

- `src/engine/engine_select.h` — `SPK_ENGINE_CSOUND → CsoundEngine` (gated).

- `csound-poc/` — the Pod dev target (`harness.cpp`, `Makefile`) for quick iteration.

- `thirdparty/csound/` — Csound 7 + the official `Daisy/` port (gitignored; rebuild via the recipe).
