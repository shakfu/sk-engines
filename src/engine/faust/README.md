# faust-spike (throwaway)

A minimal engine that wraps a [cyfaust](https://github.com/shakfu/cyfaust)-generated DSP kernel behind
`IEngine`, built to answer one question: **what does Faust-generated C++ cost in `SRAM_EXEC` on the
Daisy (STM32H7)?** This is a measurement spike, not a shippable engine - single voice, mono->stereo,
no sequencer/tape/MIDI.

## Files

- `voice.dsp` - the Faust source. A saw -> Moog ladder VCF -> ADSR voice. Closed-form (no large
  tables), so it measures generated *code* size rather than buffer/table memory.
- `faust_dsp.h` - **generated** (do not hand-edit). The cyfaust cpp-backend output (`class mydsp`),
  with a project preamble that pulls in the arch shim. Regenerate with `make faust-gen`.
- `faust_arch.h` - hand-written, MIT. The minimal `dsp` / `UI` / `Meta` base types the generated
  kernel assumes, so we avoid vendoring Faust's GPL-with-exception architecture headers.
- `faust_engine.{h,cpp}` - the `IEngine` wrapper. Holds one `::mydsp`, captures its param zones via
  `buildUserInterface` (`ParamUI`), drives them from the POS/PITCH/SOS/MOD_AMT knobs, gates the
  envelope from the Play pad, and meters the output on the rings.

## Build / measure / regenerate

cyfaust (the Cython wrapper of libfaust, with the full cpp backend) lives in a repo-local `.venv`:

```sh
python3 -m venv .venv && .venv/bin/pip install cyfaust   # one-time setup (.venv is gitignored)
```

```sh
make ENGINE=faust          # build; the link prints the SRAM_EXEC region usage
make engine-faust          # clean build + flash over DFU
make faust-gen             # regenerate faust_dsp.h from voice.dsp via .venv cyfaust
#   CYFAUST_PY=/path/to/python-with-cyfaust   # pin a different libfaust version
```

cyfaust runs only at codegen time on the host; the firmware build is unchanged
(`arm-none-eabi-g++`). The generated `faust_dsp.h` is checked in, so a normal build needs no cyfaust.

## Measured result (Faust 2.83.1, -O2, this voice.dsp)

`SRAM_EXEC` region is 186 KB. Region usage from `-Wl,--print-memory-usage`:

| Build | SRAM_EXEC | vs platform floor |
|---|---|---|
| `passthrough` (platform floor, no DSP) | 149304 B (78.4%) | - |
| `faust` (saw -> Moog VCF -> ADSR) | 153072 B (80.4%) | **+3768 B (~3.7 KB)** |
| `edrums` (hand-written 4-voice machine) | 159436 B (83.7%) | +10132 B (does more) |

Takeaway: a moderately complex *closed-form* Faust voice costs only ~3.7 KB of code here, leaving
~33 KB of `SRAM_EXEC` headroom - it does **not** threaten the budget. The overflow risk flagged when
scoping this lives elsewhere: table/delay-line-heavy Faust (reverbs, wavetables) spends **`SRAM`**
(data), not `SRAM_EXEC`, and `SRAM` is nearly empty here (~16%). Re-measure per block before adopting;
a reverb or a multi-mode physical model is the worst case to check next.
