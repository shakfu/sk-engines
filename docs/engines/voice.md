# voice engine

`ENGINE=voice` · `src/engine/voice/` · generated from `osc.dsp` + `filter.dsp` + `voice.json`

A **drone voice** - and the worked example of the **series (chain) dual-deck** generated path: **two
different** Faust kernels wired in sequence, with deck A driving the first stage and deck B the second.
Here stage 1 is a **drone oscillator** (an instrument - it generates sound from its knobs with no audio
input) and stage 2 is a **resonant filter**, so the signal flows **osc → filter → out**. Authored as two
`.dsp` files plus a JSON manifest with **no hand-written C++** (the generator emits the wrapper on the
shared `FaustChainEngine<Traits>` template). See [`docs/dev/engine-gen.md`](../dev/engine-gen.md) §9 for
the dual-deck design and [`docs/engine-types/faust.md`](../engine-types/faust.md) for the Faust path.

---

## Controls

![Voice control surface](../media/voice-controls.svg)

_Generated from [`docs/diagrams/controls/voice.json`](../diagrams/controls/voice.json) via `make diagrams`._

**Deck A drives the oscillator (stage 1):**

| Control | `ParamId` | Faust slider | Effect |
|---|---|---|---|
| **PITCH**   | `Speed` | `freq`  | oscillator pitch (~40 Hz – 2.5 kHz) |
| **SIZE**    | `Size`  | `shape` | saw ↔ square morph |
| **SOS**     | `Mix`   | `level` | oscillator level |

**Deck B drives the filter (stage 2):**

| Control | `ParamId` | Faust slider | Effect |
|---|---|---|---|
| **SIZE**    | `Size`  | `cutoff` | low-pass cutoff |
| **MOD_AMT** | `ModAmp` | `reso`  | resonance / Q |
| **PITCH**   | `Speed` | `drive` | pre-filter saturation |
| **SOS**     | `Mix`   | `mix`   | dry/wet |

The same physical knobs mean different things on each deck because each deck drives its own stage. The
chain is mono internally and duplicated to the stereo bus; the LED rings show an output-level meter. As
an instrument, it makes sound on its own - no input patch needed.

---

## Authoring (the series dual-deck path)

Three files in `src/engine/voice/`:

- **`osc.dsp`** - stage 1, a 0-input drone oscillator (`freq`/`shape`/`level`).
- **`filter.dsp`** - stage 2, a resonant low-pass (`cutoff`/`reso`/`drive`/`mix`).
- **`voice.json`** - the manifest. `"deck_mode": "series"` lists the two stages (stage 1 → deck A,
  stage 2 → deck B), each with its own knob map:

```json
{ "engine": "voice", "backend": "faust", "deck_mode": "series",
  "stages": [
    { "dsp": "osc",    "knobs": { "Pitch": "freq", "Size": "shape", "Mix (SOS)": "level" } },
    { "dsp": "filter", "knobs": { "Size": "cutoff", "Glow": "reso", "Pitch": "drive", "Mix (SOS)": "mix" } }
  ],
  "features": { "meter": true, "color": "0x88ff33" } }
```

Then:

```text
make engine-gen MANIFEST=src/engine/voice/voice.json   # both kernels + wrapper + build + diagram
make -j8 ENGINE=voice                                  # build (~84% SRAM_EXEC)
make -C host test-voice                                # host test (chain + per-stage independence)
```

Stage A may have inputs (an FX→FX chain) or none (instrument→FX, as here). The two stage kernels get
engine-scoped namespaces (`fx_voice_osc`, `fx_voice_filter`) so stage names can't collide across engines.
`voice_engine.h` and the generated kernels are checked in; regenerate the wrapper with `--force-glue`.
