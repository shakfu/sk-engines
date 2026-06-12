# reverb engine

`ENGINE=reverb` · `src/engine/reverb/reverb_engine.{h,cpp}` · class `ReverbEngine`

A **route-aware** stereo reverb that ships **two algorithms** - a **Dattorro plate** and a **Zita-rev1 hall** - with the physical **Reel/Slice/Drift mode switch selecting between them** live, per deck. An optional third voice (the gen~ **gigaverb**) folds in at build time. Built with the [Faust / cyfaust method](../engine-types/faust.md); that page covers the pipeline (codegen, namespacing, the arch shim, arena placement-new, the generic `CaptureUI` binding). This page is the reverb specifics.

## Status at a glance

- Implemented and integrated end to end (engine, build system, two Faust kernels + optional gen~ gigaverb).

- Host-tested: `make -C host test-reverb` (param round-trip, both algorithms ring out, every knob role is bound, the mode switch swaps the kernel, and DoubleMono per-deck independence + channel isolation). Plus `make -C host bench-reverb` for the process() cost.

- Links and fits: `make ENGINE=reverb` -> SRAM_EXEC ~92% of 186 KB, no overflow.

- `SRAM` (data) stays flat at ~52 KB despite a few MB of reverb delay-line state, because every kernel is placement-new'd into the SDRAM arena.

## The two algorithms

A deliberately contrasting pair, both MIT-licensed Faust demos (the `maths.lib` they pull is LGPL with the standard Faust runtime exception):

- **Dattorro plate** (`dm.dattorro_rev_demo`, ~126 KB state) - a modulated figure-8 plate (JAES 1997, "Effect Design Part 1"). Short, lush; the tank's LFO excursion avoids the metallic ring of a fixed-comb reverb.

- **Zita-rev1 hall** (`dm.zita_rev1`, ~937 KB state) - Fons Adriaensen's reference FDN hall: an 8x8 feedback-delay network with frequency-dependent decay (separate low/mid RT60) and HF damping. Lusher and longer than the plate.

It began as a spike to measure what Faust-generated C++ costs in `SRAM_EXEC` on the H7, and was promoted to a real reverb once that held up (closed-form DSP is a few KB of code). The original spike voice (a saw -> Moog VCF -> ADSR, `voice.dsp`) is retained as an alternate source for code-size measurement, not wired into the build.

## Route-aware topology

The reverb reads the panel **routing switch** (`ConfigId::Route`) and adapts, like the rest of the instrument:

- **Stereo / GenerativeStereo** -> **one stereo voice** (deck A's selection) reverberates the stereo pair. Deck A's knobs + mode switch drive it; deck B's strip is inert.

- **DoubleMono** -> **an independent mono reverb per deck**: deck A reverberates the left input into the left output, deck B the right - each deck's full knob bank + mode switch drives its own side, both live. The two sides are channel-isolated (a deck with a silent input stays silent - no crosstalk). Each side is mono (the stereo voice is run mono: input fanned to both channels, one output kept), so it trades each reverb's internal stereo width for two independent sends. Costs ~1.7x a single voice on the host (process-only); the real device load comes from the METER build.

A full set of voices is allocated **per deck** up front, so the route is a runtime branch in `process()` with no reallocation. Per-deck footprint is ~2.1 MB (Faust) or ~6.2 MB with gigaverb - trivial against the 48 MB arena.

## Control map

The six panel knobs map to reverb-agnostic **roles**; the 0..1 knob is linear-mapped into each Faust slider's native range, captured at bind time - so each reverb's units (plate 0..1, hall RT60 seconds / damping Hz / pre-delay ms) just work without per-reverb scaling code.

| Knob | `ParamId` | role | Dattorro (plate) | Zita (hall) |
|---|---|---|---|---|
| SOS | `Mix` | Mix | Dry/Wet (-1..+1) | Wet/Dry Mix |
| **PITCH** | `Speed` | **Decay** | Decay Rate | Mid RT60 |
| ENV | `Env` | Damp | Damping | HF Damping |
| **POS** | `Pos` | **Tone** | Prefilter | Low RT60 |
| SIZE | `Size` | SizeA | input Diffusion (x2) | In Delay (pre-delay) |
| MOD_AMT | `ModAmp` | SizeB | tank Diffusion (x2) | tail EQ (Eq1 Level, +/-15 dB) |
| Reel/Slice/Drift switch | `ConfigId::Mode` | select | **algorithm: plate / hall (/ gigaverb)** | |

**Decay** is on **PITCH** (the biggest knob) since it is the most impactful character control; **Tone** is on POS. Selection moved from the old Alt+PITCH (`CapAux`) gesture to the dedicated **3-position Reel/Slice/Drift switch** - one tactile position per algorithm. The switch is per-deck on the hardware: deck A selects in a stereo route; in DoubleMono each deck selects its own side.

`capabilities() = CapOwnDisplay | CapDualDeck`. Output Level is captured and held fixed (plate -6 dB, hall 0 dB); Zita's Eq1 Level is the SizeB knob (tail tone), Eq2 and the EQ frequencies stay default. The mode switch's 0/1/2 position maps straight to the voice index and re-applies the cached knob values to the newly-active kernel, so a switch never strands a knob.

The display (`render`) meters the output per deck - **plate = blue, hall = violet, gigaverb = mint** - and the three mode L/C/R LEDs show the **route** (platform convention: DoubleMono / Stereo / GenerativeStereo).

A reverb-specific binding wrinkle: a Faust label can repeat across boxes - Dattorro's "Diffusion 1/2" appear in both its Input and Feedback boxes - so each bind-list entry keys on the enclosing box as well as the label. (The generic zone-capture mechanism is in [faust.md](../engine-types/faust.md).)

## Optional third voice: gen~ gigaverb

`make ENGINE=reverb REVERB_GIGAVERB=1` folds the [gen~ gigaverb](gigaverb.md) export in as a **third selectable voice** (mode-switch position 3, mint). It is wrapped as a `GigaverbVoice` (a non-Faust `ReverbVoice` that drives the gen~ export by parameter index rather than the Faust zone table), with its gen~ state bump-allocated from a dedicated ~2 MB slice of the arena **per deck** (two independent gen~ instances are safe - genlib data objects are per-allocation, no global name registry). gigaverb has no wet/dry control, so the voice pins gen~ `dry` to 0 and crossfades wet against the dry input itself, matching the "knob up = wetter" behaviour of the Faust voices. Host-verified via `make -C host test-reverb-giga`. Pulls in the gen~ runtime (~9 KB code), pushing SRAM_EXEC to ~97%; plain `make ENGINE=reverb` stays a lean two-voice build with no gen~ dependency.

## Footprint (Faust 2.85.5, -O2)

Arena placement-new keeps `SRAM` (data) flat regardless of delay-line size, so **`SRAM_EXEC` (code) is the binding constraint** - not SDRAM (the per-deck voices live inside the static 48 MB arena, whose region usage is unchanged). From `-Wl,--print-memory-usage` (`SRAM_EXEC` is 186 KB):

| Build | SRAM_EXEC |
|---|---|
| `passthrough` (platform floor) | ~149 KB (78%) |
| `reverb` (plate + hall, route-aware) | ~175 KB (92.0%) |
| `reverb METER=1` | ~176 KB (92.2%) |
| `reverb REVERB_GIGAVERB=1` (3 voices, per-deck gen~) | ~185 KB (97.0%) |

At ~92% a fourth sizable Faust algorithm would need this engine built at `-Os` (as reso is) or one dropped; the gigaverb fold-in already sits near the ceiling.

## Files

- `src/engine/reverb/dattorro.dsp`, `zita.dsp` - the active reverb sources. `voice.dsp` - the retained spike voice (not built).

- `src/engine/reverb/faust_kernel_<name>.h` - **generated** (do not hand-edit), one `class mydsp` per namespace `rv_<name>`. Regenerate with `make faust-gen`.

- `src/engine/reverb/reverb_engine.{h,cpp}` - the `IEngine` wrapper (per-deck arena construction, `CaptureUI` + per-reverb bind tables, role mapping, mode-switch + route handling, render). The optional `GigaverbVoice` and its includes are `#if defined(SPK_REVERB_GIGAVERB)`.

- `host/test_reverb.cpp`, `host/test_reverb_giga.cpp`, `host/bench_reverb.cpp` - the host test, the gigaverb-fold-in test, and the process() cost benchmark.

- Registered in `src/engine/engine_select.h` and the root `Makefile` (`ENGINE=reverb`, `engine-reverb` flash target, `faust-gen` codegen target).

## Build / flash

```text
make -j8 ENGINE=reverb                 # build; the link prints SRAM_EXEC usage
make ENGINE=reverb REVERB_GIGAVERB=1   # + fold in the gen~ gigaverb as a 3rd voice
make ENGINE=reverb METER=1             # + on-device CPU load meter (reads over serial)
make ENGINE=reverb program-dfu         # flash (device in DFU mode first)
make engine-reverb                     # one-shot: clean + build + flash
make faust-gen                         # regenerate faust_kernel_*.h
make -C host test-reverb               # host test (also: test-reverb-giga, bench-reverb)
```

See [faust.md](../engine-types/faust.md) for the cyfaust `.venv` setup and the codegen detail.

## Adding a reverb

Follow the generic [add-an-algorithm](../engine-types/faust.md#setup-build-add) steps (drop `<name>.dsp`, append its `FAUST_KERNELS` spec, bump `ReverbEngine::kReverbCount`, register a bind table + concrete `ReverbVoice`). Then re-check `SRAM_EXEC`: with two reverbs it sits at ~92%, so a third Faust algorithm likely needs `OPT = -Os` on the reverb branch of the `Makefile`.

## Notes / TODO

- **CPU not yet measured on hardware.** `make ENGINE=reverb METER=1` enables `Meter::cpu` (prints Max/Avg/Min load over serial). The worst case is the DoubleMono route (two voices/block, ~1.7x a single on the host); flash, set DoubleMono with both decks on the hall, and read the serial log for the real %.

- **Hardware bring-up checks:** the mode-switch position -> voice-index order, and the route -> L/C/R LED order, are mapped to the natural 0/1/2; confirm they match the silkscreen on first flash (each is a one-line reorder if not).

- Licensing: the chosen demos are MIT; confirm the license of any `stdfaust.lib` functions a new reverb pulls before shipping it.
