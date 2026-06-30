# pstretch engine

`ENGINE=pstretch` · `src/engine/pstretch/pstretch_engine.{h,cpp}` · class `PstretchEngine`

A **real-time PaulStretch** ambient time-smear. PaulStretch (Nasca Octavian Paul) turns audio into a
diffuse, evolving spectral wash by FFT-ing large overlapping windows, **randomizing the phases** (keeping
the magnitudes), and overlapping back to time. Advancing the analysis read head slowly - or freezing it -
stretches and smears time without limit.

Each deck (A/B) runs an independent stretcher over its input channel (A = left, B = right, like the delay
engine); the two are blended by the crossfader and placed by the routing switch. This is a **clean-room**
reimplementation written from the published algorithm description (not derived from the GPL PaulStretch /
PaulXStretch sources), so it stays **MIT**, and it is **self-contained** - a small vendored FFT, no
external DSP library.

> Implementation, the source-mode roadmap (live / capture / SD-file), and the design notes live in
> [`docs/dev/pstretch-impl.md`](../dev/pstretch-impl.md).

---

## Controls (per deck)

![Pstretch control surface](../media/pstretch-controls.svg)

_Generated from [`docs/diagrams/controls/pstretch.json`](../diagrams/controls/pstretch.json) via `make diagrams`._

| Control | `ParamId` / config | Effect |
|---|---|---|
| **SIZE** (`Size`) | | **STRETCH** amount, 1x..64x (exponential) - how slowly the read head crawls. More = more smear. |
| **POS** (`Pos`) | | **DIFFUSION** - 0 = clean window resynthesis, 1 = full phase-randomized PaulStretch wash. |
| **PITCH** (`Speed`) | | **pitch shift** of the grain, +/- 1 octave. |
| **ENV** (`Env`) | | **tone** - a one-pole low-pass to soften the wash (open at max). |
| **MIX** (`Mix`) | | **dry/wet**. |
| **Play pad** | | **FREEZE** the read head - an infinite, evolving drone on the current spot. |
| **Rev pad** | | **CAPTURE/hold** (latched) - grab the recent input and loop the stretch through it (below). |
| **Mix fader** (`Crossfade`) | | A/B blend of the two decks. |
| **Routing switch** (`Route`) | | stereo topology (below). |

### Live vs capture - what the stretch acts on

pstretch is a **live effect** (no sample files; it processes the incoming audio). It keeps a few seconds
of input in a ring buffer, and what the stretch reads from that ring depends on the mode:

- **Live (default).** The read head trails the incoming sound. Because real-time stretching cannot stretch
  the literal present (the output would fall infinitely behind), at high **SIZE** the head holds near the
  trailing edge and you hear an evolving, smeared wash of the **recent past** - a few seconds back - rather
  than the instantaneous input. This is the ambient "blur the room" mode.

- **Freeze (Play pad).** Pins the read head: the same spectral region keeps re-randomizing into an endless
  evolving pad. This is how you drone on *this* moment.

- **Capture/hold (Rev pad).** Grabs the last ~5 seconds of input, stops listening, and loops the read head
  *through* that captured span - so a big **SIZE** slowly traverses the whole captured phrase. This is the
  classic PaulStretch "stretch a short sound into a long drone." Tap the Rev pad again to return to live.

All three respond to PITCH / DIFFUSION / ENV / MIX the same way.

### Routing / stereo image

- **LEFT (DoubleMono):** deck A hard-left, deck B hard-right (independent L/R smear).
- **CENTRE (Stereo):** both centred.
- **RIGHT (GenerativeStereo):** each deck at a random pan (re-rolled on entering the mode).

### Display

Per deck the play LED and a ring marker show the stretch amount and the state colour: **green** = live,
**amber** = captured (looping a grab), **cyan** = frozen. The routing switch lights the mode L/C/R
indicator.

---

## Notes

- **No SD card, no sample prep** - it is a pure live effect. The per-deck input rings and the FFT scratch
  live in the SDRAM arena.
- **Delayed smear** (live mode) is intentional and inherent to real-time stretching - use **Freeze** or
  **Capture** to play *through* a fixed moment/phrase instead of trailing the input.
- **Window is 4096 samples (~85 ms).** A valid smear; larger windows (smoother wash) are a possible
  follow-on.
- A full **SD-file** source (load an arbitrary clip and stretch a whole song into an hour-long drone) is
  planned as a second source mode on the platform's SD-streaming stack - see the dev notes.

---

## Build / flash

```text
make -j8 ENGINE=pstretch      # build (~78% SRAM_EXEC)
make ENGINE=pstretch program-dfu
make engine-pstretch          # one-shot: clean + build + flash (device in DFU mode)
make -C host test             # host suites incl. test-pstretch
```
