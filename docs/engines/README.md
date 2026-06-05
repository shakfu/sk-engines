# Engines

Spotykach is a fixed hardware/UI **platform** hosting a swappable DSP **engine** (one chosen at
build time via `ENGINE=`). This folder documents each engine in detail; this page covers the model
they all share. Companion docs: `docs/architecture.md` (platform/engine design), `docs/engine-layout.md`
(which files belong to which engine).

## The contract an engine implements

Every engine implements `IEngine` (`src/engine/iengine.h`). The audio lifecycle is required:

- `init(const EngineContext&)` — receive sample rate, block size, the SDRAM `arena`, the `ITimeSource`
  clock, and the platform `transport` (read-only `ITransport`). Allocate + subscribe here.
- `prepare()` — non-real-time main-loop housekeeping.
- `process(in, out, size)` — the 48 kHz / 96-frame audio block.

Everything else (params, MIDI, pads, CV/gate, storage, LED queries, `render`) has a no-op default;
an engine overrides only what it supports and advertises optional regions via `capabilities()`
(`CapOwnDisplay`, `CapDualDeck`, `CapTransport`, `CapTapeStorage`, ...).

## The transport (shared clock)

The clock/transport is a **platform service** (`src/transport/`, see `docs/engine-layout.md`), not part
of any engine. The platform owns it and drives it; an engine receives a read-only `ITransport`
(`src/engine/itransport.h`) in `init()` and can:

- **read tempo/state**: `tempo()`, `source()`, `is_external_sync()`, `key_interval()`,
  `is_key_sub_quarter()`.
- **subscribe to ticks**: `set_on_tick(cb)` — the platform calls `cb(const TransportTick&)` every
  clock tick (from the audio-block context).

`TransportTick` carries `{index, tick, key, quarter, tempo, reset}`:

| field | meaning |
|---|---|
| `index` | monotonic counter of common (divided) ticks since boot — use for step placement |
| `tick` | a common/divided tick fired this step (1/16 at the default divider resolution) |
| `key` | a key (bar) boundary, per the key-interval |
| `quarter` | a quarter-note boundary |
| `tempo` | current BPM |
| `reset` | the grid was realigned (clock reset / external resync) — realign your own sequencer |

The delay engine *reads* tempo; the edrums engine *subscribes* and steps a sequencer. A non-synced
engine ignores the transport entirely.

### Clock control (player-facing)

- **TAP** button — tap tempo (internal clock).
- **Tap-hold TAP + MODFREQ_A** — set tempo continuously.
- **Alt + TAP** — cycle clock source: internal → TS4 clock-in (jack 23) → MIDI.
- The clock-source LED (green = internal, pink = external) and the speed blink under jack 23 render
  for every engine.

## Knobs: how a physical control reaches an engine

The platform reads the 7 analog knobs per deck plus the crossfade, applies its pickup/modifier
grammar, and calls `engine.set_param(ParamId, deck, value)` (or `set_mod_speed`). The **same physical
knob maps to different `ParamId`s depending on the held modifier** (the Flux/Grit/Alt pads, the
tap-hold). For a non-granular engine (default `DeckLayout::single`) the per-deck routing is:

| Physical knob | default | Flux-pad held | Grit-pad held | Alt held | Tap-hold |
|---|---|---|---|---|---|
| POS   | `Pos`   | `FluxFb` | — | — | — |
| ENV   | `Env`   | — | — | — | — |
| SIZE  | `Size`  | — | — | — | — |
| PITCH | `Speed` | `FluxIntensity` | `GritIntensity` | `Aux` (CapAux engines) / else pitch-quantize | — |
| MODFREQ | `set_mod_speed` | — | — | (sync flag) | **Tempo** (A) / PanSpeed (B) → platform |
| MOD_AMT | `ModAmp` | — | — | — | ClickMix (A) / PanRange (B) → platform |
| SOS   | `Mix`   | `FluxMix` | `GritMix` | `Feedback` | **KeyInterval** (A) → platform |

The `ParamId` names are granular-flavored (the platform's vocabulary), but a non-granular engine just
interprets whichever ones it cares about — e.g. the delay reads `Size/Pos/Speed/Mix` as
time/feedback/pitch/mix; edrums reads them as Euclidean + voice controls. The **default column** gives
each engine 7 per-deck controls with no modifier needed.

`Tempo` and `KeyInterval` are not engine params — the platform writes them straight to the transport.

## Building and flashing a variant

```
make -j8 ENGINE=granular      # default; also: passthrough | delay | edrums
make ENGINE=edrums program-dfu
make engine-edrums            # one-shot: clean + build + flash (device in DFU mode)
make check-boundary           # platform (hw/ui/memory/transport) must not include engine/granular/
```

## The engines

| Engine | `ENGINE=` | What it is | Doc |
|---|---|---|---|
| Granular looper | `granular` (default) | the full instrument: dual granular looper/sampler | [granular.md](granular.md) |
| Stereo delay | `delay` | tempo-synced dual delay line | [delay.md](delay.md) |
| Edrums | `edrums` | dual Euclidean drum machine (synthesized) | [edrums.md](edrums.md) |
| Passthrough | `passthrough` | minimal stereo passthrough (reference engine) | [passthrough.md](passthrough.md) |
