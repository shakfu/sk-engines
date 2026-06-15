# dfilter engine

`ENGINE=dfilter` · `src/engine/dfilter/` · generated from `dfilter.dsp` + `dfilter.json`

A **dual resonant filter** - and the worked example of the **parallel (DoubleMono) dual-deck** generated path: one mono resonant low-pass kernel, run as **two independent instances**, deck A on the **left** channel and deck B on the **right**. Each deck has its own cutoff / resonance / drive / mix, and the two channels never interact. Authored as a Faust `.dsp` plus a JSON manifest with **no hand-written C++** (the generator emits the wrapper on the shared `FaustEngine<Traits>` template, `decks = 2`). See [`docs/dev/engine-gen.md`](../dev/engine-gen.md) §9 for the dual-deck design and [`docs/engine-types/faust.md`](../engine-types/faust.md) for the Faust path.

---

## Controls

![Dual filter control surface](../media/dfilter-controls.svg)

_Generated from [`docs/diagrams/controls/dfilter.json`](../diagrams/controls/dfilter.json) via `make diagrams`._

Each deck (A and B) carries the same four controls, addressing **its own channel**:

| Control | `ParamId` | Faust slider | Effect |
|---|---|---|---|
| **SIZE**    | `Size`  | `cutoff` | low-pass cutoff (~40 Hz – 20 kHz) |
| **MOD_AMT** | `ModAmp` | `reso`  | resonance / Q |
| **PITCH**   | `Speed` | `drive` | pre-filter saturation |
| **SOS**     | `Mix`   | `mix`   | dry/wet |

Deck A's knobs filter the left channel; deck B's filter the right. The crossfader is unused (the two decks are independent channels, not an A/B blend). The LED rings show a **per-deck** output-level meter.

---

## Authoring (the parallel dual-deck path)

The whole engine is two files in `src/engine/dfilter/`:

- **`dfilter.dsp`** - a mono (1-in/1-out) resonant low-pass with drive and dry/wet.

- **`dfilter.json`** - the manifest. `"deck_mode": "parallel"` turns on DoubleMono (two instances, `CapDualDeck`); the `knobs` map applies to both decks:

```json
{ "engine": "dfilter", "backend": "faust", "deck_mode": "parallel",
  "knobs": { "Size": "cutoff", "Glow": "reso", "Pitch": "drive", "Mix (SOS)": "mix" },
  "features": { "meter": true, "color": "0xff8833" } }
```

Then:

```text
make faust-engine MANIFEST=src/engine/dfilter/dfilter.json   # kernel + wrapper + build + diagram
make -j8 ENGINE=dfilter                                    # build (~83% SRAM_EXEC)
make engine-dfilter                                        # clean + build + DFU flash
make -C host test-dfilter                                  # host test (per-deck independence)
```

The kernel must be **mono** for DoubleMono (deck A=L, deck B=R). `dfilter_engine.h` and the generated kernel are checked in (a normal build needs no cyfaust); regenerate the wrapper with `--force-glue`.
