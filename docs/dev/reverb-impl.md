# Dev notes — route-aware Faust reverb (`reverb` engine)

Implementation/bring-up notes for `ENGINE=reverb`. The user-facing reference (the two algorithms, route-aware topology, control map, the optional gigaverb voice, build/flash commands) is [`docs/engines/reverb.md`](../engines/reverb.md); this file holds the status, the Faust footprint and on-device load measurements, the file map, and the "adding a reverb" how-to.

## Status at a glance

- Implemented and integrated end to end (engine, build system, two Faust kernels + optional gen~ gigaverb).

- Host-tested: `make -C host test-reverb` (param round-trip, both algorithms ring out, every knob role is bound, the mode switch swaps the kernel, the DoubleMono plate-only cap, and channel isolation). Plus `make -C host bench-reverb` for the process() cost and `make ENGINE=reverb METER=1` for on-device CPU load (non-blocking serial; won't hang).

- CPU measured on hardware (METER build): one hall ~18.6%, DoubleMono two plates ~41%/61% peak; the capped corner (two halls) ~70%/89% can no longer occur. The reverb is SDRAM-latency-bound, so the cap is what keeps the worst block comfortably under the 2 ms deadline.

- Links and fits: `make ENGINE=reverb` -> SRAM_EXEC ~92% of 186 KB, no overflow.

- `SRAM` (data) stays flat at ~52 KB despite a few MB of reverb delay-line state, because every kernel is placement-new'd into the SDRAM arena.

## Measured device load (`METER` build, worst block)

| Config | Avg | Peak |
|---|---|---|
| 1 voice (Stereo, hall) | ~18.6% | — |
| DoubleMono, two plates (the cap) | ~40.8% | ~60.8% |
| ~~DoubleMono, two halls~~ | ~~70.5%~~ | ~~88.9%~~ (prevented by the cap) |

The reverb is **memory-latency-bound** (delay lines in SDRAM), so two heavy voices scale ~3.8x, not 2x - the host benchmark (which can't model SDRAM) under-predicted both the absolute load and the scaling. The cap keeps the worst case at two plates (~61% peak). To lift the cap and allow two heavies later, the levers are: move the plate to internal SRAM (de-contends a plate+heavy pairing), or downsample/shrink the hall (a smaller FDN, not just shorter delays - per-sample cost is set by tap *count*, not buffer size).

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

- `src/engine/reverb/faust_kernel_<name>.h` - **generated** (do not hand-edit), one `class mydsp` per namespace `rv_<name>`. Regenerate with `make faust-kernels`.

- `src/engine/reverb/reverb_engine.{h,cpp}` - the `IEngine` wrapper (per-deck arena construction, `CaptureUI` + per-reverb bind tables, role mapping, mode-switch + route handling, render). The optional `GigaverbVoice` and its includes are `#if defined(SPK_REVERB_GIGAVERB)`.

- `host/test_reverb.cpp`, `host/test_reverb_giga.cpp`, `host/bench_reverb.cpp` - the host test, the gigaverb-fold-in test, and the process() cost benchmark.

- Registered in `src/engine/engine_select.h` and the root `Makefile` (`ENGINE=reverb`, `engine-reverb` flash target, `faust-kernels` codegen target).

## Adding a reverb

Follow the generic [add-an-algorithm](../engine-types/faust.md#setup-build-add) steps (drop `<name>.dsp`, append its `FAUST_KERNELS` spec, bump `ReverbEngine::kReverbCount`, register a bind table + concrete `ReverbVoice`). Then re-check `SRAM_EXEC`: with two reverbs it sits at ~92%, so a third Faust algorithm likely needs `OPT = -Os` on the reverb branch of the `Makefile`.

## Notes / TODO

- **CPU measured on hardware** (`make ENGINE=reverb METER=1`, `Meter::cpu` over the external USB CDC, non-blocking): one stereo hall **~18.6%**, DoubleMono two plates **~40.8% avg / ~61% peak**, single ~13-19%. The capped corner (two halls) measured ~70%/89% before the cap and can no longer occur. The reverb is **SDRAM-latency-bound** - cost scales super-linearly with concurrent heavy voices (~3.8x for two halls, from D-cache contention), which the host bench (~1.7x, no SDRAM model) badly under-predicted. To lift the cap later: move the plate's delay lines to internal AXI SRAM (de-contends a plate+heavy pairing - a hall's 937 KB can't fit internally), or shrink the hall's FDN *order* (per-sample cost is tap count, not delay length).

- **Hardware bring-up checks:** the mode-switch position -> voice-index order, and the route -> L/C/R LED order, are mapped to the natural 0/1/2; confirm they match the silkscreen on first flash (each is a one-line reorder if not).

- Licensing: the chosen demos are MIT; confirm the license of any `stdfaust.lib` functions a new reverb pulls before shipping it.
