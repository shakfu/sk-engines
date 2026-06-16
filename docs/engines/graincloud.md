# graincloud engine

`ENGINE=graincloud` · `src/engine/graincloud/graincloud_engine.{h,cpp}` · class `GraincloudEngine`

A **polyphonic grain cloud** (engine #11 in [`docs/engine-ideas.md`](../engine-ideas.md), "Grain cloud") - the granular the looper *isn't*. Where the stock `granular` engine is a dual tape-loop scanner whose grains share one pitch/position/speed, `graincloud` scatters dozens of **independent** grains - each with its own pitch, buffer position, pan, duration and direction - over a recorded buffer. It is built on a de-STL'd, bare-metal port of [GrainflowLib](https://github.com/shakfu/nanodsp/tree/main/thirdparty/GrainflowLib).

Two decks = two clouds (A/B), recorded and played independently and **morphed by the crossfader**.

> Implementation (the GrainflowLib port, the buffer-reader seam, the phasor glue, the file map, the SDRAM benchmark, risks/watch-items) lives in [`docs/dev/graincloud-impl.md`](../dev/graincloud-impl.md).

---

## Concept

Each deck records the live input into its own ~20 s SDRAM buffer (the **Rev pad**), then fills it with a fixed pool of grains. Each grain is reborn on its stream's grain-clock cycle at a scattered position, reads the buffer with cubic interpolation at its own transpose, and is panned into the stereo field - so the cloud is a shifting, spatialised texture rather than a few playheads. With no recording, the deck is silent (the grains have nothing to read).

The two knobs that define a cloud are **density** (how many grains per second) and **spray** (how far their start points scatter from the cloud centre); pitch, pan and duration each have a centre value plus a randomization depth.

## Controls

![Grain cloud control surface](../media/graincloud-controls.svg)

_Generated from [`docs/diagrams/controls/graincloud.json`](../diagrams/controls/graincloud.json) via `make diagrams`._

Per deck:

| Control | `ParamId` | Function |
|---|---|---|
| **POS** | `Pos` | cloud centre position in the recorded buffer |
| **SIZE** | `Size` | position spray (spread of grain start points) |
| **PITCH** | `Speed` | grain transpose (centre = unity; +V/Oct, MIDI) |
| **ENV** | `Env` | grain duration / window |
| **MODFREQ** | `set_mod_speed` | grain density (grains/sec) |
| **MOD_AMT** | `ModAmp` | pitch spray + pan spray (per-grain randomization depth) |
| **SOS** | `Mix` | dry/wet |
| **Mode switch (L/C/R)** | `ConfigId::Mode` | grain direction: forward / reverse / random |
| **Alt+PITCH** | — | select the SD loop slot (platform storage) |
| **Crossfader** | `Crossfade` | morph between cloud A and cloud B |

**Recording:** hold **Alt + Play** (or **Alt + Rev**) to toggle recording the live input into the focused deck's buffer; **Clear** empties it. To monitor the input while recording, turn **SOS** (dry/wet) down from fully-wet - at full wet you hear only the cloud, which is silent until something is recorded. **Remember the cloud lives in one deck**, so set the crossfader toward the deck you recorded.

**SD save/load** (`CapTapeStorage`): the recorded buffer is exposed to the platform storage port, so a cloud's source can be saved to and recalled from the card. **Alt+PITCH selects the loop slot** (which is why the Aux/direction selector moved to the Mode switch - the two can't share Alt+PITCH).

CV/MIDI: **V/Oct** transposes the grains (additive on top of the PITCH knob); a **MIDI note** addresses deck A (even channel) or B (odd) and sets its transpose.

## Routing / display

Each deck paints its own ring (`CapOwnDisplay`): a cloud level meter and a position dot for POS. The Play indicator turns red while recording. Capabilities: `CapRecording | CapTapeStorage | CapDualDeck | CapOwnDisplay | CapTransport`. Note `CapAux` is intentionally absent: on a storage engine Alt+PITCH is the loop-slot picker, so grain direction moved to the Mode switch.

## Build / flash

```text
make -j8 ENGINE=graincloud
make ENGINE=graincloud program-dfu
make engine-graincloud            # one-shot: clean + build + flash (device in DFU mode)
```

> **Hardware status:** builds and fits (SRAM_EXEC ~87% at -O2) and passes the host test suite, but the on-device cost of the cloud's *scattered* SDRAM reads has not yet been confirmed on hardware. Run `make ENGINE=graincloud METER=1`, set a high density, and read the serial load before relying on the maximum grain count. See the impl doc's benchmark section.
