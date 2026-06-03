# TODO

Deferred work, newest first. See `docs/` for the platform/engine design and `CHANGELOG.md` for done work.

## Grow `src/dsp/` with the remaining shared primitives

`src/dsp/` (the engine-agnostic primitive tier, dependency flows platform/engine -> dsp, never the
reverse) was seeded in Phase 5 R4 with the header-only batch: `lutsinosc.h`, `smooth.h`, `deline.h`,
`hann.h`. Two follow-ups remain:

- **Move the `.cpp`-bearing generic primitives** currently under `src/engine/granular/`: `biquad.{h,cpp}`
  (filter), `follower.{h,cpp}` (envelope follower), `adenv.{h,cpp}` (AD envelope), `cpattern.{h,cpp}`.
  They're self-contained (no granular siblings) but deferred because each needs build wiring: their
  `.cpp` must move from granular's `$(wildcard src/engine/granular/*.cpp)` into the **global**
  `CPP_SOURCES` (a new `$(wildcard src/dsp/*.cpp)`) so every engine links them. Verify that doesn't
  regress SRAM and that non-granular builds still link cleanly.
- **Refactor the delay engine to use the shared primitives** instead of its hand-rolled copies: the
  delay reimplemented one-pole smoothing and a fractional delay line, which now live in `dsp/smooth.h`
  and `dsp/deline.h`. This is the concrete second consumer that justified the tier - but it CHANGES
  the delay's DSP (its smoothing/interpolation may not be bit-identical to the shared versions), so do
  it deliberately with a hardware flash test, not as a silent swap.

Principle going forward: a primitive earns its place in `dsp/` when it gets a real second consumer,
not preemptively. Raised 2026-06-03 during Phase 5 R4.

## Mono-input normalization (left -> right when right is unused)

When only the left input is patched, mirror it to the right so a mono source feeds both channels
(e.g. the stereo delay's two taps both get signal instead of the right tap going silent).

Open question — how to detect "right input not used":
- **Hardware normalling** (preferred if the board supports it): the right input jack normals to the
  left when nothing is plugged in, so it's automatic and the firmware does nothing. Check whether the
  Spotykach audio input jacks are physically normalled; if so, this TODO is moot.
- **Software fallback** (if not normalled): detect a near-silent right input (peak below a small
  threshold over a window) and copy left -> right. Needs hysteresis/timing so it doesn't flap, and a
  decision on where it lives — it's a *platform* input concern (applies to any engine), so it likely
  belongs in the platform's audio path (e.g. `AppImpl::ProcessAudio` before `engine.process`), not in
  an individual engine. Caveat: silence-detection can't tell "cable plugged but quiet" from "no cable".

Raised 2026-06-03 while testing the stereo delay (engine #2): a mono source into the left input left
the right delay tap silent.
