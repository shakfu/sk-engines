# Licensing notice — glitch engine

This repository is **MIT** (see the top-level `LICENSE`), **except** for the files listed below, which are **GPLv3** because they derive from [Noisferatu](https://github.com/rob-scape/noisferatu) by Rob Scape, a GPLv3 project (see the license note at the end of its `README.md`).

## GPLv3 files

- `src/engine/glitch/glitch_voice.h` — a port of the 12 selected algorithms from Noisferatu's `firmware/Noisferatu/algos.h`. The oscillator/buffer/logic/scale-blip algorithms, their integer triangle/saw forms, and the xorshift PRNG are taken from it. The port is de-Arduino'd, refactored to per-instance state, and retuned from 16 kHz to 48 kHz, but it is a derivative work and inherits Noisferatu's license.

- `src/engine/glitch/glitch_engine.{h,cpp}` — the engine that incorporates the voice. It is original code (the dual-deck wrapper, control mapping, routing, and display), but as a combined work with the GPLv3 `glitch_voice.h` it is distributed under GPLv3.

## Consequence

Any firmware built with `ENGINE=glitch` is a combined work that **must be distributed under GPLv3** (source available, etc.). The rest of the project — every other engine and the platform — remains MIT; none of them include `glitch_voice.h`.

The full GPLv3 text is preserved in [`LICENSE`](LICENSE) alongside this file.
