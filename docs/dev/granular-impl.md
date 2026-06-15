# Dev notes — dual granular looper / sampler (`granular` engine)

Implementation, the DSP graph, and the file map for `ENGINE=granular`. The user-facing reference (modes,
transport, controls, persistence) is [`docs/engines/granular.md`](../engines/granular.md). Granular is the
reference engine; the architecture is also covered in [`docs/architecture.md`](../architecture.md).

## The graph

`GranularEngine` owns a `Core` (`src/engine/granular/core.{h,cpp}`) which holds two `Deck`s (A/B) plus the shared mix/pan/click and the per-deck modulators. `Route` (DoubleMono / Stereo / GenerativeStereo) selects the channel topology and infers the panner mode.

Each `Deck` is a full looper voice:

- **Buffer** — the audio loop (record / overdub / feedback), in external SDRAM (sub-allocated from the engine arena; up to ~42 s).

- **Generator** — the granular engine: `kVoxCount` grains (`Vox`), owning start / size / spread / pitch / speed, slicing, and reverse.

- **Track** — a per-deck step sequencer recording `Event`s into a ring of slices (its own pattern divider).

- **Detector / Divider / Dispatcher / Fx** (drive + reduce) / **XFade** (in/out mix).

## Files

`src/engine/granular/`: the `IEngine` wrapper `granular_engine.{h,cpp}` plus the private DSP (`core`, `deck`, `generator`, `vox`, `buffer`, `track`, `fx*`, `modulator`, `panner`, `detector`, `click`, ...). Build is the default (`ENGINE=granular`, `make engine-granular`). Note: the platform (`hw/ui/memory/transport`) must never include `engine/granular/` — enforced by `make check-boundary`.
