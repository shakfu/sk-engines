# Licensing notice — qdelay engine

This repository is **MIT** (see the top-level `LICENSE`), **except** for the files
listed below, which are **GPLv3** because they derive from [qdelay](https://github.com/tiagolr/qdelay)
by Tiago Lobato Gimenes (tilr), a GPLv3 plug-in.

## GPLv3 files

- `src/dsp/diffuser.h` — a port of qdelay's `Diffusor` (`src/dsp/Diffusor.h`): the 8-stage
  allpass topology, the L/R coefficient tables, and the size→offset mapping are taken from it.
  qdelay credits the design to TARON's MiniVerb. The port is JUCE-free and allocation-free, but it
  is a derivative work and inherits qdelay's license.
- `src/engine/qdelay/qdelay_engine.{h,cpp}` — the engine that incorporates the diffuser. It is
  original code, but as a combined work with the GPLv3 diffuser it is distributed under GPLv3.

## Consequence

Any firmware built with `ENGINE=qdelay` is a combined work that **must be distributed under GPLv3**
(source available, etc.). The rest of the project — every other engine and the platform — remains MIT;
none of them link `src/dsp/diffuser.h`.

The reverse-delay gesture and the duck follower added elsewhere were written independently from
standard DSP technique and are **not** covered by this notice.

The full GPLv3 text is preserved in [`LICENSE`](LICENSE) alongside this file.
