# reverb engine

`ENGINE=reverb` · `src/engine/reverb/reverb_engine.{h,cpp}` · class `ReverbEngine`

A stereo reverb that ships **two algorithms** - a **Dattorro plate** and a **Zita-rev1 hall** - with **Alt+PITCH (`CapAux`) selecting between them** live. Built with the [Faust / cyfaust method](../engine-types/faust.md); that page covers the pipeline (codegen, namespacing, the arch shim, arena placement-new, the generic `CaptureUI` binding). This page is the reverb specifics.

## Status at a glance

- Implemented and integrated end to end (engine, build system, two reverb kernels).

- Firmware-only (no host test of its own); the host suite confirms nothing else regressed.

- Links and fits: `make ENGINE=reverb` -> SRAM_EXEC 174672 B (91.7% of 186 KB), no overflow.

- `SRAM` (data) stays flat at ~52 KB despite ~1 MB of reverb delay-line state, because every kernel is placement-new'd into the SDRAM arena.

## The two algorithms

A deliberately contrasting pair, both MIT-licensed Faust demos (the `maths.lib` they pull is LGPL with the standard Faust runtime exception):

- **Dattorro plate** (`dm.dattorro_rev_demo`, ~126 KB state) - a modulated figure-8 plate (JAES 1997, "Effect Design Part 1"). Short, lush; the tank's LFO excursion avoids the metallic ring of a fixed-comb reverb.

- **Zita-rev1 hall** (`dm.zita_rev1`, ~937 KB state) - Fons Adriaensen's reference FDN hall: an 8x8 feedback-delay network with frequency-dependent decay (separate low/mid RT60) and HF damping. Lusher and longer than the plate.

It began as a spike to measure what Faust-generated C++ costs in `SRAM_EXEC` on the H7, and was promoted to a real reverb once that held up (closed-form DSP is a few KB of code). The original spike voice (a saw -> Moog VCF -> ADSR, `voice.dsp`) is retained as an alternate source for code-size measurement, not wired into the build.

## Control map

The six panel knobs map to reverb-agnostic **roles**; the 0..1 knob is linear-mapped into each Faust slider's native range, captured at bind time - so each reverb's units (plate 0..1, hall RT60 seconds / damping Hz / pre-delay ms) just work without per-reverb scaling code.

| Knob | `ParamId` | role | Dattorro (plate) | Zita (hall) |
|---|---|---|---|---|
| SOS | `Mix` | Mix | Dry/Wet (-1..+1) | Wet/Dry Mix |
| POS | `Pos` | Decay | Decay Rate | Mid RT60 |
| ENV | `Env` | Damp | Damping | HF Damping |
| PITCH | `Speed` | Tone | Prefilter | Low RT60 |
| SIZE | `Size` | SizeA | input Diffusion (x2) | In Delay (pre-delay) |
| MOD_AMT | `ModAmp` | SizeB | tank Diffusion (x2) | tail EQ (Eq1 Level, +/-15 dB) |
| Alt+PITCH | `Aux` | select | **algorithm: plate / hall** | |

`capabilities() = CapOwnDisplay | CapAux`. Output Level is captured and held fixed (plate -6 dB, hall
0 dB); Zita's Eq1 Level is the SizeB knob (tail tone), Eq2 and the EQ frequencies stay default. Aux quantizes its 0..1 value to a reverb index (`round(v * (N-1))`, the same idiom reso uses for model select) and re-applies the cached knob values to the newly-active kernel, so a switch never strands a knob.

The display (`render`) meters the output on both rings - **plate = blue, hall = violet** - and, while Alt+PITCH is held, shows the selector (one dot per algorithm, the active one bright).

A reverb-specific binding wrinkle: a Faust label can repeat across boxes - Dattorro's "Diffusion 1/2" appear in both its Input and Feedback boxes - so each bind-list entry keys on the enclosing box as well as the label. (The generic zone-capture mechanism is in [faust.md](../engine-types/faust.md).)

## Footprint (Faust 2.85.5, -O2)

Arena placement-new keeps `SRAM` (data) flat regardless of delay-line size, so **`SRAM_EXEC` (code) is the binding constraint**. From `-Wl,--print-memory-usage` (`SRAM_EXEC` is 186 KB):

| Build | SRAM_EXEC | SRAM (data) |
|---|---|---|
| `passthrough` (platform floor) | 149304 B (78.4%) | ~52 KB |
| `reverb` Dattorro only | 155912 B (81.9%) | 52104 B |
| `reverb` Dattorro + Zita (shipped) | 174672 B (91.7%) | 52088 B |
| Zita as a *static member* (counter-example) | - | 638 KB -> SRAM overflow |

At ~92% (Zita's compute added ~18 KB), a third sizable algorithm would need this engine built at `-Os` (as reso is) or one of the two dropped.

## Files

- `src/engine/reverb/dattorro.dsp`, `zita.dsp` - the active reverb sources. `voice.dsp` - the retained spike voice (not built).

- `src/engine/reverb/faust_kernel_<name>.h` - **generated** (do not hand-edit), one `class mydsp` per namespace `rv_<name>`. Regenerate with `make faust-gen`.

- `src/engine/reverb/reverb_engine.{h,cpp}` - the `IEngine` wrapper (arena construction, `CaptureUI` + per-reverb bind tables, role mapping, Aux selector, render).

- `src/engine/reverb/README.md` - in-source pointer + the per-reverb `FAUST_KERNELS` specs.

- Registered in `src/engine/engine_select.h` and the root `Makefile` (`ENGINE=reverb`, `engine-reverb` flash target, `faust-gen` codegen target).

## Build / flash

```text
make -j8 ENGINE=reverb          # build; the link prints SRAM_EXEC usage
make ENGINE=reverb program-dfu  # flash (device in DFU mode first)
make engine-reverb              # one-shot: clean + build + flash
make faust-gen                  # regenerate faust_kernel_*.h
```

See [faust.md](../engine-types/faust.md) for the cyfaust `.venv` setup and the codegen detail.

## Adding a reverb

Follow the generic [add-an-algorithm](../engine-types/faust.md#setup-build-add) steps (drop `<name>.dsp`, append its `FAUST_KERNELS` spec, bump `ReverbEngine::kReverbCount`, register a bind table + concrete `ReverbVoice`). Then re-check `SRAM_EXEC`: with two reverbs it sits at ~92%, so a third likely needs `OPT = -Os` on the reverb branch of the `Makefile`.

## Notes / TODO

- No host test yet. A reverb host test would exercise param round-trip, the Aux selector switch, and that each kernel binds all six roles (a null role would silently no-op a knob).

- CPU not measured on hardware (`Meter::cpu`). Only the active kernel computes, so worst case is the single most expensive algorithm (Zita), not the sum.

- Licensing: the chosen demos are MIT; confirm the license of any `stdfaust.lib` functions a new reverb pulls before shipping it.
