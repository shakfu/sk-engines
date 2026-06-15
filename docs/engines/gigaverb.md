# gigaverb — stereo reverb from a gen~ patch

`ENGINE=gigaverb` · `src/engine/gigaverb/` · built via the [gen~ / gen-dsp method](../engine-types/gen.md)

The stock Max/MSP `gen~` reverb example (Tom Erbe's gigaverb), exported to C++ and run as an engine through gen-dsp - the first engine built this way, and the worked example for the method. The mechanism (genlib isolation, the arena-bound allocator, the generic `GenEngine<W>` wrapper, the codegen) is documented in [engine-types/gen.md](../engine-types/gen.md); this page is the gigaverb specifics.

> Implementation, the file map, and dev notes live in [`docs/dev/gigaverb-impl.md`](../dev/gigaverb-impl.md).

## Control map

![gigaverb control surface](../media/gigaverb-controls.svg)

_Generated from [`docs/diagrams/controls/gigaverb.json`](../diagrams/controls/gigaverb.json) via `make diagrams`._

`set_param` arrives normalized 0..1 and is linear-mapped into each gen~ param's declared `[min,max]` (from `manifest.json`). The map uses only `ParamId`s the platform delivers to a single-deck engine via `set_param()` - the six plain panel knobs, plus two modifier layers for the two extra params:

| Knob | `ParamId` | gen~ param | range |
|---|---|---|---|
| SIZE | `Size` | roomsize | 0.1..300 |
| POS | `Pos` | revtime | 0.1..1 |
| PITCH | `Speed` | bandwidth | 0..1 |
| ENV | `Env` | damping | 0..1 |
| SOS | `Mix` | dry | 0..1 |
| MOD_AMT | `ModAmp` | tail | 0..1 |
| SOS + Alt | `Feedback` | spread | 0..100 |
| ENV + chord | `EnvSize` | early | 0..1 |

This table lives in `src/engine/gigaverb/gigaverb_engine.h` (the `index_of()` switch) and is the one spot to retune; everything else is the generic wrapper. `ParamId::ModSpeed` is intentionally absent - the MODFREQ knob routes to `set_mod_speed()`, which a gen engine does not receive (see [the method doc](../engine-types/gen.md#control-map) for the full reachability rules and the knob grammar in [README.md](README.md)).

## Build / flash / regenerate

```text
make clean && make -j8 ENGINE=gigaverb     # build; the link prints SRAM_EXEC usage
make ENGINE=gigaverb program-dfu           # flash (device in DFU mode first)
make gen-engines                               # regenerate from the vendored export (keeps the knob map)
```

## Also available inside the `reverb` engine

Beyond the standalone `ENGINE=gigaverb` build, this export can be folded into the [`reverb` engine](reverb.md) as a **third selectable voice** alongside the Faust plate and hall: `make ENGINE=reverb REVERB_GIGAVERB=1`. There it is wrapped as a `GigaverbVoice` (driving the same gen~ export by parameter index), gets its own per-deck gen~ arena slice, and - unlike this standalone build - synthesizes a true wet/dry crossfade in software (pins `dry` to 0 and blends against the input) so its Mix knob matches the Faust voices. See [reverb.md](reverb.md#optional-third-voice-gen-gigaverb).
