# reverb engine

A stereo reverb engine whose DSP is generated from [Faust](https://faust.grame.fr) sources by
[cyfaust](https://github.com/shakfu/cyfaust) and wrapped behind `IEngine`. It ships **two algorithms**
- a Dattorro plate and a Zita-rev1 hall - and **Alt+PITCH (`CapAux`) selects between them** at runtime.

It began as a throwaway spike to measure what Faust-generated C++ costs in `SRAM_EXEC` on the Daisy
(STM32H7); that measurement held up (closed-form DSP is a few KB of code), and the engine was promoted
to a real reverb once the memory model below was settled. The original spike voice (a saw -> Moog VCF
-> ADSR, `voice.dsp`) is kept as an alternate source for code-size measurement.

## Memory model (the one thing that matters)

A reverb's state is hundreds of KB of comb/allpass/FDN **delay lines** (Dattorro ~126 KB, Zita
~937 KB). Faust emits that state as fixed member arrays of the generated `class mydsp`, so holding a
kernel as a static value member puts it in `.bss` -> it overflows the 326 KB `SRAM` region (a static
Zita member overflows by ~300 KB; even Dattorro would eat half of free SRAM). The fix is the pattern
`ResoEngine` uses for Rings: **placement-new each kernel into the injected SDRAM arena at `init()`**.
Only pointers live in the engine object; `SRAM` stays flat and the 48 MB arena absorbs the delay lines.

Code size (`SRAM_EXEC`), not data, is therefore the binding constraint on how many algorithms fit: see
the table below.

## Multiple kernels in one build

cyfaust's cpp backend has no class-rename flag - every kernel is `class mydsp`. To let several coexist,
`make faust-gen` wraps each generated kernel in its own namespace `spotykach::rv_<name>` (and rewrites
the generated `__mydsp_H__` include guard to a per-reverb name, else the first kernel's guard suppresses
the rest). The kernel's own `#include`s are hoisted to global scope so a namespaced `#include` does not
pull `<cmath>` etc. into the namespace; the generated class's unqualified `dsp`/`UI`/`Meta` then resolve
to the global arch shim. Only the **active** kernel's `compute()` runs per block - combining algorithms
into one Faust `process` would run all of them every sample (Faust has no branch elision).

## Files

- `dattorro.dsp`, `zita.dsp` - the Faust reverb sources (the active set, listed in the Makefile's
  `RV_NAMES`). Both are MIT-licensed demos (`dm.dattorro_rev_demo` / `dm.zita_rev1`); the `maths.lib`
  they pull is LGPL with the standard Faust runtime exception.
- `voice.dsp` - the original spike voice (saw -> Moog VCF -> ADSR), retained as an alternate source.
- `faust_kernel_<name>.h` - **generated** (do not hand-edit). One per reverb, each `class mydsp` in
  namespace `spotykach::rv_<name>`. Regenerate with `make faust-gen`.
- `../faust_arch.h` (shared, `src/engine/faust_arch.h`) - hand-written, MIT. The minimal `dsp` / `UI` /
  `Meta` base types the generated kernels assume, so we avoid vendoring Faust's GPL-with-exception
  architecture headers. Shared with the tape engine's Faust kernel.
- `reverb_engine.{h,cpp}` - the `IEngine` wrapper. Constructs every kernel in the SDRAM arena, captures
  each one's control zones via `buildUserInterface` (a generic `CaptureUI` driven by a per-reverb bind
  table), maps the panel knobs onto reverb-agnostic roles, and selects the active algorithm on Aux.

## Control map

The six panel knobs map to reverb-agnostic *roles*; the 0..1 knob is linear-mapped into each Faust
slider's native range (captured at bind time), so each reverb's units (Dattorro 0..1, Zita's RT60
seconds / damping Hz / pre-delay ms) just work.

| knob | role | Dattorro (plate) | Zita (hall) |
|---|---|---|---|
| SOS | Mix | Dry/Wet (-1..+1) | Wet/Dry Mix |
| POS | Decay | Decay Rate | Mid RT60 |
| ENV | Damp | Damping | HF Damping |
| PITCH | Tone | Prefilter | Low RT60 |
| SIZE | SizeA | input Diffusion (x2) | In Delay (pre-delay) |
| MOD_AMT | SizeB | tank Diffusion (x2) | tail EQ (Eq1 Level, +/-15 dB) |
| **Alt+PITCH** | **Aux** | **algorithm select (plate / hall)** | |

Output Level is captured and held fixed; Zita's Eq2 and EQ frequencies keep their defaults. The rings meter
the output (plate = blue, hall = violet); while Alt+PITCH is held they show the selector.

Add an algorithm: drop `<name>.dsp` here, append `<name>` to `RV_NAMES` in the Makefile, bump
`kReverbCount`, and register a bind table + concrete `ReverbVoice` in `reverb_engine.cpp`.

## Build / regenerate / flash

cyfaust (the Cython wrapper of libfaust, with the full cpp backend) lives in a repo-local `.venv`:

```sh
python3 -m venv .venv && .venv/bin/pip install cyfaust   # one-time setup (.venv is gitignored)
```

```sh
make ENGINE=reverb          # build; the link prints the SRAM_EXEC region usage
make engine-reverb          # clean build + flash over DFU (device in DFU mode first)
make faust-gen             # regenerate faust_kernel_*.h for every reverb in RV_NAMES
#   CYFAUST_PY=/path/to/python-with-cyfaust   # pin a different libfaust version
```

cyfaust runs only at codegen time on the host; the firmware build is unchanged (`arm-none-eabi-g++`).
The generated `faust_kernel_*.h` are checked in, so a normal build needs no cyfaust.

## Measured footprint (Faust 2.85.5, -O2)

`SRAM_EXEC` is 186 KB; data lives in the SDRAM arena. From `-Wl,--print-memory-usage`:

| Build | SRAM_EXEC | SRAM (data) |
|---|---|---|
| `passthrough` (platform floor) | 149304 B (78.4%) | ~52 KB |
| `reverb` Dattorro only | 155912 B (81.9%) | 52104 B (15.6%) |
| `reverb` Dattorro + Zita (shipped) | 174672 B (91.7%) | 52088 B (15.6%) |
| Zita as a *static member* (counter-example) | - | **638 KB -> SRAM overflow** |

Two takeaways: (1) the arena placement-new keeps `SRAM` flat regardless of how much delay-line state
the reverbs hold - contrast the static-member overflow; (2) `SRAM_EXEC` is now the binding constraint
at ~92% (Zita's compute added ~18 KB). A third sizable algorithm would need this engine built at `-Os`
(as `reso` is) or one of the current two dropped.
