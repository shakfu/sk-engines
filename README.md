# sk-engines: A Spotykach (platform/engine fork)

A fork of the official [Synthux Academy Spotykach](https://synthux.academy/store/spotykach) firmware, restructured as a fixed hardware/UI **platform** with a swappable DSP **engine** architecture.

The hardware and interaction model remain constant across firmware variants: multi-function encoders with pickup behavior and LED ring feedback, pad gestures, transport controls, SD-card sample storage, CV/gate I/O, and MIDI support. Individual firmware builds replace only the DSP engine and its parameter set. Clocking and transport are also provided as shared platform services, allowing any engine to synchronize to the same internal, TS4, or MIDI clock sources.

The platform is intentionally decoupled from any specific engine. Core subsystems in `src/hw/`, `src/ui/`, `src/memory/`, and `src/transport/` contain no engine-specific dependencies, and build-time checks enforce this separation (`make check-boundary`).

Current engines include:

1. [granular](docs/engines/granular.md): dual-deck granular looper (the original reference and default engine)

2. [delay](docs/engines/delay.md): tempo-synchronized stereo delay

3. [edrums](docs/engines/edrums.md): four-voice Euclidean drum machine

4. [reso](docs/engines/reso.md): resonator/plucked-string instrument based on the [Mutable Instruments Rings DSP code](https://github.com/pichenettes/eurorack/tree/master/rings)

5. [tape](docs/engines/tape.md): dual streaming tape deck (two independent record/playback decks, SD-streamed, no in-memory length cap)

6. [reverb](docs/engines/reverb.md): stereo reverb with two switchable algorithms (Dattorro plate / Zita-rev1 hall), generated from [Faust](https://faust.grame.fr) sources

7. [gigaverb](docs/engines/gigaverb.md): stereo reverb authored in Max/MSP **gen~** and translated to C++ via [gen-dsp](https://github.com/shakfu/gen-dsp) (Tom Erbe's gigaverb)

8. [passthrough](docs/engines/passthrough.md): minimal stereo passthrough engine demonstrating the platform API

Engines can be authored in three ways:

1. Using [C++](docs/engine-types/cpp.md) against `IEngine`

2. Using [**Faust**](docs/engine-types/faust.md) (via [cyfaust](https://github.com/shakfu/cyfaust))

3. Using Max/MSP's [**gen~**](docs/engine-types/gen.md) language (via [gen-dsp](https://github.com/shakfu/gen-dsp))

The latter two generate C++ that the platform wraps behind the same contract. The three methods are documented in [`docs/engine-types/`](docs/engine-types/).

Originally started as a feature-extension fork of the upstream firmware, the project evolved into a platform/engine architecture that enables new instruments to reuse the existing hardware and interaction language rather than reimplement them. See [`docs/architecture.md`](docs/architecture.md) for an overview of the design and instructions for creating new engines.

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

- `make -j8 ENGINE=reso` — a resonator/pluck voice on the Mutable Instruments Rings DSP (modal / sympathetic-string / string / FM / string+reverb models on Alt+PITCH; three excite modes — discrete plucks, live-input resonator, scatter cloud). Vendored Rings/stmlib live under `src/engine/reso/thirdparty/`.

- `make -j8 ENGINE=tape` — two independent mono **tape decks** (A/B) that play and record arbitrarily long takes to the SD card, removing the in-SDRAM loop-length cap. Per deck: Play pad = play, Alt+Play = record, PITCH = varispeed, Alt+POS = pan, MIX = volume, ENV = loop mode (none / loop / faded / Frippertronics), Alt+PITCH = tape-slot select (8 slots under `/tapes/`); the routing switch and mix fader place/blend the two decks. Streams mono float WAV through lock-free per-deck SDRAM rings drained by a main-loop FatFs pump.

- `make -j8 ENGINE=reverb` — a stereo reverb with **two switchable algorithms** (a Dattorro plate and a Zita-rev1 hall, Alt+PITCH selects live), generated from [Faust](https://faust.grame.fr) sources by cyfaust. Regenerate the kernels with `make faust-gen`.

- `make -j8 ENGINE=gen_gigaverb` — a stereo reverb (Tom Erbe's **gigaverb**) authored in Max/MSP **gen~** and translated to C++ by [gen-dsp](https://github.com/shakfu/gen-dsp). The engine directory is generated from a gen~ export with `make gen-engines` (or `scripts/gen_engine.py`); see [`docs/engine-types/gen.md`](docs/engine-types/gen.md).

- `make -j8 ENGINE=passthrough` — a minimal stereo-passthrough variant.

Switching `ENGINE` does not require `make clean`. Other build flags: `DEBUG=1` (enables UART logging) and `LOFI_INT16=1` (16-bit loop buffer, doubling record time). See [`docs/architecture.md`](docs/architecture.md) for the platform/engine design, [`docs/engines/`](docs/engines/) for a per-engine reference, and [`docs/engine-types/`](docs/engine-types/) for the three ways to author an engine (native C++, Faust, gen~).

There is also an **opt-in CMake build** (an in-progress alternative; the `make` build above stays canonical): `make -f Makefile.cmake ENGINE=<engine>` configures and builds via CMake, with output in `build-cmake/<engine>/` instead of `build/`. It mirrors the same commands (`program-dfu`, `engine-<name>`, `DEBUG=1`, `LOFI_INT16=1`) and caches each engine in its own dir, so switching engines never forces a rebuild.

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

For convenience there are one-shot targets that **clean + build + flash** a variant (put the device in DFU mode first, as in step 3): `make engine-granular` (the looper), `make engine-delay`, `make engine-edrums`, `make engine-reso`, `make engine-tape`, `make engine-reverb`, and `make engine-passthrough`. The gen~ engine has no one-shot target; flash it with `make clean && make ENGINE=gen_gigaverb && make program-dfu`.

Once finished, the device will automatically boot the new firmware. This can "brick" (temporarily) the device and require reinstallation of either the bootloader, the firmware binary, or both.

## Architecture & developer docs

Firmware internals are documented under [`docs/`](docs/) — start with [`docs/architecture.md`](docs/architecture.md), which covers the hardware platform, the platform/engine seam (`IEngine`), and how to slot in a new engine. [`docs/engines/`](docs/engines/) documents each engine in detail plus the shared transport and knob-routing model, and [`docs/engine-types/`](docs/engine-types/) covers the three engine-authoring methods (native C++, Faust/cyfaust, gen~/gen-dsp). Notable changes are tracked in [`CHANGELOG.md`](CHANGELOG.md).
