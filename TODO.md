# TODO

Deferred work, newest first. See `docs/` for the platform/engine design and `CHANGELOG.md` for done work.

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
