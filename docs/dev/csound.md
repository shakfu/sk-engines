# Dev notes — Csound on Daisy (`csound-poc`)

Notes for running [Csound](https://csound.com) 7 as a synthesis engine on Daisy hardware. This is an experimental track that lives **outside** the normal sk-engines SRAM firmware: Csound is far too large for the 186 KB `SRAM_EXEC` budget the spotykach engines link into, so it runs as a QSPI-flash app with its heap in SDRAM. Treat it as its own firmware target, not a drop-in `IEngine`.

Development is on a **Daisy Pod** (a scratch board), because Csound needs the stock Daisy QSPI bootloader and the spotykach hardware permanently carries the SRAM bootloader — the two cannot coexist on one board.

## Status

- **Proof of life (done):** the official `DaisyCsoundGenerative` example plays on hardware; the whole chain — cross-compiled `libcsound.a`, QSPI/SDRAM layout, boot, and the audio path — is proven.
- **Foundation app (done):** `csound-poc/` is our own app — a band-limited saw whose pitch and level are driven live by the Pod's two knobs, via Csound control channels. This is the base to grow from.
- **Open:** richer orchestras (filter/envelope, MIDI note input), the Pod's buttons/encoder, and the eventual decision on how (or whether) to fold this into sk-engines as a first-class engine.

## Why it is separate from the SRAM engines

The spotykach engines link their code into `SRAM_EXEC` (186 KB) and boot from the SRAM bootloader for speed. A minimal `libcsound.a` is hundreds of KB of code and Csound allocates heavily at compile and init, so it needs:

- code in **QSPI flash** (8 MB) — `BOOT_QSPI`, not `BOOT_SRAM`;
- a **heap in SDRAM** (64 MB) — provided by the Csound port's custom linker script;
- the **stock Daisy bootloader** at `0x90040000`, not the spotykach SRAM bootloader.

This is a different memory and boot model from every other engine, which is why it is a standalone build under `csound-poc/` rather than an entry in `engine_select.h`.

## Building `libcsound.a`

The Csound 7 source under `thirdparty/csound/` ships an official `Daisy/` port (toolchain file, `Custom.cmake`, examples, the custom linker script, and the v5.4 bootloader). To cross-compile the static library (needs `arm-none-eabi-gcc` and CMake):

```
cd thirdparty/csound
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../Daisy \
         -DCUSTOM_CMAKE=../Daisy/Custom.cmake \
         -DCMAKE_TOOLCHAIN_FILE=../Daisy/crosscompile.cmake
make -j && make install
```

This installs `libcsound.a` (single precision, bare-metal, ~3.7 MB archive) and headers under `thirdparty/csound/Daisy/{lib,include}`. The build is single precision (`USE_DOUBLE=OFF`), bare metal (`BARE_METAL=ON`), with all desktop audio/MIDI backends off.

## Building and flashing the app

The app build reuses the example's exact recipe — same custom linker script (heap in SDRAM), same v5.4 bootloader, same `BOOT_QSPI`. The Csound examples expect `libDaisy` and `DaisySP` next to them, so the repo copies are symlinked into place:

```
thirdparty/csound/Daisy/libDaisy -> lib/libDaisy
thirdparty/csound/Daisy/DaisySP  -> lib/DaisySP
```

Build the app from `csound-poc/`:

```
cd csound-poc
make
```

Flashing is a two-step DFU dance on the Pod. The bootloader is installed once into internal flash; the app is then written to QSPI through it.

- **Install the v5.4 bootloader** (one time, or to recover): enter the STM32 system bootloader (hold **BOOT**, tap **RESET**, release **BOOT**), then write it to internal flash:

  ```
  dfu-util -a 0 -s 0x08000000:leave \
    -D ../thirdparty/csound/Daisy/DaisyCsoundExamples/dsy_bootloader_v5_4.bin -d ,0483:df11
  ```

- **Flash the app to QSPI**: tap **RESET** to enter the Daisy bootloader's DFU window, then catch it with a retry loop:

  ```
  while ! make program-dfu; do sleep 0.2; done
  ```

  Wait for `File downloaded successfully`, then Ctrl-C. A trailing `Error during download get_status` is benign — it is the `:leave` handshake after a successful write, and the device has already left DFU.

## Integration pattern

The app follows the official examples. The shape is small:

- **Setup:** `csoundCreate(NULL, NULL)`, `csoundSetHostAudioIO`, options `-n` / `--ksmps=512` / `-dm0`, then `csoundCompileCSD(text, 1, 0)` and `csoundStart`.
- **Audio:** the orchestra runs at `ksmps=512` while the audio block is 256, so the callback performs one k-cycle only when its running index into `spout` wraps (every other block), and de-interleaves `spout` into `out[0]`/`out[1]`. A large `ksmps` matters: at `ksmps=32` Csound's per-cycle overhead overruns the CPU.
- **Control:** the Pod's two knobs (Seed pins `D21`, `D15`) are read in the main loop and pushed with `csoundSetControlChannel("knob1"/"knob2", value)`; the orchestra reads them with `chnget`.

## Gotchas

These each cost real debugging time and are worth remembering.

- **Bootloader v5.4, not v6.4.** v5.4 points the vector table at the QSPI app for us. v6.4 does not, so under v6.4 the app boots but every interrupt (SysTick, audio DMA) is dead — silent audio and a frozen main loop — unless the app sets `SCB->VTOR = 0x90040000` itself. Standardize on v5.4.
- **Bare `DaisySeed`, not `DaisyPod`.** Initializing through the Pod board-support class left the audio output silent on this board; the examples use bare `DaisySeed` and so do we.
- **Trigger instruments with `schedule(...)` in the orchestra, not a score `i` event.** A `<CsScore>` `i 1 0 -1` event did not fire and produced silence; `schedule(1, 0, 100000)` in the orchestra works.
- **Prefer table-less oscillators.** `poscil` with an `ftgen` table was also silent (table not present at init?); `oscils` (init-rate) and `vco2` (k-rate) need no table and work. The `ftgen` question is still open.
- **No console.** With no serial and the Seed LED not clearly visible on the Pod, the reliable debug signal is audio itself: synthesize a tone in C to test the boot/output path, and play a distinct pitch as a "compile failed" flag.

## Files

- `csound-poc/app.cpp` — the app (DaisySeed init, buffered Csound callback, knobs to control channels).
- `csound-poc/app.h` — the embedded orchestra (`csdText`).
- `csound-poc/Makefile` — `BOOT_QSPI`, the custom SDRAM-heap linker script, v5.4 bootloader, links `libcsound.a`.
- `thirdparty/csound/` — Csound 7 with the official `Daisy/` port (toolchain, `Custom.cmake`, examples, linker script, bootloader).
