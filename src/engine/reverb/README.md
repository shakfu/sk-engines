# reverb engine (sources)

A stereo reverb (Dattorro plate + Zita-rev1 hall, Alt+PITCH selects) whose DSP is Faust-generated.

- **Engine documentation:** `../../../docs/engines/reverb.md` (concept, control map, footprint, files).
- **How it is built:** the Faust / cyfaust method - `../../../docs/engine-types/faust.md` (codegen,
  namespacing, the arch shim, arena placement-new, the generic `CaptureUI` binding).

This directory holds only the engine's sources; the method docs above own the build mechanics.

## Files here

- `dattorro.dsp`, `zita.dsp` - the active Faust reverb sources (MIT demos `dm.dattorro_rev_demo` /
  `dm.zita_rev1`; the `maths.lib` they pull is LGPL with the Faust runtime exception).
- `voice.dsp` - the original spike voice (saw -> Moog VCF -> ADSR), retained as an alternate code-size
  source; not built.
- `faust_kernel_<name>.h` - **generated** (do not hand-edit). One `class mydsp` per reverb, each in
  namespace `spotykach::rv_<name>`. Regenerate with `make faust-gen`. The shared, hand-written arch shim
  is `../faust_arch.h` (MIT).
- `reverb_engine.{h,cpp}` - the `IEngine` wrapper: constructs every kernel in the SDRAM arena, captures
  each one's zones via `buildUserInterface` (a generic `CaptureUI` + a per-reverb bind table), maps the
  panel knobs onto reverb-agnostic roles, and selects the active algorithm on Aux.

## `faust-gen` specs

The reverb's kernels are listed in the root `Makefile`'s `FAUST_KERNELS` (one `<dir>:<prefix>:<name>`
spec each):

```text
src/engine/reverb:rv_:dattorro   src/engine/reverb:rv_:zita
```

Add an algorithm: drop `<name>.dsp` here, append its spec, bump `ReverbEngine::kReverbCount`, and register
a bind table + concrete `ReverbVoice` in `reverb_engine.cpp`. Then re-check `SRAM_EXEC` (the reverb sits
at ~92% with two algorithms - see `../../../docs/engines/reverb.md`).
