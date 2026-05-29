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

### Changed

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

## [1.0.2]

Baseline release prior to this changelog. See Git history for details.
