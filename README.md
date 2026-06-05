# sk-engines: A Spotykach (platform/engine fork)

A fork of the official [Synthux Academy Spotykach](https://synthux.academy/store/spotykach) firmware, re-architected into a fixed hardware/UI **platform** that hosts a swappable DSP **engine**. The board and its whole interaction language — multi-function knobs with pickup, ring feedback, pad gestures, transport, SD-card sample storage, CV/gate, MIDI — stay fixed; a firmware variant swaps only the parameters and DSP. The dual-deck granular looper is the reference engine (and remains the default build); a tempo-synced stereo delay, a four-drum Euclidean drum machine, and a minimal stereo passthrough ship as additional, non-granular engines that demonstrate the api. The clock/transport is itself a shared platform service, so any engine can lock to the same internal/TS4/MIDI clock. The platform is decoupled from any one engine: `src/hw/`, `src/ui/`, `src/memory/`, and `src/transport/` include zero granular headers, and the build enforces it (see `make check-boundary`).

This began as a feature-extension fork of the upstream firmware and grew into the platform/engine separation above, so future instruments can reuse the board and the interaction language rather than rewrite them. See [`docs/architecture.md`](docs/architecture.md) for the design and how to add an engine.

## Setup

Clone the repo recursively or run `git submodule update --init --recursive` to update submodules (libDaisy + DaisySP)

Note that the ws2812 driver requires a slight modification to libDaisy, as such the libDaisy submodule commit ref points at a specific branch within the bleeptools fork (which is based on Infrasonic Audio fork), containing a few other modifications as well, namely within the MIDI and mpr121 classes.

## Compiling the Firmware

First you must build the libraries. For convenience there is a target in the `Makefile` for this, so you simply need to run the following command from a terminal (in the root directory of this repository):

`make -j8 libs`

_Note: The `-j8` flag runs make with 8 parallel jobs, feel free to change the number. It works best if you use the number of cores (including hyperthread cores) on your machine._

Then, build the actual firmware code:

`make -j8`

If successful the compiled binaries will end up in the `build/` directory along with many other intermediate build files:

```text
spotykach.bin
spotykach.elf
```

The `.elf` file is mainly used for debugging. The `.bin` file is the one that the DFU utilities will flash onto the Seed.

### Build options

The firmware is a fixed hardware/UI **platform** that hosts a swappable DSP **engine**, chosen at build time with the `ENGINE` variable:

- `make -j8` — the granular looper (default; `ENGINE=granular`).

- `make -j8 ENGINE=delay` — a tempo-synced stereo delay (musical divisions, feedback, pitch-shifted taps).

- `make -j8 ENGINE=edrums` — a four-drum Euclidean drum machine (two drums per deck, Rev-pad swaps the editable one; synthesized voices, polymeter, live model select).

- `make -j8 ENGINE=passthrough` — a minimal stereo-passthrough variant.

Switching `ENGINE` does not require `make clean`. Other build flags: `DEBUG=1` (enables UART logging) and `LOFI_INT16=1` (16-bit loop buffer, doubling record time). See [`docs/architecture.md`](docs/architecture.md) for the platform/engine design and [`docs/engines/`](docs/engines/) for a per-engine reference (and how to add a new engine).

### Editor tooling (clangd)

The repo's includes (the libDaisy header set, `-Isrc`, the build-time `-DSPK_ENGINE_*` define) aren't discoverable by clangd on their own, so generate a `compile_commands.json` from a real build:

`bear -- make -j8`

It is git-ignored and is a snapshot of whichever `ENGINE` you built (granular by default) — regenerate after adding files or changing flags.

## Flashing the Firmware

The bootloader version used in this project enables USB DFU firmware updating from the _external_ USB port - i.e. the USB-C port on the rear of the main PCB, NOT the one on the Seed. Application firmware can only be flashed using the USB-C port.

1. Compile the firmware using the steps above

2. Connect the USB-C connector on the main PCB to the computer (ensure the cable is not power-only)

3. Hold the `Reset` button on the back of the unit for ~3 seconds. The leds under bottom pads going to "breathe" in white.

4. Run the command `make program-dfu` from a terminal

`make program-dfu` flashes whatever is currently in `build/` (it does not rebuild). To flash a non-default engine, build it first in the same step, e.g. `make ENGINE=passthrough && make program-dfu`.

For convenience there are one-shot targets that **clean + build + flash** a variant (put the device in DFU mode first, as in step 3): `make engine-granular` (the looper), `make engine-delay`, `make engine-edrums`, and `make engine-passthrough`.

Once finished, the device will automatically boot the new firmware. This can "brick" (temporarily) the device and require reinstallation of either the bootloader, the firmware binary, or both.

## Architecture & developer docs

Firmware internals are documented under [`docs/`](docs/) — start with [`docs/architecture.md`](docs/architecture.md), which covers the hardware platform, the platform/engine seam (`IEngine`), and how to slot in a new engine. [`docs/engines/`](docs/engines/) documents each engine in detail plus the shared transport and knob-routing model. Notable changes are tracked in [`CHANGELOG.md`](CHANGELOG.md).
