# Dev notes — dual virtual RadioMusic (`radio` engine)

Implementation/bring-up notes for `ENGINE=radio`. The user-facing reference (controls, SD-card layout, file formats, build commands) is [`docs/engines/radio.md`](../engines/radio.md); this file holds the internals, the file map, the risks, and the bug writeups.

## TL;DR — where we are

- **Builds + host-tested; field-verified for the stutter bugs.** `make -j8 ENGINE=radio` links at ~83% SRAM_EXEC (no record path, no Faust kernels). `make -C host test` includes `test-radio` (raw + wav codec, station/bank quantization, the playhead modulo, varispeed, static, RESET-spam, boundary chatter, rate.txt, per-file wav rate) - all green.

- The pot/pad/LED/CV paths have no host harness; behaviour was confirmed on the device, including the macOS-AppleDouble stutter fix (the original symptom report and resolution are below).

## Reused platform stack

Built on the same SD-streaming service as the [tape](../engines/tape.md) engine: the platform `StreamDeck` (lock-free per-deck SDRAM ring + a main-loop FatFs pump), gated by `SPK_USE_STREAM`, injected via `EngineContext::stream`. The radio adds only an int16/headerless codec, a directory scan, and the free-running playhead; it reuses the 1 MB/deck rings, `PlayStream`/`SpscRing`, and the `app.cpp` construct/inject/pump wiring unchanged (all `SPK_USE_STREAM`-guarded, so non-streaming engines stay byte-identical).

## Free-running playhead — mechanism

A single monotonic `uint64_t` **frame clock** advances by the block size each `process()`. Tuning to a station opens its file and seeks to `(clock + START) mod station_length` before streaming forward, so each station lands at its "live" position. The raw format makes this cheap: a `.raw` station's length is `filesize / 2` (a pure `f_stat`, no header) and the seek is `f_lseek(frame * 2)`. The file open + seek run off the audio path in `prepare()` (main loop); the audio ISR only ever drains the per-deck ring.

## Audio path (ISR) vs control (main loop)

- **`process()` (ISR):** per deck, pull int16 bytes from the play ring, convert (`int16 * 1/32768`), run a 2-frame linear-interp varispeed resampler (`step = SIZE_varispeed * deck_rate_ratio`), crossfade the static burst, mix to the soft-limited stereo bus. No FatFs, no allocation.

- **`prepare()` (main loop):** rescans a bank's directory, settles the desired station, and (re)opens it at its free-running offset via `StreamDeck::start_play_raw` / `start_play_wav`. Reads `rate.txt` once at boot.

## Formats — one reader, two entry points

`RawStreamReader` (`src/memory/raw_stream.h`) is the single int16-mono streaming reader:

- `begin(f, filesize)` — headerless `.raw`: body = whole file, `_data_start = 0`.

- `begin_wav(f, filesize, out_rate)` — 16-bit-mono PCM `.wav`: a spec-compliant chunk walk validates `fmt==1`/`bits==16`/`channels==1`, sets `_data_start` to the data-chunk body and `_data_size` to its length (clamped to the file for a truncated take), and reports the header sample rate.

- `seek_to_frame`/`rewind` are body-relative (`_data_start + frame*2`), so the playhead jump and looping work identically for both layouts.

Per-file rate: a `.wav` carries its own rate, so `_do_open` sets `_deck_rate_ratio[deck] = wav_rate/48000` (a `.raw` uses the global `rate.txt` ratio). That is why a 44.1k `.wav` plays at correct pitch with no `rate.txt`. Indexing a `.wav` costs one `f_open`+header-parse per file in `scan_bank` (a `.raw` is a pure `f_stat`); playback cost is identical.

## Anti-stutter guards (re-open = ring flush)

A (re)open flushes and re-seeds the per-deck ring, so anything that triggers re-opens at audio rate replays the same short grain = a stutter (pitched by SIZE, since it goes through the resampler). Three guards, all in `radio_engine.cpp`:

- **Station-select hysteresis** (`kStationHyst`, 0.25) — a deadband in `_quant_station` so CV/pot noise near a station boundary can't flip the target (oscillation must swing > 2× the margin).

- **Settle-every-prepare** — the settle timer (`kSettleMs`, 180 ms) is reset whenever the target moves *at all* (including back to the open station), so a target oscillating between two stations (parking PITCH on a boundary) **never settles**; the engine holds the last clean station instead of re-opening ~5×/s. Earlier the timer was only updated inside the switch branch, which let `_pending` stick and fire every 180 ms — that was the "boundary stutter" report.

- **RESET** (`on_play_pad` / `on_gate_trigger`) is **debounced** (`kDebounceMs`) and a **no-op on a live deck** (only re-starts a *silent* deck). Re-seeking a station you are already tuned to is redundant for a free-running radio, and honoring a repeated/floating-gate trigger would flush the ring continuously.

## macOS AppleDouble filter (the original "constant stutter")

**Symptom:** on an original RadioMusic card copied via Finder, tuning alternated clean station / fast stutter / clean station, the stutter pitched by SIZE. **Cause:** Finder writes a hidden AppleDouble companion `._NAME.raw` next to every `NAME.raw` (~4 KB of metadata, not audio), plus `.DS_Store`. The companion ends in `.raw`, so `scan_bank` indexed it as a bogus tiny "station" that loops a sliver of garbage — and since there's one per real file, they interleave. **Fix:** `scan_bank` drops them three
ways — the FAT hidden/system attribute (`AM_HID|AM_SYS`; macOS sets it on dot-prefixed files), a
leading-dot name, and `kMinStationBytes` (32 KB; no real station is a fraction of a second). The converter skips dotfiles on the source side too.

## Files

New:

- `src/memory/raw_stream.h` — `RawStreamReader : IChunkSource` (int16 body; headerless `begin` or WAV-header `begin_wav`; body-relative `seek_to_frame`/`rewind`).

- `src/engine/radio/radio_engine.{h,cpp}` — the engine.

- `host/test_radio.cpp` — host suite (wired into `make -C host test`).

- `scripts/convert_radio_audio.py` — source → `.raw`/`.wav` converter (`mirror`/`from-dir`/`convert`).

Edited (all `SPK_USE_STREAM`-guarded, byte-identical for non-streaming engines):

- `src/engine/istreamdeck.h` — `start_play_raw`/`start_play_wav` (seek-on-open), `frames_of`, `scan_bank` (dotfile filter, indexes `.raw`+`.wav`), `BankEntry` (name/frames/rate/is_wav), `read_text` (rate.txt).

- `src/hw/stream_deck.{h,cpp}` — the new calls + a `RawStreamReader` per deck; the macOS dotfile filter + `.wav` header probe in `scan_bank`.

- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` — register `radio`.

## Risks / watch-items

- **Dropouts** when both decks sweep stations at once (each switch is an f_open + seek + ring refill). Two int16 mono streams are ~half the tape engine's proven float bandwidth, so steady-state is expected to hold; sustained simultaneous sweeping is the unmeasured case. (Fragmentation on an old card was an early red herring for the stutter — the real cause was the AppleDouble files above.)

- **Mount delay** — the card mounts ~1 s after boot; the engine retries the bank scan for `kBootScanMs` (5 s) so stations appear once mounted rather than showing dead air.

- **First-cut constants** (`kSettleMs`, `kStaticDec`, `kNoiseLevel`, `kStaticThresh`, `kStationHyst`) are tunable by ear on hardware.

- **START is applied on a switch**, not continuously, to keep SD I/O bounded. Continuous scrub would need a lighter in-file seek (an `f_lseek` + ring flush without a re-open).

## What's left

`.wav` support is done. Possible future work:

- **Per-station fade/declick** at the loop seam for short stations.

- **A lighter in-file seek** so START can scrub continuously.

- **A file browser / more banks** beyond the fixed 0..15 numbered folders.
