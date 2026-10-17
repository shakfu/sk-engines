# kuskus - Mutable Instruments Beads on the Spotykach

Design and implementation plan for `ENGINE=kuskus`, a port of the [Beads](https://github.com/pichenettes/eurorack/tree/master/beads) granular processor. kuskus takes the slot of `graincloud`, removed on 2026-10-16.

**Status: phases 0-2 and most of 4 implemented (2026-10-17); not yet run on hardware.** This document is the plan; [Implementation status](#implementation-status) records what shipped and where it departs. Facts about Beads come from a read of `pichenettes/eurorack@f4cfa76` (2026-10-12) with stmlib at `e3bd7c9`. Claims marked *(inference)* are untested.

## Why Beads

- Beads is a full granular instrument: 32 grains, a delay mode, four "media" (tape/digital characters), a reverb, and a wavetable synth for when nothing is patched. graincloud had a grain cloud and nothing around it.

- It is MIT-licensed. Every `.h`/`.cc` carries the MIT header (Emilie Gillet, 2014-2021; `drivers/` are Fred's Lab, also MIT). The code can be vendored like reso (Rings) and mosc (Plaits).

- It targets an STM32H7, the same family as the Daisy Seed's H750, so float math and cache behaviour carry over.

- `beads/dsp/` is hardware-free. Its only `TEST` use is one `printf` in `wavetable_granular_player.h`.

## What Beads is, in code terms

| Unit | Role |
|---|---|
| `beads::Processor` (`dsp/processor.h/.cc`) | the whole audio-input chain |
| `beads::Synth` (`dsp/synth.h/.cc`) | the no-input wavetable mode |
| `GranularProcessor<res, len, ratio>` | one of 4 template instances, one per media |
| `DelayGranularPlayer` | 32 grains, Hermite reads from the delay line |
| `MultitapDelay` + `DelayController` | the delay mode (8 taps) |
| `TriggerGenerator` | the seed clock: regular, random, divided or audio-rate |
| `Reverb` | Griesinger/Dattorro on a `FxEngine<32768, FORMAT_16_BIT>` |

`Processor::Process(SeedButtonState, const GateFlags* seed, const bool* freeze, const ModulatedParameter& density, const Parameters&, const MixParameters&, const FloatFrame* in, FloatFrame* out, size_t size)`. Frames are float stereo, nominally +/-1.

Signal order inside `Process`:

1. Mode select: delay mode when SIZE > 0.99 (0.98 with hysteresis), with a 1.5 s transition.

2. Triggers: `TriggerGenerator` (granular) or `DelayController` (delay).

3. The media's `GranularProcessor`: optional down-sampling, feedback mix through a limiter and soft clip, `DelayLine::Write` (skipped while frozen), the grain player or multitap, DC blocker and low-pass into the feedback path, optional up-sampling.

4. Dry path quantised to the media (12-bit truncation or a mu-law round trip).

5. Dry/wet, then wow and flutter (Dirty Tape only), then a pre-reverb limiter, the reverb, and an output limiter.

Measured on `arm-none-eabi-g++ -mcpu=cortex-m7 -O2`:

| Object | `.text` / data |
|---|---|
| `processor.cc` (4 template instances) | 62.1 KB |
| `synth.cc` | 12.8 KB |
| `delay_controller.cc` | 7.1 KB |
| `trigger_generator.cc` | 3.1 KB |
| stmlib `units.cc` | 2.1 KB |
| `resources.cc` | 105 KB rodata (97.5 KB of it is `wav_integrated_waves`, used only by `Synth`) |
| `sizeof(Processor)` / `sizeof(Synth)` | 25,824 B / 4,416 B |

## Platform fit

### Sample rate: the platform is fixed at 48 kHz

Beads changes the codec rate per media. The platform cannot.

| Media | Beads codec / internal rate | Bit depth | Stereo buffer | On a 48 kHz host |
|---|---|---|---|---|
| Digital | 48k / 48k | 16-bit | 4 s | as is |
| Clean Tape | 48k / 24k (ratio-2 SRC) | 12-bit, soft-clip AGC | 10.7 s | as is |
| Dirty Tape | 24k / 24k | 8-bit mu-law | 16 s | `#undef ENABLE_HARDWARE_24K` selects the author's own 48k path (ratio-2 SRC, `set_lp(0.6)`). The wow/flutter depth in samples is not rescaled, so it is half as deep in time, and the reverb runs at 48k instead of 24k. |
| Vintage Digital | 32k / 32k | 12-bit | 8 s | no integer ratio from 48k |

Vintage Digital options, in order of preference:

1. **Run it at 24k with ratio 2.** It reuses the (2,48) filter and loses some band-limit character. Smallest change.

2. Wrap the whole `Processor` in a 2:3 rational resampler. This needs new FIR filters (`tools/generate_src_filters.py`); the existing 3x filters cut off too low.

3. Drop the mode.

Decision for phase 1: Digital only. Phase 2 adds Clean and Dirty Tape on the 48k path and option 1 for Vintage.

### Block size and control rate

Beads calls `Process` at 4 kHz: `sr / kBlockRate`, `kBlockRate = 4000`, so 12 frames at 48k. Its parameter interpolators, CV one-poles, random-walk step, wow/flutter LFO and the 2.5 ms seed delay all assume that rate. The platform block is 96 frames, so `process()` calls `Processor::Process` 8 times on 12-frame sub-blocks. That keeps every control constant correct, and 12 is even, as the ratio-2 down-samplers require.

### Memory

| Buffer | Beads location | kuskus location |
|---|---|---|
| `delay_buffer_1` 512 KiB, `delay_buffer_2` 256 KiB | `.ramd1`, `.ramd2` | SDRAM engine arena |
| `reverb_buffer` 64 KiB (`uint16_t[32768]`) | `.ramd3` | SDRAM engine arena |
| `Processor` + `Synth` (~30 KB) | DTCM `.bss` | SDRAM arena, placement-new'd (the reso/mosc pattern) |

- `DelayLine` addresses three banks of 262,144 B through `items_[bank]`, so the banks need not be contiguous. One 768 KB arena block works.

- `Processor` and `Synth` both `Init` on the same reverb buffer, so only one may run at a time. kuskus selects one per block and resets the other's state on a switch.

- `Configure()` zero-fills all 768 KB on every media or mono change. Beads mutes for 1000 blocks around it. kuskus runs `Configure` from `prepare()` (main loop), with output muted until it returns.

- There is no malloc, new, RTTI or exceptions. There are VLAs on the stack in `Process`/`Render`, which GCC accepts; the vendored tree builds as `-isystem`, so no warning reaches `WERROR`.

### Code size

`.rodata` links into SRAM_EXEC with `.text` (`linker/alt_sram.lds`), so the tables count against the 260 KB region. The platform alone is 156,000 B (`ENGINE=passthrough`).

| Build | Estimate | Fits SRAM_EXEC? |
|---|---|---|
| Phase 1: `Processor`, Digital only, no `Synth` | 156 KB + ~80 KB = ~236 KB (91%) | yes, at -O2; -Os gives margin |
| Phase 1 with `TERMINAL=1` | ~265 KB (terminal adds ~29 KB, measured on granular) | no |
| Full: all 4 media + `Synth` + wavetables | ~350 KB | no |

The full engine therefore needs one of:

1. **`BOOT_QSPI`, as mosc does** (`APP_TYPE=BOOT_QSPI LDSCRIPT=linker/alt_qspi.lds`, the csound VTOR inject, keeping the engine arena). Known to work. Code then executes from QSPI through the I-cache.

2. Keep code in SRAM and move `wav_integrated_waves` into a `.qspiflash_data` section. The SRAM linker script defines that section at `0x90000000`, but whether the Daisy bootloader programs it for a `BOOT_SRAM` app is unverified *(inference)*.

Plan: phase 1 builds `BOOT_SRAM` without the terminal. Phase 3 moves to `BOOT_QSPI` when `Synth` lands, the same decision mosc made.

### CPU *(inference, the main risk)*

Beads reads its grains from internal SRAM. Here the delay line is in SDRAM. That is 32 grains x 4 Hermite taps x 2 channels of scattered reads per sample at 48 kHz. SDRAM latency through the D-cache may dominate. granular already reads loops from SDRAM, but with far fewer voices.

Mitigations, if the meter shows a problem:

- Cap the grain count below 32 (a constant in `DelayGranularPlayer`).

- Put the Digital-mode buffer, or part of it, in the 252 KB `SRAM` region (25% used by granular). That only fits a shortened buffer: 4 s of 16-bit stereo is 768 KB.

- Run `-O2` for `processor.cc` only and `-Os` elsewhere.

Profile with the `SPK_CPU_METER` build on hardware before any UI work.

## Engine design

### Structure

- `src/engine/kuskus/kuskus_engine.{h,cpp}`: `KuskusEngine : IEngine` with PIMPL (`struct Impl* _p`). stmlib declares a global `namespace impl` that collides with `app.cpp`'s `static AppImpl impl;`, so no Beads or stmlib header may reach `kuskus_engine.h`. reso and mosc solve it the same way.

- `src/engine/kuskus/thirdparty/beads/`: `dsp/` and `resources.{h,cc}` only, trimmed to the compile closure from the compiler's `.d` output, as reso is. Not vendored: `beads.cc`, `ui.*`, `cv_reader.*`, `gain_control.*`, `normalization_detector.*`, `background_buffer_backup.*`, `drivers/`, `bootloader/`, `hardware_design/`.

- Shared stmlib (`src/engine/common/thirdparty/stmlib`): it is byte-identical to Beads' copy except for three missing files, `dsp/sample_rate_converter.h`, `dsp/hysteresis_filter.h` and `utils/gate_flags.h`. Add them; reso and mosc are unaffected.

- Capabilities: `CapDualDeck | CapOwnDisplay | CapTransport`. kuskus does not record to card, so it has no `CapTapeStorage` and no card bank.

### One stereo instance, both decks as one panel

Beads is a single stereo processor with 12 knobs. Two instances would double the CPU, which is already the main risk. kuskus runs one instance and spreads Beads' panel across both decks.

| Control | Deck A | Deck B |
|---|---|---|
| PITCH | PITCH | PITCH attenuverter |
| POS | TIME | TIME attenuverter |
| SIZE | SIZE (top of travel = delay mode) | SIZE attenuverter |
| ENV | SHAPE | SHAPE attenuverter |
| MODFREQ | DENSITY | input gain (Beads' MEDIA+FEEDBACK gesture) |
| MOD_AMT | FEEDBACK | REVERB |
| SOS (Mix) | dry/wet | output level |
| Crossfader | dry/wet performance control (overrides SOS A while moving) | |

| Gesture | kuskus | Beads equivalent |
|---|---|---|
| Play A | SEED: press for a manual gate, hold 4 s to latch | SEED |
| Play B | FREEZE toggle | FREEZE |
| Deck A mode switch (Reel/Slice/Drift) | media: Clean Tape / Digital / Dirty Tape | MEDIA |
| Alt + Play A | latch or unlatch SEED | SEED long press |
| Route switch | Stereo / Mono (sum to one grain stream) / gate-out mode | mono input, ASSIGN+SEED |
| Gate in A | seed input | SEED jack |
| Gate in B | freeze gate | FREEZE jack |
| V/Oct A | PITCH CV | PITCH CV |
| SIZE/POS CV A, B | SIZE CV, TIME CV | SIZE, TIME CV |
| MIX CV A, B | SHAPE CV, DENSITY CV | SHAPE, DENSITY CV |
| Crossfade CV | dry/wet | assign CV (dry/wet destination) |
| MIDI note | a seed trigger, pitch offset from note 60 | none |

This is a proposal. The deck B column is the one most worth reconsidering: attenuverters on a whole deck of knobs is honest to Beads but spends prime controls on modulation depth.

### No jack detection

Beads sends a pseudo-random bit sequence down its normalled jack lines to know what is patched (`NormalizationDetector`). That feeds `has_cv`, mono input, AGC and the automatic synth mode. Spotykach has no equivalent in `src/hw` or `src/ui`. kuskus therefore:

- treats every CV as unpatched (`has_cv = false`), so the attenuverters always set grain randomness and CV is added to the knob base;

- takes mono/stereo from the route switch, not from the inputs;

- enters synth mode by a gesture (Alt + deck B mode switch), not by 10 s of silence.

### Clock

Beads latches SEED and derives a divided or multiplied clock from DENSITY when a clock reaches the SEED jack. kuskus subscribes to the platform transport (`CapTransport`). While SEED is latched, transport ticks become the seed gate stream, so TS4, MIDI clock and the internal tempo drive the grain clock exactly as a patched clock would. The 2.5 ms seed delay is kept.

### Gain staging

- Beads' AGC (`GainControl`) is tied to its codec's int32 frames and is replaced by a fixed input gain on MODFREQ B.

- The gate-out mode writes -0.8 on R, which assumes Beads' inverting output stage. On this device that should be +0.8 *(inference; check the Spotykach output polarity)*.

- Synth mode's `post_gain_dry 0.5` is set for 10 Vpp; expect to trim it by ear.

### Display (`CapOwnDisplay`)

Using the `indicators.h` helpers:

- Ring A: the delay line, with the write head and up to 32 grain read heads as points, if `DelayGranularPlayer` exposes them; otherwise the density clock as a pulse.

- Ring B: input VU (Beads' VU meter), and the freeze state as a frozen-colour ring.

- Mode LED: media colour from Beads' palette (`ui.cc:139`).

- Play A: seed pulse (a triangle when latched). Play B: freeze.

### Not ported

- Freeze-buffer backup to internal flash (`BackgroundBufferBackup`, 768 KB at `0x08100000`). On the Daisy that would be QSPI, where an erase stalls code executing from QSPI, or SD. Revisit after phase 4.

- CV calibration (ASSIGN+MEDIA): the platform owns calibration.

- `WasteCPUCycles`: the main-loop dummy render that keeps current draw constant *(inference about its purpose)*.

- `ENABLE_ADVANCED_FEATURES` (mono grains, buffer trim). Upstream's `Processor::Trim()` falls through every `case` with no `break`.

## Implementation phases

| Phase | Scope | Exit criterion |
|---|---|---|
| 0 | Vendor `dsp/` + resources, add the 3 stmlib headers, host-build the DSP. | `host/test_kuskus.cpp`: dry passes at dry/wet 0; grains produce output; freeze holds the buffer; delay mode at SIZE 1; finite output in every media. |
| 1 | `KuskusEngine`, Digital media only, the control map, `BOOT_SRAM` build. | `make WERROR=1 ENGINE=kuskus` links under 260 KB; CPU meter on hardware under budget with 32 grains. |
| 2 | Clean and Dirty Tape on the 48k path; Vintage via 24k ratio 2; mute + `Configure` on a media change. | Host test per media; on hardware, no click on media switch. |
| 3 | `Synth` mode and wavetables; move to `BOOT_QSPI`. | Synth renders on the host; QSPI image boots. |
| 4 | Transport-driven seed clock, MIDI, display, `param_label()` table, docs, release list. | Hardware pass, then add to `DEFAULT_ENGINES`. |

Registration touches, as for any engine: `engine_select.h`, the root `Makefile` branch and `engine-kuskus` target, `CMakeLists.txt`, `host/Makefile`, the CI matrix, `docs/engines/kuskus.md`, and the README engine table.

## Open decisions

1. The deck B mapping: attenuverters (faithful) or a second set of performance controls with randomness on Alt layers.

2. Vintage Digital: 24k ratio 2, a rational resampler, or drop it.

3. Whether kuskus should ever save the frozen buffer (SD rather than flash).

4. `BOOT_QSPI` from phase 1, if the CPU profile shows `-Os` is needed anyway, which would remove the SRAM budget question.

5. Can kuskus load a file instead of acting only as an effect on the live input? Options, untested:
   - Stream a WAV from SD into Beads' input through `IStreamDeck` (`start_play_wav`, `play_consume`; `pstretch` uses it), then freeze. Beads converts to the media's format on record, so no vendored patch is needed. The file passes through the input path at 1x, so loading takes as long as the file plays.
   - Write samples straight into the delay line, then freeze. Loading is near-instant, but this needs a second vendored patch and per-media encoding (16-bit, 12-bit, 8-bit mu-law).
   - Either way the buffer caps the file length: 4 s for Digital, 16 s for Dirty Tape. Which pad or switch selects the file, and where the bank lives on SD, is not decided.

## Implementation status

Done: phases 0, 1, 2 (all four media), and phase 4's MIDI, display, `param_label()`, docs and CI entries. `make test` passes.

Measured, `make WERROR=1 ENGINE=kuskus` at -O2, clean build:

| Build | SRAM_EXEC |
|---|---|
| plain | 227,180 B (85.3%) |
| `METER=1` | 238,824 B (89.7%) |
| `TERMINAL=1` | 256,440 B (96.3%) |
| `TERMINAL=1 OSC=1` | 265,648 B (99.8%) |

All four media fit in `BOOT_SRAM`, so the Code size section's phase-1 cut to Digital was not needed. `TERMINAL=1 OSC=1` leaves 592 B.

Departures from the plan above:

- **Vintage Digital: option 1** (24 kHz, ratio 2). The patch is `BEADS_FIXED_48K` in `processor.{h,cc}`, 25 lines. Its selector is the deck B mode switch at Drift, because deck A's three positions hold the other three media.

- **SEED latch is Alt + Play A, not a 4 s hold.** The platform sends `on_play_pad` on touch only, with no release. Flux A, whose release the platform does report, acts as the held SEED button.

- **Clock-follow is a toggle on Rev A**, not automatic while latched. The internal transport always runs, so following it unconditionally would replace Beads' free-running latched mode with the divider for good.

- **"Patched" SEED** means an edge within the last 4 s (the divider's resync window). With SEED latched, one stray gate holds the free-running clock for 4 s.

- **Media colour is on ring A**, not the mode LEDs: the L/C/R LEDs sit under the routing switch and show the route.

- **Ring A shows output level and TIME**, not grain heads. Grain state is private in three nested classes; exposing it needs a second vendored patch.

- **Gate-out**: R is +0.8 per grain, and each rising edge also pulses gate out A.

Open:

- Phase 3 (Synth + `BOOT_QSPI`). The 97.5 KB `wav_integrated_waves` table alone does not fit beside the processor in SRAM_EXEC.
- CPU on hardware (`METER=1`), the main risk named above. Media switch clicks on hardware.
- Output polarity of the gate-out mode.
- `DEFAULT_ENGINES` in `scripts/build_release.py` waits on the hardware pass.

## Hardware validation checklist

First flash: 2026-10-17, plain build (227,180 B). Tick each item on hardware; note failures with the build flags used.

Boot:
- [ ] Boots without hang. SEED latched; Play A breathes in the media colour.
- [ ] With input, grains start once the knobs are touched (DENSITY 0.3, dry/wet 0.5).

Deck A knobs:
- [ ] SOS A fully left: clean input at about 0.75x. Ring B tracks input level.
- [ ] MODFREQ A (DENSITY): left of noon regular grains, faster further left; noon band silent while latched; right of noon random grains, denser further right.
- [ ] POS sets how far back grains start; ring A dot follows.
- [ ] SIZE sets grain length; bottom third reverses grains; top of travel enters delay mode within about 1.5 s.
- [ ] ENV changes the grain envelope.
- [ ] PITCH detents at 0, 1, 3, 4, 7, 12, 24 semitones.

Deck B:
- [ ] PITCH/POS/SIZE/ENV B at noon add nothing; off noon they add per-grain randomness.
- [ ] MOD_AMT A = feedback, MOD_AMT B = reverb, MODFREQ B = input gain, SOS B = output level (unity at noon).

Pads:
- [ ] Play A: SEED tap, unlatches. Alt + Play A: latches again.
- [ ] Flux A held: grains run while held.
- [ ] Play B: freeze; ring B full cyan; grains keep playing after input stops.
- [ ] Rev A: clock-follow toggles (green); grains follow the transport quarter notes.

Switches:
- [ ] Mode A Slice / Reel / Drift = Digital / Clean Tape / Dirty Tape; ring A colour changes; buffer clears.
- [ ] Mode B Drift = Vintage Digital, overriding Mode A.
- [ ] No click on any media switch (expect a 96-sample fade).
- [ ] Routing: centre stereo, left mono (longer buffer), right gate-out.
- [ ] Gate-out: L = mono mix, R = +0.8 gate per grain, gate out A pulses. Record the R polarity seen at the jack.

CV, gates, MIDI:
- [ ] V/Oct A tracks pitch in semitones.
- [ ] SIZE/POS CV A/B, MIX CV A/B, crossfade CV move their targets. Note any range that feels badly scaled.
- [ ] Gate in A seeds; gate in B toggles freeze.
- [ ] MIDI note seeds a grain at pitch note - 60.

CPU (main risk):
- [ ] No dropout or crackle at maximum DENSITY, in every media, with feedback and reverb up.
- [ ] `make engine-kuskus METER=1`: record the peak load.
- [ ] `TERMINAL=1` build boots and runs the same pass.

After a full pass: add kuskus to `DEFAULT_ENGINES` in `scripts/build_release.py`.

## References

- Beads source: `beads/dsp/processor.{h,cc}`, `granular_processor.h`, `delay_line.h`, `trigger_generator.cc`, `randomized_parameter.h`, `beads.cc` (`ConfigureAndStartCodec`, the static buffers), `ui.cc`.

- sk precedents: [`reso-impl.md`](reso-impl.md) (PIMPL, arena placement, trimmed vendoring), [`mosc-impl.md`](mosc-impl.md) (shared stmlib, `BOOT_QSPI` with the arena kept).
