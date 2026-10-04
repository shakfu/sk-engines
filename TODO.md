# TODO

Deferred work, in priority order (highest first). See `docs/` for the platform/engine design and `CHANGELOG.md` for done work.

## Critical

## High

- [ ] Indicator toolkit: net-new indicators deferred from the 2026-07-31 migration (HARDWARE-GATED): `ring::value` pickup feedback, `led::clock` (reso/delay/qdelay/edrums), `led::cycle` for the LFO engines, idle breathe on renders without `ITimeSource`. See `docs/dev/indicator-comparison.md` §7.

- [ ] Confirm input A is jack-normalled to B (HARDWARE-GATED). No firmware mirrors it (`docs/manual.md:78`), so it is presumably the jack wiring. Check: `ENGINE=passthrough`, patch A only, both outputs carry signal. If confirmed, softcut records A-only input at +6 dB, since it sums `in[0] + in[1]` (`softcut_engine.cpp:226,248`).

- [ ] One bench session: measure + voice the engines that work but aren't quantified (HARDWARE-GATED)

## Medium

- [ ] Refactor the delay engine onto the shared primitives (HARDWARE-GATED)

- [ ] Web front-end: browser-based SD card builder + terminal (BUILT 2026-08-01, host-verified only)

## Low

- [ ] Tape wow/flutter rate: experiment with a quadratic curve and lower maximums
