# Changelog

All notable changes to the Spotykach firmware are recorded here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project does not yet expose a version constant in the firmware; entries are grouped
by the Git tag / release they ship in. The latest released baseline is 1.0.2.

## [Unreleased]

### Fixed

- **Granular slice fade-out at high pitch/speed (V6).** `Vox::_set_decay_start()` assigned
  the grain decay-start position only when the fade region was shorter than the slice; in
  the high-increment, small-slice corner (e.g. playhead increment 8.0 with the minimum
  slice of 1344 samples, where the fade region 1536 exceeds the slice) it left a stale,
  history-dependent value. That produced an inconsistent fade trigger - grains cut short,
  or ending without their ~4 ms Hann fade and clicking at the boundary. The function now
  always assigns `_decay_start = std::max(0.f, _size - decay_length)`, so when the fade is
  longer than the slice the whole grain becomes the fade. Deterministic and bounded.
  Consistent with the manual's "slice-mode overdub may glitch when synced to MIDI clock."
  This is the only change in this release that alters runtime audio behaviour; validate by
  ear at maximum pitch on the smallest slice. (`src/core/vox.cpp`)

- **Stack buffer overflows in SD-card path building.** Five `char` buffers used to build
  tape/file paths were each one byte too small for their string plus the null terminator
  (`tape_dir_path[4]` for `"SK/G"`, three `audio_path[11]` for `"/SK/G/1.WAV"`, two
  `name[5]` for `"1.WAV"`). The `sprintf` calls wrote the terminating null one byte past the
  end - benign-by-luck via stack padding, but undefined behaviour. Buffers resized to fit;
  surfaced while replacing `sprintf` (see Changed). (`src/hw/card.cpp`, `src/memory/storage.cpp`)

### Changed

- **SD path building uses manual string joins instead of `sprintf`.** All five `sprintf`
  calls (plain `%s` concatenations) were replaced with `strcpy`/`strcat`. This lets the
  linker drop the `sprintf`/`fprintf` objects, recovering ~648 B of the nearly-full
  `SRAM_EXEC` region (99.05% -> 98.71% used). Note: the core `vfprintf`/`dtoa` machinery
  (~2.3 KB) remains, pulled in by libDaisy's `logger.o` via `vsnprintf` - reclaiming it
  would require a libDaisy-side change. (`src/hw/card.cpp`, `src/memory/storage.cpp`)

These are correctness/intent fixes that do not change current runtime behaviour; they
harden the code against future changes.

- **`Config::_is_loaded` default-initialised (V1).** Added `= false` to the flag backing
  `Config::is_loaded()`. In current usage `Config` exists only as the zero-initialised
  static singleton (`Config::dynamic()`), so the flag was already reliably `false`; this
  guards against any future non-static instantiation reading an indeterminate value.
  (`src/config.h`)

- **Tape navigation uses `kStorageTapeCount` (V2).** `DeckStorage::next_tape`/
  `previous_tape` and the `kTapeName[]` array were sized/wrapped on `kStorageSlotCount`.
  Both constants are 6 today, so the generated code is unchanged; the fix uses the
  semantically correct constant so tape handling stays correct if tape count ever diverges
  from slot count. (`src/memory/storage.cpp`)

### Added

- **In-repo developer documentation** under `docs/`: `architecture.md`, `source-guide.md`,
  a firmware-tracked `manual.md`, and `review-260529.md` (a code/architecture review with
  verified and re-validated findings).

- **Host-only test harness** under `test/` (`make -C test test`). Compiles the pure-logic
  timing classes natively (no ARM toolchain, no hardware) and asserts their invariants. It
  does not build or affect the firmware. Initial coverage locks the two findings that
  required manual tracing: `Divider` triplet timing does not drift over long runs (S1), and
  `SynClock` emits exactly `_ticks_per_clock` internal ticks per external clock pulse for
  24->48 and 12->48 PPQN, including when the internal timer is over-driven (S3). Also covers
  the `Config` text parser (S5): valid parse, the `is_loaded()` default (V1), and the
  documented brittleness (mid-token whitespace stripping, unknown/overlong names ignored);
  the `wav.h` header builder/parser (canonical 44-byte layout, build->serialize->parse
  round-trip, rejection of short/non-RIFF input); and `SpeedMap` (semitone->speed table:
  octave/fifth anchors, end clamping, monotonicity). A `test/stubs/daisysp.h` shim supplies
  the few DaisySP symbols referenced by otherwise-pure headers (`fmap`, `fclamp`); and
  `Follower` (mean-square envelope follower: reset, steady-state convergence, output
  clamping, amp scaling, attack-faster-than-release); the `sample16` float<->int16
  conversion (round-trip, clamping, quantization step); `convert_pcm_block` (the load
  shim: f32<->i16 block conversion, clamping, and float->i16->float round-trip within a
  quantization step); and `PcmLoader` - a simulation of the SD audio-load accounting
  (frame counts, truncation to capacity, termination, contiguous placement) across every
  width combo and many chunk sizes, exercising the exact code card.cpp runs. 116 checks
  across 9 classes.

- **`LOFI_INT16` 16-bit loop buffer (default off, opt-in via `make LOFI_INT16=1`).** Stores
  the loop buffer as 16-bit PCM instead of 32-bit float, **doubling record time to 84 s in
  the same SDRAM** (verified: SDRAM footprint identical at both depths; `SRAM_EXEC` +304 B
  for the conversion code). Pieces:
  - `src/core/sample16.h` - pure, host-tested float<->int16 convert with hard clip.
  - `Buffer::Frame` becomes `int16_t` under the flag (`Buffer::Sample` alias); conversion
    is confined to the two element-wise sites, `Buffer::_read` and `Buffer::write` (the
    overdub mix stays in float, then clamps + quantizes). Identity / byte-identical codegen
    when the flag is off.
  - `kSourceMaxSeconds` 42 -> 84 under the flag; `wav.h` emits a 16-bit PCM header; the SD
    load path (`card.cpp`) accepts the matching depth and computes frames from the correct
    byte width.
  - `Makefile` gains a `LOFI_INT16=1` build option (mirrors `DEBUG=1`).
  Audio storage behaviour in the default build is byte-identical (the `int16` paths compile
  out); the default save format is still 32-bit float.

- **Convert-on-load shim (`src/core/pcm_convert.h`, wired into `card.cpp`).** The SD load
  path now accepts BOTH 16-bit PCM and 32-bit float files in either build and converts to
  the buffer's storage width on the fly (`convert_pcm_block`, pure + host-tested). So a
  16-bit firmware loads legacy float tapes and a float firmware loads 16-bit tapes -
  resolving the prior "rejects mismatched tapes" limitation; tapes are portable across
  builds. `kChunk` (32 KB) is a multiple of both sample widths and WAV data is
  sample-aligned, so chunks never straddle a sample (no carry buffer needed). Loading a
  longer tape than the buffer holds truncates to capacity. The streaming size/offset/
  termination math lives in a pure, host-tested helper (`src/core/pcm_loader.h`,
  `PcmLoader`) that card.cpp delegates to, so the accounting is validated off-target rather
  than by reasoning alone. Note: this adds the dual-format acceptance + converter to the
  **default** build too (~+150 B `SRAM_EXEC`), so the default firmware now also reads 16-bit
  tapes - a small, additive behaviour change. The audible overdub-quantization character of
  the 16-bit buffer remains unverified by ear. See `docs/lofi-int16-scope.md`.

- **Design scope docs** `docs/lofi-path-b-scope.md` (half-rate buffer) and
  `docs/lofi-int16-scope.md` (16-bit storage): inventories of the frame<->time/tempo
  coupling and the change sites for each lo-fi record-time approach.

### Fixed (portability)

- **`divider.h` now includes `<cmath>` for `std::round`** (was relying on `<math.h>`, which
  only places `round` in `std::` on the ARM toolchain, not in standard libc++). Surfaced by
  the new host harness; no effect on the firmware build. (`src/core/divider.h`)

- **`WavHeader::size` retyped `size_t` -> `uint32_t`.** The RIFF chunk-size field is a
  32-bit field per the WAV spec; `size_t` is 4 bytes only on the 32-bit target, so the
  44-byte layout (and its `static_assert`) held there but broke on a 64-bit host and would
  emit a malformed header on any 64-bit build. Layout-identical on-target (verified: the
  firmware binary is byte-for-byte unchanged in size); `wav.h` also now includes the
  `<cstdint>`/`<cstddef>`/`<cstring>` it uses rather than relying on its includers.
  Surfaced by the host harness. (`src/memory/wav.h`)

- **`follower.cpp` now includes `<cmath>` for `std::exp`** (was relying on `<math.h>`; same
  std-namespace portability issue as `divider.h`). Surfaced by the host harness; no effect
  on the firmware build. (`src/core/follower.cpp`)

## [1.0.2]

Baseline release prior to this changelog. See Git history for details.
