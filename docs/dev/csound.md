# Dev notes — Csound on Daisy (`csound-poc` + `CsoundEngine`)

Notes for running [Csound](https://csound.com) 7 as a synthesis engine on Daisy hardware. This is an experimental track that lives **outside** the normal sk-engines SRAM firmware: Csound is far too large for the 186 KB `SRAM_EXEC` budget the spotykach engines link into, so it runs as a **QSPI-flash app** with its heap in SDRAM. The `IEngine` wrapper exists and works; what differs from the other engines is only the memory and boot model.

Development is on a **Daisy Pod** (a scratch board): a clean board to flash freely, and the audio Pod knobs map straight to Csound control channels.

## Status

- **Proof of life (done):** the official `DaisyCsoundGenerative` example plays on hardware — the whole chain (cross-compiled `libcsound.a`, QSPI/SDRAM layout, boot, audio path) is proven.
- **Foundation + control (done):** an own orchestra (band-limited saw) with the Pod's two knobs driving pitch and level via Csound control channels.
- **`CsoundEngine : IEngine` + thin harness (built; hardware test pending):** Csound wrapped behind the real sk-engines contract, driven by a small harness that stands in for the platform.
- **Open:** confirm the harness on hardware; richer orchestras (filter/envelope, MIDI note in); SD-loaded orchestras; and the spotykach-hardware question below.

## Memory and boot model (why this is a separate target)

Csound's linked code is ~2 MB and it allocates heavily at compile/init, so it cannot use the SRAM engines' model (code in 186 KB `SRAM_EXEC`, copied in by the SRAM bootloader). Instead it is a **`BOOT_QSPI`** app:

- code in **QSPI flash** (8 MB), executed in place;
- heap in **SDRAM** (64 MB), via the Csound port's custom linker script;
- flashed to QSPI at `0x90040000` through the Daisy bootloader's DFU.

A subtle but important point about the bootloader: **both** `BOOT_SRAM` and `BOOT_QSPI` apps are flashed to the *same* QSPI address (`0x90040000`) through the *same* Daisy bootloader DFU. The only difference is where the app is linked to run — `BOOT_SRAM` is copied QSPI→SRAM and run at `0x24000000`; `BOOT_QSPI` runs in place from QSPI. The stock Daisy bootloader does both, deciding from the app's vectors.

This means running Csound on the **spotykach** board may not require changing its (fixed) bootloader at all — only building Csound as `BOOT_QSPI` and flashing it through the normal engine-flashing flow. Two things gate that, and neither is the `IEngine` interface:

1. **Is `bootloader-spotykach-v2` the stock Daisy bootloader or SRAM-only?** If it was customized to only copy-to-SRAM, a QSPI app fails. Likely fine (the split is app-side), but unverified. A `BOOT_QSPI` Csound image flashed to the board (reversible — reflash any engine) settles it.
2. **Hardware.** A spotykach build needs *its* codec/audio config and *its* controls/pads/display, not the Pod's. That's app work.

So the separation is a build/memory model, not an `IEngine` limitation. The wrapper slots into a QSPI-capable target; the SRAM engine bundle is just a different target.

## Building `libcsound.a`

The Csound 7 source under `thirdparty/csound/` ships an official `Daisy/` port (toolchain file, `Custom.cmake`, examples, custom linker script, v5.4 bootloader). To cross-compile the static library (needs `arm-none-eabi-gcc` and CMake):

```
cd thirdparty/csound
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../Daisy \
         -DCUSTOM_CMAKE=../Daisy/Custom.cmake \
         -DCMAKE_TOOLCHAIN_FILE=../Daisy/crosscompile.cmake
make -j && make install
```

Installs `libcsound.a` (single precision, bare-metal) and headers under `thirdparty/csound/Daisy/{lib,include}`.

## Building and flashing the harness

The build reuses the example's recipe — same custom SDRAM-heap linker script, same v5.4 bootloader, same `BOOT_QSPI` — plus `-std=gnu++17` (the sk-engines contract uses `std::clamp`; libDaisy defaults to gnu++14) and `-I../src` for the contract headers. The Csound examples expect `libDaisy`/`DaisySP` next to them, so the repo copies are symlinked in:

```
thirdparty/csound/Daisy/libDaisy -> lib/libDaisy
thirdparty/csound/Daisy/DaisySP  -> lib/DaisySP
```

Build and flash from `csound-poc/`:

```
make
while ! make program-dfu; do sleep 0.2; done   # then tap RESET to catch the DFU window
```

Wait for `File downloaded successfully`, Ctrl-C, reset. A trailing `Error during download get_status` is the benign `:leave` handshake after a successful write.

To (re)install the v5.4 bootloader on a board: enter STM32 system DFU (hold **BOOT**, tap **RESET**, release **BOOT**) and

```
dfu-util -a 0 -s 0x08000000:leave \
  -D ../thirdparty/csound/Daisy/DaisyCsoundExamples/dsy_bootloader_v5_4.bin -d ,0483:df11
```

## How Csound maps onto IEngine

`CsoundEngine : public IEngine` overrides only the three required lifecycle methods plus `set_param`/`param`/`capabilities` (everything else has no-op defaults):

- `init(ctx)` → `csoundCreate` / `SetHostAudioIO` / options (`--ksmps` from `ctx.block_size`) / `CompileCSD` / `Start`.
- `process(in, out, n)` → de-interleave `in`→`spin`, `csoundPerformKsmps`, `spout`→de-interleave `out`. `ksmps == block`, so one k-cycle per block. Daisy's non-interleaving buffers (`const float* const*` / `float**`) match `IEngine::process` with no glue.
- `set_param(id, deck, v)` → `csoundSetControlChannel(name, v)`; the orchestra reads it with `chnget`. The orchestra is the param vocabulary — swap the `.csd`, swap the synth, no C++ change.

The heap comes from the QSPI linker script (newlib `_sbrk` into SDRAM), so unlike the SRAM engines there is no bump-allocator dance and `ctx.arena` is left free.

`csound-poc/harness.cpp` is the thin stand-in for the platform: it builds an `EngineContext`, `init()`s the engine, forwards the audio callback to `process()`, and drives `set_param(ParamId::Speed/Mix, DeckRef::A, knob)` from the Pod's two knobs.

## Gotchas

These each cost real debugging time.

- **Bootloader v5.4, not v6.4.** v5.4 points the vector table at the QSPI app; v6.4 does not, so under v6.4 every interrupt is dead (silent audio, frozen main loop) unless the app sets `SCB->VTOR = 0x90040000` itself. Standardize on v5.4.
- **Bare `DaisySeed`, not `DaisyPod`.** Initializing through the Pod board-support class left audio silent on this board; the examples and our code use bare `DaisySeed`.
- **Trigger instruments with `schedule(...)` in the orchestra, not a score `i` event.** A `<CsScore>` `i 1 0 -1` did not fire and produced silence; `schedule(1, 0, 100000)` works.
- **Prefer table-less oscillators.** `poscil`+`ftgen` was silent too (table not present at init?); `oscils` (init-rate) and `vco2` (k-rate) need no table. The `ftgen` question is still open.
- **Large `ksmps`.** At `ksmps=32` Csound's per-cycle overhead overruns the CPU (works briefly, then the audio ISR starves everything). Use a block of >=128 (256 proven).
- **No console.** With no serial and the Seed LED not clearly visible on the Pod, the reliable debug signal is audio: synthesize a tone in C to test the boot/output path, and play a distinct pitch as a "compile failed" flag.

## Files

- `src/engine/csound/csound_engine.{h,cpp}` — `CsoundEngine : IEngine`. Builds only in the QSPI target (needs `csound.h`/`libcsound.a`); not in `engine_select.h` / the SRAM bundle.
- `csound-poc/harness.cpp` — thin QSPI harness driving `CsoundEngine` from the Pod knobs.
- `csound-poc/Makefile` — `BOOT_QSPI`, custom SDRAM-heap linker script, v5.4 bootloader, `gnu++17`, links `libcsound.a` + the engine source.
- `thirdparty/csound/` — Csound 7 with the official `Daisy/` port (toolchain, `Custom.cmake`, examples, linker script, bootloader).
