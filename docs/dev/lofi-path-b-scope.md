# Path B scope: half-rate ("lo-fi") loop buffer

Goal: optionally store a deck's loop buffer at half rate (24 kHz-equivalent) to double record time (~84 s) at **zero extra SDRAM**, while the codec, clock, and output stay at 48 kHz. This document inventories every site that couples buffer frames to time / tempo / ticks, and — crucially — separates the sites that genuinely need a rate factor from the much larger set that *self-corrects*.

Scope was produced by a full read of `buffer`, `deck`, `generator`, `vox`, `window`, `driver`, `track`, and `config.h`. Line numbers drift; grep the named symbols.

## The analytical key: two domains, not one

Every quantity here lives in exactly one of two domains. Misclassifying them is what makes this look like a 50-site change when it is roughly a dozen.

- **Buffer-frame domain** - positions/sizes that index `_srcBuf*`. These scale with the buffer's storage rate. `rec_size()` is the unit here.

- **Output-sample domain** - counts driven by the 48 kHz audio callback. The grain iterator, envelope slopes, and record fades live here. These do **not** change with buffer rate.

The bridge between them is the playback **increment**: `Window::process` advances `_playhead += _increment` (buffer frames) once per output sample, then reads the buffer at `_playhead` (window.h:148). So a grain lasts `_size` *output samples* but spans `_size * _increment` *buffer frames*.

Consequence: **a half-rate buffer is fundamentally a pitch/increment change plus a few frame↔tempo constants — not a global "halve every ms constant" sweep.** Reading 24 kHz content at 48 kHz output for correct pitch means a baseline `increment = bufferRate / codecRate = 0.5`, and the existing `read_linear`/`read_cubic` interpolation already handles fractional positions.

## What self-corrects (NO change needed)

These were flagged by the survey but are rate-agnostic. List them so the work isn't spent here.

- **Fraction-of-buffer computations.** `_abs_start = start * rec_size()` (generator.cpp:~70) and `_abs_size = norm_size * rec_size()` (generator.cpp:109). A fraction of `rec_size()` yields a correct buffer-frame index at any storage rate.

- **Frame-ratio normalizations.** `playhead_at()/rec_size()` (deck.cpp:~414), `_max_loop_ticks * read_head()/rec_size()` (deck.cpp:~418), `write_head()/rec_size()` (UI), `norm_rec_size()` (buffer.h). Numerator and denominator are both buffer frames, so the ratio is invariant.

- **Interpolation math.** `read_linear`/`read_cubic`/`_read` (buffer.cpp) operate on fractional indices and `% _size`; they adapt automatically to a smaller `_size`.

- **Tick-domain math.** `_quantize_loop` (`norm_size * _max_loop_ticks`), loop-restart comparisons (`_through_loop_ticks >= _max_loop_ticks`). Pure ticks; unaffected once `_max_loop_ticks` is computed correctly (see below).

- **Envelope slope constants, as envelope timings.** `kWindowSlope` (20 ms), `kSliceSlope` (4 ms), `kRecordFade` (4 ms) are compared against the *output-sample* iterator (window.h:157,162; vox.cpp:90). A 4 ms fade should stay 4 ms regardless of buffer rate — so as envelope durations they do not change. (They re-enter the picture only via the `_size` coupling below.)

## What genuinely changes

### A. Buffer I/O — the new code (decimate on write, increment on read)

| Site | Domain | Change |
|------|--------|--------|
| `Buffer::write` (buffer.cpp:~169) | buffer-write | Decimate input 2:1 (store every Nth sample). Optional anti-alias LPF before decimation; omitting it *is* the lo-fi aliasing. |
| `Window::process` increment baseline (window.h:148) | bridge | Apply baseline `increment *= bufferRate/codecRate` (0.5) so pitch is preserved when reading a half-rate buffer at 48 kHz. The interpolation already exists. |

### B. Tempo<->frame factors (one family; unify under buffer rate)

All three are `bufferRate * K`. They currently hardcode the 48 kHz product. Express them in terms of the buffer rate and they fix consistently.

| Site | Code | Meaning | Half-rate |
|------|------|---------|-----------|
| deck.cpp:36 | `_start_step_kof = 30.f * p.sample_rate; // 1/8` | frames per 1/8-note unit; **already parameterized** | pass buffer rate as `p.sample_rate` |
| deck.cpp:214 | `bpm = 2880000 * (1 + round(frac*15)) / rec_size()` | `2880000 = 48000*60` (frames/min) | `bufferRate*60` |
| deck.cpp:331 | `ticks = rec_size() * _tempo / 720000.f` | `720000 = 48000*15`; frames->ticks @ 4PPQN | `bufferRate*15` |

`720000` is the linchpin: it sets `_max_loop_ticks` from the recorded frame count. Leave it at 48 k with a half-rate buffer and the loop length halves / clock sync breaks.

### C. Absolute frame-count caps (buffer-frame literals)

| Site | Code | Note |
|------|------|------|
| generator.cpp:222 | `_input_spread * std::min(rec_size(), 144000)` | `144000 = 3 s @ 48k`; a hard cap in buffer frames. Half-rate makes it 6 s unless scaled to `3 * bufferRate`. |

### D. Record-path fade (buffer-write domain)

| Site | Code | Note |
|------|------|------|
| buffer.cpp:25 | `_cut_switch.init(48000)` | overdub cut fade; if the record path decimates, this and `kRecordFade` count *written* (half-rate) frames -> 4 ms becomes 8 ms. Decide whether the record fade is specified in ms (use buffer rate) or output samples. |

### E. The central design decision: `_size` <-> `rec_size()` <-> `increment`

`_abs_size` sets the grain `_size` from `norm_size * rec_size()` (buffer frames), but `_size` is consumed as an *output-sample* iterator count, and the buffer span actually covered is `_size * increment`. At full rate (increment ~1) these coincide. With a half-rate buffer and baseline increment 0.5, a grain set to cover buffer fraction `f` would either play the wrong span or the wrong duration unless `_size` is scaled by `1/increment_baseline` (i.e. doubled). This is the one place that needs deliberate design, not a mechanical substitution. It also governs how `kSliceMinSize`/`kDefaultWindowSize` (output-domain floors) interact with a buffer-frame-derived `size` in `std::max(size, kSliceMinSize)` (generator.cpp:110).

## Open questions / ambiguities to resolve before coding

1. **Spread-mode window range** (vox.cpp:276): `_window_size = 2880 + norm*21120 (max 500ms @48k)`. `_window_size` feeds `_size` (output-domain) — so likely *stays* at 48 k as a duration, but verify it isn't also used as a buffer span. Flagged, not settled.

2. **Record fade domain** (D above): ms-specified or output-sample-specified?

3. **WAV persistence**: a half-rate buffer saved to SD should declare its real rate in the `WavHeader` (now correctly `uint32_t`), or be upsampled on save. Today the header hardcodes 48000.

4. **Anti-aliasing**: decimating 48->24k folds >12 kHz down. Feature for lo-fi; if cleaner is wanted, the existing `Biquad` before decimation. (Note: `fx.reduce`'s `Decimator` is an *effect on the signal*, not buffer storage — do not reuse it for this.)

5. **24 kHz is not a codec rate** (libDaisy SAI enum is 8/16/32/48/96). Path B keeps the codec at 48 k and only the *buffer* is half-rate, which sidesteps this — but it is the reason Path A (lower the codec) would have to settle for 32 kHz.

## Recommended approach

1. Introduce one per-deck `bufferRateDiv ∈ {1,2}` (and derived `bufferRate = codecRate/div`). Thread it to: the three factors in B (ideally by deriving them from `bufferRate` rather than literals), the cap in C, the increment baseline in A, and the `_size` scaling in E.

2. Decimate in `Buffer::write`; rely on existing interpolation for read.

3. Decide D/1/2 explicitly and comment the chosen domain at each constant.

4. **Test first.** The frame<->tempo conversions (B) and the `_size`/increment relationship (E) are pure arithmetic and are exactly what the host harness (`test/`) is for. Add characterization tests for `tempo_to_fit`/`_set_grid` math at div=1 and div=2 *before* changing behavior, so the invariant "same wall-clock loop length and pitch at either div" is pinned. `Deck` pulls in hardware/graph deps, so this likely needs extracting the pure frame<->tempo helpers (the `720000`/`2880000`/`30*sr` math) into a testable free function or small struct first — a worthwhile refactor in its own right.

## Bottom line

The feature is tractable and the SDRAM math is free. The work is **not** "halve ~50 constants"; it is: one decimate-on-write, one increment baseline, three tempo<->frame factors (one already parameterized), one absolute cap, one record-fade decision, and one genuine design call on `_size`/increment. The biggest risk is E and the loop-sync factor in B; both are pure arithmetic and should be pinned with host tests before touching playback.
