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

This map is **declared in `src/engine/gigaverb/gigaverb.json`** - the `knobs` block (control name -> gen~ param name), the gen~ analogue of a Faust engine's manifest. The generator resolves it (control -> `ParamId`, param name -> index, validated against the export) and emits the `index_of()` switch in `gigaverb_engine.h`; **edit the manifest, not the generated `.h`**, then re-run the generator. `ParamId::ModSpeed` is intentionally absent - the MODFREQ knob routes to `set_mod_speed()`, which a gen engine does not receive (see [the method doc](../engine-types/gen.md#control-map) for the full reachability rules and the knob grammar in [README.md](README.md)).

## Build / flash / regenerate

```text
make -j8 ENGINE=gigaverb           # build; the link prints SRAM_EXEC usage
make ENGINE=gigaverb program-dfu   # flash (device in DFU mode first)
make engine-gigaverb               # one-shot: clean + build + flash
make gen-engine MANIFEST=src/engine/gigaverb/gigaverb.json            # regenerate (gen-dsp + glue) from the manifest
make gen-engine MANIFEST=src/engine/gigaverb/gigaverb.json NOGEN=1 FORCE=1   # glue only (no gen-dsp), after editing the knob map
```
