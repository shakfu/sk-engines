# reverb engine

A stereo reverb whose DSP is generated from [Faust](https://faust.grame.fr) sources by
[cyfaust](https://github.com/shakfu/cyfaust) and wrapped behind `IEngine`. It ships **two algorithms** -
a **Dattorro plate** and a **Zita-rev1 hall** - and **Alt+PITCH (`CapAux`) selects between them** live.

The companion `src/engine/reverb/README.md` carries the build-system detail (codegen, namespacing, the
exact footprint table); this page covers concept, control map, and the architecture decisions.

## Status at a glance

- Implemented and integrated end to end (engine, build system, two reverb kernels).
- Host test suite passes (`make -C test test` -> 116 passed) - the reverb engine has no host test of its
  own yet (it is firmware-only); the suite confirms nothing else regressed.
- Firmware links and fits: `make ENGINE=reverb` -> SRAM_EXEC 174672 B (91.7% of 186 KB), no overflow.
- `SRAM` (data) stays flat at ~52 KB despite ~1 MB of reverb delay-line state, because every kernel is
  placement-new'd into the SDRAM arena (see Memory model).

## Concept

It began as a measurement spike - *what does Faust-generated C++ cost in `SRAM_EXEC` on the H7?* - and
was promoted to a real reverb once that question was answered (closed-form DSP is a few KB of code) and
the memory model was settled. The original spike voice (a saw -> Moog VCF -> ADSR, `voice.dsp`) is kept
as an alternate source for code-size measurement, not wired into the build.

Two reverbs were chosen as a deliberately contrasting pair:

- **Dattorro plate** (`dm.dattorro_rev_demo`, ~126 KB state) - a modulated figure-8 plate (JAES 1997,
  "Effect Design Part 1"). Short, lush, the tank's LFO excursion avoids the metallic ring of a fixed
  comb reverb. MIT-licensed.
- **Zita-rev1 hall** (`dm.zita_rev1`, ~937 KB state) - Fons Adriaensen's reference free FDN hall: an
  8x8 feedback-delay network with frequency-dependent decay (separate low/mid RT60) and HF damping.
  Lusher and longer than the plate. MIT-licensed.

(Both pull `maths.lib`, LGPL with the standard Faust runtime exception.)

## Control map

The six panel knobs map to reverb-agnostic **roles**; the 0..1 knob is linear-mapped into each Faust
slider's native range, captured at bind time - so each reverb's units (plate 0..1, hall RT60 seconds /
damping Hz / pre-delay ms) just work without per-reverb scaling code.

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
0 dB); Zita's Eq1 Level is the SizeB knob (tail tone), Eq2 and the EQ frequencies stay default. Aux quantizes its 0..1 value to a reverb
index (`round(v * (N-1))`, the same idiom reso uses for model select) and re-applies the cached knob
values to the newly-active kernel, so a switch never strands a knob.

The display (`render`) meters the output on both rings - **plate = blue, hall = violet** - and, while
Alt+PITCH is held, shows the selector (one dot per algorithm, the active one bright).

## Memory model (the load-bearing decision)

A reverb's state is hundreds of KB of comb/allpass/FDN **delay lines**, which Faust emits as fixed
member arrays of the generated `class mydsp`. Holding a kernel as a static value member puts that state
in `.bss` and overflows the 326 KB `SRAM` region (a static Zita member overflows by ~300 KB). The fix is
the pattern `ResoEngine` uses for Rings: **placement-new each kernel into the injected SDRAM arena at
`init()`**; only pointers live in the engine object. All kernels are constructed up front and only the
**active** one's `compute()` runs per block, so switching is a pointer flip with no audio-thread
allocation. (Combining the algorithms into one Faust `process` would run every one every sample - Faust
has no branch elision - so separate kernels are also the CPU-cheap design.)

Consequence: `SRAM` (data) is no longer the constraint; **`SRAM_EXEC` (code) is**, now ~92% with both
kernels (Zita's compute added ~18 KB). A third sizable algorithm would need this engine built at `-Os`
(as reso is) or one of the two dropped.

## Multiple kernels in one build

cyfaust's cpp backend has no class-rename flag - every kernel is `class mydsp`. `make faust-gen` makes
them coexist by, per reverb: wrapping the generated class in namespace `spotykach::rv_<name>`, rewriting
the generated `__mydsp_H__` include guard to a per-reverb name (else the first kernel's guard suppresses
the rest - the one real bug hit during bring-up), and hoisting the kernel's own `#include`s to global
scope (a namespaced `#include` would pull `<cmath>` into the namespace). The generated class's
unqualified `dsp`/`UI`/`Meta` then resolve to the global arch shim `faust_arch.h` (hand-written, MIT, so
Faust's GPL-with-exception architecture headers are not vendored).

Knob binding is data-driven: a generic `CaptureUI` walks each kernel's `buildUserInterface` and fills a
Role table from a small per-reverb **bind list** (label -> role, plus enclosing box where a label is
reused - Dattorro's "Diffusion 1/2" appear in both its Input and Feedback boxes).

## Files

- `src/engine/reverb/dattorro.dsp`, `zita.dsp` - the active reverb sources (listed in the Makefile's
  `RV_NAMES`). `voice.dsp` - the retained spike voice (not built).
- `src/engine/reverb/faust_kernel_<name>.h` - **generated** (do not hand-edit), one `class mydsp` per
  namespace `rv_<name>`. Regenerate with `make faust-gen`.
- `src/engine/reverb/faust_arch.h` - hand-written MIT `dsp`/`UI`/`Meta` base types.
- `src/engine/reverb/reverb_engine.{h,cpp}` - the `IEngine` wrapper (arena construction, `CaptureUI` +
  bind tables, role mapping, Aux selector, render).
- `src/engine/reverb/README.md` - build-system detail and the footprint table.
- Registered in `src/engine/engine_select.h` and the root `Makefile` (`ENGINE=reverb`, `engine-reverb`
  flash target, `faust-gen` codegen target).

## Build / regenerate / flash

cyfaust (the Cython libfaust wrapper, full cpp backend) lives in a repo-local `.venv`:

```text
python3 -m venv .venv && .venv/bin/pip install cyfaust   # one-time (.venv is gitignored)
make -j8 ENGINE=reverb          # build; the link prints SRAM_EXEC usage
make ENGINE=reverb program-dfu  # flash (device in DFU mode first)
make engine-reverb              # one-shot: clean + build + flash
make faust-gen                 # regenerate faust_kernel_*.h for every reverb in RV_NAMES
#   CYFAUST_PY=/path/to/python   # pin a different libfaust version
```

cyfaust runs only at codegen time on the host; the firmware build is plain `arm-none-eabi-g++`. The
generated `faust_kernel_*.h` are checked in, so a normal build needs no cyfaust.

## Adding a reverb

Drop `<name>.dsp` into `src/engine/reverb/`, append `<name>` to `RV_NAMES` in the root `Makefile`, bump
`ReverbEngine::kReverbCount`, and register a bind table + concrete `ReverbVoice` in `reverb_engine.cpp`.
Then re-check `SRAM_EXEC`: with two reverbs it sits at ~92%, so a third likely needs `OPT = -Os` on the
reverb branch of the `Makefile`.

## Notes / TODO

- No host test yet. A reverb host test would exercise param round-trip, the Aux selector switch, and
  that each kernel binds all six roles (a null role would silently no-op a knob).
- CPU not measured on hardware (`Meter::cpu`). Only the active kernel computes, so worst case is the
  single most expensive algorithm (Zita), not the sum.
- Licensing: the chosen demos are MIT; confirm the license of any `stdfaust.lib` functions a new reverb
  pulls before shipping it.
