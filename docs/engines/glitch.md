# glitch engine

`ENGINE=glitch` · `src/engine/glitch/glitch_engine.{h,cpp}` · class `GlitchEngine`

A **dual-deck lo-fi / circuit-bent noise voice**. Each deck (A/B) runs one of **12 curated algorithms** ported from Rob Scape's [Noisferatu](https://github.com/rob-scape/noisferatu); the two voices are blended by the platform crossfader and placed by the routing switch, so it plays like a pair of glitch oscillators you mix.

Its character is the **digital-glitch aesthetic** that the platform's cleaner synthesis engines (the Plaits-based `mosc`, the Rings-based `reso`) don't cover: address bit-mangling, logic-noise, generative scale blips, and rhythmic noise. Everything is intentionally **lo-fi** - 10-bit-flavoured integer oscillators with no band-limiting, so the tones alias and crunch by design.

> Implementation, the 16 kHz -> 48 kHz retuning, the curation rationale, the file map, and the bug writeups live in [`docs/dev/glitch-impl.md`](../dev/glitch-impl.md).

---

## Controls (per deck)

![Glitch control surface](../media/glitch-controls.svg)

_Generated from [`docs/diagrams/controls/glitch.json`](../diagrams/controls/glitch.json) via `make diagrams`._

| Control | `ParamId` / config | Effect |
|---|---|---|
| **Alt+PITCH** (`Aux`) | | **ALGORITHM** select (held selector, ring dots) - the one knob that picks which of the 12 voices this deck runs. |
| **SIZE** (`Size`) | | **param 1** - algorithm-specific (see the table below). |
| **POS** (`Pos`) | | **param 2** - algorithm-specific. |
| **PITCH** (`Speed`) | | **master pitch** (+/- 2 octaves around centre) for the pitched algorithms; folds into playback speed for the buffer players. |
| **ENV** (`Env`) | | **tone** - a one-pole low-pass tilt (fully open at max, progressively darker toward zero). Lo-fi material gets harsh without it. |
| **MIX** (`Mix`) | | deck **volume**. |
| **Mix fader** (`Crossfade`) | | A/B blend of the two voices. |
| **Routing switch** (`Route`) | | stereo topology (below). |
| **Play pad** | | **regenerate** the glitch buffer (a fresh sparse pattern for the buffer-player algorithms). The Rev pad is inert. |

### The 12 algorithms

Selected with **Alt+PITCH**; the ring shows 12 dots with the current one bright. SIZE/POS map to the two params listed.

| # | Algorithm | Character | SIZE (param 1) | POS (param 2) |
|---|---|---|---|---|
| 0 | **Sparse Glitch** | sparse chunked noise/silence buffer, played back with random silence injection | playback speed (0.25-4x) | silence probability |
| 1 | **Wander Window** | a small playback window random-walking through the buffer (grain-freeze) | playback speed | walk rate (1-60 Hz) |
| 2 | **Bit Mangle** | buffer read with its **address bits corrupted** before each read (SoundScaper-style) | playback speed | bit-clock rate (0.5-30 Hz) |
| 3 | **Tri XOR Tri** | two triangles XORed - Benjolin/logic-noise metallic tone | osc 1 freq (0.7-220 Hz) | osc 2 freq (0.6-440 Hz) |
| 4 | **Square NAND** | two ramps NANDed - harsh digital logic tone | osc 1 freq (0.1-50 Hz) | osc 2 freq (0.08-45 Hz) |
| 5 | **FM Noise** | two inharmonic triangles XORed; a coincidence clock re-rolls the inharmonic ratio | base freq (20-2000 Hz) | coincidence clock (0.5-50 Hz) |
| 6 | **Ring Mod** | two triangles multiplied (ring modulation) | osc 1 freq (20-2000 Hz) | osc 2 freq (20-2000 Hz) |
| 7 | **Phrygian Tri** | enveloped triangle blips random-walking a Phrygian scale | trigger rate (1-16 Hz) | burst modulation (0.1-20 Hz) |
| 8 | **Penta Blips** | enveloped triangle blips picking random major-pentatonic notes | clock rate (3-11.5 Hz) | envelope decay |
| 9 | **Bernoulli Tris** | two Bernoulli-gated enveloped triangles (probabilistic intervals) | gate 1 probability | gate 2 probability |
| 10 | **Dust** | sparse filtered random clicks (SuperCollider Dust) | density | tone (click filter) |
| 11 | **Noise Rhythm** | clock-divided rhythmic noise bursts (two enveloped noise voices) | clock rate (1-10 Hz) | clock division (/1../7) |

The pitched algorithms (3-11 where a frequency is involved) follow **PITCH** as a master transpose; the buffer players (0-2) fold it into playback speed.

### Routing / stereo image

- **LEFT (DoubleMono):** voice A hard-left, voice B hard-right (two glitch voices across the field).
- **CENTRE (Stereo):** both centred.
- **RIGHT (GenerativeStereo):** each voice at a random pan (re-rolled on entering the mode).

### Display

Per deck the ring shows a marker at the current algorithm position over a faint base; the play indicator is lit (a generator is always running). Holding **Alt** shows the **ALGORITHM** selector (12 dots, the current bright). The routing switch lights the mode L/C/R indicator.

---

## Notes

- **No SD card, no sample prep, no CV inputs needed** - the engine is a pure generator. The two voices each own an ~8 KB glitch buffer in RAM (regenerated on algorithm select and on the Play pad); there is no arena or streaming.
- **Decks decorrelate:** the two voices carry distinct PRNG seeds, so the same algorithm on both decks still sounds different (useful with the random-pan routing).
- **Aliasing is the aesthetic:** the oscillators are naive (no band-limiting). Use **ENV** (tone) to tame the harshness on the pitched algorithms.

---

## Build / flash

```text
make -j8 ENGINE=glitch        # build (~79% SRAM_EXEC)
make ENGINE=glitch program-dfu
make engine-glitch            # one-shot: clean + build + flash (device in DFU mode)
make -C host test             # host suites incl. test-glitch
```
