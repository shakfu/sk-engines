# gigaverb — stereo reverb from a gen~ patch

`ENGINE=gen_gigaverb` · `src/engine/gen_gigaverb/` · built via the [gen~ / gen-dsp method](../engine-types/gen.md)

The stock Max/MSP `gen~` reverb example (Tom Erbe's gigaverb), exported to C++ and run as an engine through gen-dsp - the first engine built this way, and the worked example for the method. The mechanism (genlib isolation, the arena-bound allocator, the generic `GenEngine<W>` wrapper, the codegen) is documented in [engine-types/gen.md](../engine-types/gen.md); this page is the gigaverb specifics.

## Status at a glance

- 2-in / 2-out, 8 parameters, no `[buffer]`s. Stereo, so it maps 1:1 onto the platform bus.

- Links and fits: `make ENGINE=gen_gigaverb` -> SRAM_EXEC 158732 B (83.3% of 186 KB) at the default `-O2`, no overflow, no `-Os` needed.

- `SRAM` (data) stays flat (~52 KB) despite the reverb delay lines, because all gen~ state is bump-allocated from the injected SDRAM arena.

- `capabilities() = 0`: stereo audio + knob params only (no custom display, MIDI, CV-out, or sequencing).

- Not yet flashed/heard on hardware.

## Control map

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

This table lives in `src/engine/gen_gigaverb/gigaverb_engine.h` (the `index_of()` switch) and is the one spot to retune; everything else is the generic wrapper. `ParamId::ModSpeed` is intentionally absent - the MODFREQ knob routes to `set_mod_speed()`, which a gen engine does not receive (see [the method doc](../engine-types/gen.md#control-map) for the full reachability rules and the knob grammar in [README.md](README.md)).

## Files

The shared `src/engine/gen/` family and the generated `src/engine/gen_gigaverb/` tree are described in [engine-types/gen.md](../engine-types/gen.md#files). gigaverb-specific:

- `src/engine/gen_gigaverb/gigaverb_engine.h` - the per-engine glue (traits + the control map above). Generated once, then hand-tuned; preserved across re-generation unless `--force-glue`.

- `src/engine/gen_gigaverb/gen/` - the copied gen~ export (Tom Erbe's gigaverb). Also the default source for `make gen-engines` (`GEN_EXPORTS` points back at it), so a regen is reproducible from the checkout.

## Build / flash / regenerate

```text
make clean && make -j8 ENGINE=gen_gigaverb     # build; the link prints SRAM_EXEC usage
make ENGINE=gen_gigaverb program-dfu           # flash (device in DFU mode first)
make gen-engines                               # regenerate from the vendored export (keeps the knob map)
```

## Notes / TODO

- No display: `capabilities() = 0`, so the platform shows its default UI. A meter would need `render()`.

- Reverb tail / CPU not measured on hardware. Fits SRAM_EXEC at 83% with headroom before `-Os` is needed.

- The `Mix` knob drives gigaverb's `dry` level (not a wet/dry blend); retune the map if a true mix is wanted.
