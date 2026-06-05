# Delay — tempo-synced stereo delay

`ENGINE=delay` · `src/engine/delay/delay_engine.{h,cpp}` · class `DelayEngine`

Two independent delay lines (deck A → left, deck B → right) whose delay time **locks to the transport tempo**, stepping through musical divisions. The second engine to consume the platform transport (it *reads* tempo; it does not subscribe to ticks).

## Signal path (per deck, the `Tap` struct)

A single delay line over borrowed SDRAM (sub-allocated from the engine arena, sized to hold the longest division at the slowest tempo, ~6 s):

1. Fractional read `s_delay` samples behind the write head.

2. Write `input + feedback·wet` (feedback is taken pre-pitch so the delay structure stays stable).

3. Output `dry·(1−mix) + pitch(wet)·mix`.

A crossfading two-head **pitch shifter** transposes the wet taps (the PITCH knob), bypassed at unity. All controls are one-pole smoothed per sample (no zipper).

## Tempo sync

`process()` reads `transport->tempo()` once per block and sets each tap's target delay from the selected musical division: `delay_samples = sr · (60/bpm) · beats`. The per-sample smoother glides the change, so turning tempo (or patching a different clock) ramps the delay time rather than clicking.

The division table (`kDivBeats`, ascending so SIZE up = longer): `1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/4., 1/2` (straight + dotted + triplet).

## Control map

| Knob | `ParamId` | Function |
|---|---|---|
| **SIZE** | `Size` | musical division (stepped; the SIZE ring shows the selected division) |
| **POS** | `Pos` | feedback |
| **SOS** | `Mix` | wet/dry mix |
| **PITCH** | `Speed` | transpose the wet taps (±1 octave, centre = unity) |

`capabilities()` = `CapOwnDisplay | CapDualDeck`. The engine renders its own display (the division arc

- an input-level play indicator) and the platform composites the clock indicators over it.

## Notes / possible improvements

- Sync is always on; there is no free-running mode (that is the engine's previous behavior, retired).

- Feedback is capped (`·0.95`) to avoid runaway.

- Possible: dotted/triplet display glyphs; a ping-pong / cross-feedback mode; tempo-free fine mode on an Alt layer.

## Files

`src/engine/delay/delay_engine.{h,cpp}`; build via `engine_select.h` (`SPK_ENGINE_DELAY`) + `Makefile` (`ENGINE=delay`, `make engine-delay`).
