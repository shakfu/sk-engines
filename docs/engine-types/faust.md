# Faust engines (cyfaust)

Author the DSP in [Faust](https://faust.grame.fr) (`.dsp`), generate C++ kernels with [cyfaust](https://github.com/shakfu/cyfaust) (a Cython wrapper of libfaust), and hand-write the `IEngine` wrapper that drives them. Best for closed-form signal-processing DSP - especially several interchangeable algorithms - where you still want full control of knob mapping, selection, and display.

**Implementations:** [reverb](../engines/reverb.md) (Dattorro plate + Zita hall, Alt+PITCH selects), [tape](../engines/tape.md) (a native engine whose `tapefx` wow/flutter + hysteresis is a Faust kernel). The build-system detail and footprint table also live in `src/engine/reverb/README.md`.

## The pipeline

```text
<name>.dsp ──cyfaust (cpp backend)──▶ faust_kernel_<name>.h   (class mydsp in namespace spotykach::<ns>)
                  make faust-gen                    │
   reverb_engine.cpp  ─ placement-new into arena ───┘ + CaptureUI binds zones to roles
```

`make faust-gen` reads `FAUST_KERNELS` in the root `Makefile` - one `<dir>:<namespace-prefix>:<name>` spec per kernel - and for each compiles `<dir>/<name>.dsp` to `<dir>/faust_kernel_<name>.h`. cyfaust runs only at codegen time on the host; the generated headers are checked in, so a normal `arm-none-eabi-g++` build needs no cyfaust.

cyfaust's cpp backend has no class-rename flag - every kernel is `class mydsp`. To let several coexist in one binary, `faust-gen`, per kernel: wraps the generated class in its own namespace `spotykach::<ns>`, rewrites the generated `__mydsp_H__` include guard to a per-kernel name (else the first kernel's guard suppresses the rest), and hoists the kernel's own `#include`s to global scope (a namespaced `#include` would pull `<cmath>` into the namespace). The class's unqualified `dsp`/`UI`/`Meta` base types then resolve to **`src/engine/faust_arch.h`** - a hand-written, MIT-licensed arch shim, so Faust's GPL-with-exception architecture headers are not vendored.

## The wrapper (what you hand-write)

The kernel is pure DSP; the engine `.cpp` wires it to the platform:

- **Arena construction.** A reverb's state is hundreds of KB of comb/allpass/FDN delay lines, which Faust emits as fixed member arrays of `class mydsp`. Held as a static value member that overflows `SRAM` (a static Zita member overflows by ~300 KB). So **placement-new each kernel into the SDRAM arena at `init()`** (the pattern reso uses for Rings); only pointers live in the engine. All kernels are built up front and only the **active** one's `compute()` runs per block - combining algorithms into one Faust `process` would run every one every sample (Faust has no branch elision).

- **Knob binding.** A generic `CaptureUI : public UI` walks each kernel's `buildUserInterface` and captures the `FAUSTFLOAT*` control zones by label, filling a Role table from a small per-kernel **bind list** (label -> role, plus the enclosing box where a label repeats). The platform's 0..1 knob is then linear-mapped into each slider's native range captured at bind time - so each algorithm's units (plate 0..1, hall RT60 seconds / damping Hz / pre-delay ms) just work with no per-algorithm scaling code.

- **Selection / display.** Multiple algorithms select via `CapAux` + Alt+PITCH (`round(v*(N-1))`, re-applying cached knob values to the newly-active kernel); `render` meters the output.

## Memory and footprint

As with all engines, arena placement-new keeps `SRAM` (data) flat regardless of delay-line size, so **`SRAM_EXEC` (code) is the binding constraint**. Measured (Faust 2.85.5, `-O2`, from `-Wl,--print-memory-usage`; `SRAM_EXEC` is 186 KB):

| Build | SRAM_EXEC | SRAM (data) |
|---|---|---|
| passthrough (platform floor) | 149304 B (78.4%) | ~52 KB |
| reverb, Dattorro only | 155912 B (81.9%) | 52104 B |
| reverb, Dattorro + Zita (shipped) | 174672 B (91.7%) | 52088 B |
| Zita as a static member (counter-example) | - | 638 KB -> SRAM overflow |

At ~92% a third sizable algorithm would need this engine built at `-Os` (as reso is) or one dropped.

## Setup, build, add

cyfaust lives in the repo-local `.venv` (gitignored):

```text
python3 -m venv .venv && .venv/bin/pip install cyfaust   # one-time
make ENGINE=reverb              # build; the link prints SRAM_EXEC usage
make ENGINE=reverb program-dfu  # flash (device in DFU first)
make engine-reverb              # one-shot: clean + build + flash
make faust-gen                  # regenerate faust_kernel_*.h for every spec in FAUST_KERNELS
#   CYFAUST_PY=/path/to/python  # pin a different libfaust version
```

**Add an algorithm:** drop `<name>.dsp` in the engine dir, append its spec to `FAUST_KERNELS`, bump the engine's kernel count, and register a bind table + concrete kernel type in the engine `.cpp`. Then re-check `SRAM_EXEC`.
