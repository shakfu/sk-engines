# reverb engine

`ENGINE=reverb` · `src/engine/reverb/reverb_engine.{h,cpp}` · class `ReverbEngine`

A **route-aware** stereo reverb that ships **two algorithms** - a **Dattorro plate** and a **Zita-rev1 hall** - with the physical **Reel/Slice/Drift mode switch selecting between them** live, per deck. An optional third voice (the gen~ **gigaverb**) folds in at build time. Built with the [Faust / cyfaust method](../engine-types/faust.md); that page covers the pipeline (codegen, namespacing, the arch shim, arena placement-new, the generic `CaptureUI` binding). This page is the reverb specifics.

## Status at a glance

- Implemented and integrated end to end (engine, build system, two Faust kernels + optional gen~ gigaverb).

- Host-tested: `make -C host test-reverb` (param round-trip, both algorithms ring out, every knob role is bound, the mode switch swaps the kernel, the DoubleMono plate-only cap, and channel isolation). Plus `make -C host bench-reverb` for the process() cost and `make ENGINE=reverb METER=1` for on-device CPU load (non-blocking serial; won't hang).

- CPU measured on hardware (METER build): one hall ~18.6%, DoubleMono two plates ~41%/61% peak; the capped corner (two halls) ~70%/89% can no longer occur. The reverb is SDRAM-latency-bound, so the cap is what keeps the worst block comfortably under the 2 ms deadline.

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

- **DoubleMono** -> **two independent mono PLATES, one per deck**: deck A reverberates the left input into the left output, deck B the right, each driven by its own knob bank. The two sides are channel-isolated (a deck with a silent input stays silent - no crosstalk). Each side is mono (the plate is run mono: input fanned to both channels, one output kept), trading the plate's internal stereo width for two independent sends.

**The cap: DoubleMono is plate-only.** The hall and gigaverb are heavy and SDRAM-bound; *two* of them at once thrash the H7's D-cache (measured ~70% avg / ~89% peak on hardware - too close to the 2 ms deadline). So the heavy voices are **single-voice (stereo route) only** - in DoubleMono the Mode switch is forced to plate (`eff_voice()`), which can never put two heavy voices on the bus at once. The switch selection is remembered, so flipping back to a stereo route restores the chosen hall/gigaverb.

A full set of voices is allocated **per deck** up front, so the route is a runtime branch in `process()` with no reallocation. Per-deck footprint is ~2.1 MB (Faust) or ~6.2 MB with gigaverb - trivial against the 48 MB arena.

### Measured device load (`METER` build, worst block)

| Config | Avg | Peak |
|---|---|---|
| 1 voice (Stereo, hall) | ~18.6% | — |
| DoubleMono, two plates (the cap) | ~40.8% | ~60.8% |
| ~~DoubleMono, two halls~~ | ~~70.5%~~ | ~~88.9%~~ (prevented by the cap) |

The reverb is **memory-latency-bound** (delay lines in SDRAM), so two heavy voices scale ~3.8x, not 2x - the host benchmark (which can't model SDRAM) under-predicted both the absolute load and the scaling. The cap keeps the worst case at two plates (~61% peak). To lift the cap and allow two heavies later, the levers are: move the plate to internal SRAM (de-contends a plate+heavy pairing), or downsample/shrink the hall (a smaller FDN, not just shorter delays - per-sample cost is set by tap *count*, not buffer size).

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

**Decay** is on **PITCH** (the biggest knob) since it is the most impactful character control; **Tone** is on POS. Selection moved from the old Alt+PITCH (`CapAux`) gesture to the dedicated **3-position Reel/Slice/Drift switch** - one tactile position per algorithm. The switch is per-deck on the hardware, but only takes effect in a stereo route: there deck A's switch selects the voice; in DoubleMono the voice is forced to plate (the cap below), so the switch is inert (selection is remembered for when you return to a stereo route).

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
| `reverb METER=1` (adds the USB CDC stack) | ~187 KB (98.2%) |
| `reverb REVERB_GIGAVERB=1` (3 voices, per-deck gen~) | ~185 KB (97.0%) |

At ~92% a fourth sizable Faust algorithm would need this engine built at `-Os` (as reso is) or one dropped; the gigaverb fold-in already sits near the ceiling. The `METER` build adds ~12 KB (the USB CDC stack, not the meter itself), so the **3-voice + METER** build overflows `-O2` by ~6 KB and needs `OPT=-Os` to fit.

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

- **CPU measured on hardware** (`make ENGINE=reverb METER=1`, `Meter::cpu` over the external USB CDC, non-blocking): one stereo hall **~18.6%**, DoubleMono two plates **~40.8% avg / ~61% peak**, single ~13-19%. The capped corner (two halls) measured ~70%/89% before the cap and can no longer occur. The reverb is **SDRAM-latency-bound** - cost scales super-linearly with concurrent heavy voices (~3.8x for two halls, from D-cache contention), which the host bench (~1.7x, no SDRAM model) badly under-predicted. To lift the cap later: move the plate's delay lines to internal AXI SRAM (de-contends a plate+heavy pairing - a hall's 937 KB can't fit internally), or shrink the hall's FDN *order* (per-sample cost is tap count, not delay length).

- **Hardware bring-up checks:** the mode-switch position -> voice-index order, and the route -> L/C/R LED order, are mapped to the natural 0/1/2; confirm they match the silkscreen on first flash (each is a one-line reorder if not).

- Licensing: the chosen demos are MIT; confirm the license of any `stdfaust.lib` functions a new reverb pulls before shipping it.
