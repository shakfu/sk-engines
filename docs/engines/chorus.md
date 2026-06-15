# chorus engine

`ENGINE=chorus` · `src/engine/chorus/` · generated from `chorus.dsp` + `chorus.json`

A simple **stereo chorus** - and the worked example of the **generated-Faust-engine** path: it is authored as a Faust `.dsp` plus a small JSON manifest, with **no hand-written C++**. The generator (`scripts/gen_faust_engine.py`) builds the cyfaust kernel, emits the engine wrapper on the shared `FaustEngine<Traits>` template, and wires the build. See [`docs/dev/engine-gen.md`](../dev/engine-gen.md) for the generator design and [`docs/engine-types/faust.md`](../engine-types/faust.md) for the Faust path.

---

## Controls

![Chorus control surface](../media/chorus-controls.svg)

_Generated from [`docs/diagrams/controls/chorus.json`](../diagrams/controls/chorus.json) via `make diagrams`._

| Control | `ParamId` | Faust slider | Effect |
|---|---|---|---|
| **MODFREQ** | `ModSpeed` (`set_mod_speed`) | `rate`  | LFO rate (0.05–5 Hz) |
| **MOD_AMT** | `ModAmp` | `depth` | modulation depth (0–5 ms) |
| **SIZE**    | `Size`  | `delay` | base delay (5–20 ms) |
| **SOS**     | `Mix`   | `mix`   | dry/wet |

The platform's 0–1 knob is linear-mapped into each slider's native range, captured from the kernel at `init()`. The other knobs/pads/CV are unused (a stereo FX). The LED rings show an output-level meter.

---

## Authoring (the generated-Faust path)

The whole engine is two files in `src/engine/chorus/`:

- **`chorus.dsp`** - the DSP (flat `hslider`s named `rate`/`depth`/`delay`/`mix`).

- **`chorus.json`** - the manifest mapping platform knobs to those slider labels:

```json
{ "engine": "chorus", "backend": "faust",
  "knobs": { "Cycle": "rate", "Glow": "depth", "Size": "delay", "Mix (SOS)": "mix" },
  "features": { "meter": true, "color": "0x33ccff" } }
```

Then:

```text
make faust-engine MANIFEST=src/engine/chorus/chorus.json   # kernel + wrapper + build wiring + control spec
make -j8 ENGINE=chorus                                    # build (~80.5% SRAM_EXEC)
make engine-chorus                                        # clean + build + DFU flash
make -C host test-chorus                                  # host test
```

The generator also emits `docs/diagrams/controls/chorus.json`, so `make diagrams` renders the control surface above - one manifest drives the engine, the build, and the diagram. `chorus_engine.h` and the generated kernel are checked in (a normal build needs no cyfaust); regenerate the wrapper with `--force-glue`.
