# Delay — tempo-synced stereo delay, with characters + topologies

`ENGINE=delay` · `src/engine/delay/delay_engine.{h,cpp}` · class `DelayEngine`

Two delay lines (deck A → left, deck B → right) whose delay time **locks to the transport tempo**, stepping through musical divisions. The second engine to consume the platform transport (it *reads* tempo; it does not subscribe to ticks). The Reel/Slice/Drift switch picks the delay **character** and the routing switch picks the stereo **topology**.

## Signal path (per deck, the `Tap` struct)

A single delay line over borrowed SDRAM (sub-allocated from the engine arena, sized to hold the longest division at the slowest tempo, ~6 s). Per sample, split into `read_color()` then `write_out()` (so ping-pong can cross the two taps' feedback):

1. Fractional read `s_delay` samples behind the write head (Tape adds a wow/flutter LFO to the read time).

2. **Colorize the feedback signal:** a one-pole low-pass (the ENV tone control), then — by character — Tape soft-saturates it, or Shimmer pitch-shifts it +12.

3. Write `input + feedback·colorized` (in ping-pong, the colorized feedback comes from the *other* deck). Tape/Shimmer soft-clip the loop so the colored feedback can't run away; Clean is bit-for-bit.

4. Output `dry·(1−mix) + pitch(wet)·mix` — a crossfading two-head **pitch shifter** transposes the heard wet (the PITCH knob), bypassed at unity. The same shifter type does the Shimmer feedback shift. All controls are one-pole smoothed per sample (no zipper).

## Tempo sync

`process()` reads `transport->tempo()` once per block and sets each tap's target delay from the selected musical division: `delay_samples = sr · (60/bpm) · beats`. The per-sample smoother glides the change, so turning tempo (or patching a different clock) ramps the delay time rather than clicking.

The division table (`kDivBeats`, ascending so SIZE up = longer): `1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/4., 1/2` (straight + dotted + triplet).

## Control map

| Control | `ParamId` / config | Function |
|---|---|---|
| **SIZE** | `Size` | musical division (stepped; the ring shows the selected division) |
| **POS** | `Pos` | feedback (capped ·0.95) |
| **SOS** | `Mix` | wet/dry mix |
| **PITCH** | `Speed` | transpose the heard wet (±1 octave, centre = unity) |
| **ENV** | `Env` | feedback tone — a one-pole low-pass in the loop: up = open/clean, down = darker repeats (dub) |
| **MODFREQ** | `set_mod_speed` | mod-LFO rate (~0.05 .. 12 Hz) — chorus/flange/vibrato when paired with MOD_AMT |
| **MOD_AMT** | `ModAmp` | mod-LFO depth — modulates the delay time 0 .. ~12 ms (all characters; Tape keeps a small floor) |
| **Play pad** | `on_play_pad` | **Freeze** (per deck): loop the buffer at unity feedback; the input still passes dry so you can play over it |
| **Reel/Slice/Drift switch** | `ConfigId::Mode` (per deck) | **character**: Clean / Tape / Shimmer |
| **Route switch** | `ConfigId::Route` | **topology**: Stereo / DoubleMono / Ping-pong |

Knob meanings are fixed across characters — the mode only changes the feedback-path treatment.

### Characters (`ConfigId::Mode`, per deck; ring tint blue / amber / violet)

- **Clean** — clean digital repeats. Identical to the original delay when ENV is up.
- **Tape** — a slow wow/flutter LFO on the read time + the tone low-pass + soft saturation in the loop: warbly, degrading dub/analog repeats.
- **Shimmer** — the feedback is pitch-shifted **+12** each pass (a second crossfading shifter), so repeats climb into an octave wash.

### Topologies (`ConfigId::Route`; shown on the mode L/C/R LEDs)

- **DoubleMono** — two independent mono delays (deck A → L and deck B → R, each with its own controls).
- **Stereo** — linked: both delays share deck A's controls (a coherent stereo delay; deck B's strip inert).
- **Ping-pong** — linked + **cross-feedback**: each deck's colored feedback feeds the *other*, so echoes bounce L↔R.

`capabilities()` = `CapOwnDisplay | CapDualDeck`; `route()` reports the topology for the route LED. The engine renders its own display (a division arc tinted by character + an input-level play indicator); the platform composites the clock indicators over it.

## Tests

`make -C host test-delay` (`host/test_delay.cpp`): a fed-back delay rings out and more feedback lengthens the tail; the three characters are distinct + finite; ping-pong cross-feeds (driving deck A produces deck-B output) while DoubleMono does not; the ENV tone changes the output; and every character stays finite + bounded at maximum feedback.

## Notes / possible improvements

- Sync is always on; there is no free-running mode (retired). A tempo-free fine (ms) mode on an Alt layer is still open.
- Feedback is capped (`·0.95`); Tape/Shimmer additionally soft-clip the loop, and Freeze loops it at unity (the soft-clip keeps Tape/Shimmer frozen loops from running away while they evolve).
- The mod LFO is one per tap (MODFREQ rate / MOD_AMT depth); in Tape it has a rate+depth floor, which is that character's wow/flutter, so Tape warbles even with the mod knobs down.
- Still open: dotted/triplet display glyphs; ducking (sidechain the wet to the dry envelope); more characters (diffuse / reverse); a tempo-free fine (ms) mode.

## Files

`src/engine/delay/delay_engine.{h,cpp}`; build via `engine_select.h` (`SPK_ENGINE_DELAY`) + `Makefile` (`ENGINE=delay`, `make engine-delay`). Host test: `host/test_delay.cpp`.
