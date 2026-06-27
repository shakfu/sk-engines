# Dev notes — tempo-synced stereo delay (`delay` engine)

Implementation, the file map, and dev notes for `ENGINE=delay`. The user-facing reference (signal path, tempo sync, control map, characters, topologies) is [`docs/engines/delay.md`](../engines/delay.md).

## Tests

`make -C host test-delay` (`host/test_delay.cpp`): a fed-back delay rings out and more feedback lengthens the tail; the three characters are distinct + finite; ping-pong cross-feeds (driving deck A produces deck-B output) while DoubleMono does not; the ENV tone changes the output; the mod LFO changes the output; Freeze sustains the loop far longer than an unfrozen decay; **Reverse** (Rev pad) differs from forward, rings out, stays bounded, and is bit-identical to forward after toggling twice; and every character stays finite + bounded at maximum feedback.

## Notes / possible improvements

- Sync is always on; there is no free-running mode (retired). A tempo-free fine (ms) mode on an Alt layer is still open.

- Feedback is capped (`·0.95`); Tape/Shimmer additionally soft-clip the loop, and Freeze loops it at unity (the soft-clip keeps Tape/Shimmer frozen loops from running away while they evolve).

- The mod LFO is one per tap (MODFREQ rate / MOD_AMT depth); in Tape it has a rate+depth floor, which is that character's wow/flutter, so Tape warbles even with the mod knobs down.

- **Reverse** (Rev pad, per deck) reads the buffer *backwards* over a delay-length window. `Tap::read_rev(win)` walks a phase forward 0..win while the read offset (`win - phase`) shrinks from the oldest sample toward the write head; two heads half a window apart are raised-cosine crossfaded so the offset wrap is click-free (the same seamless-wrap trick as the pitch `Shifter`). `read_buf(off)` is the shared linear-interpolated read used by both forward and reverse. Reverse composes with all characters and topologies (it only changes the read), and the mod LFO still wobbles the window. The gesture is idempotent (toggle twice = forward).

- The diffuse and ducking ideas were taken further in a separate **[qdelay](../engines/qdelay.md)** flavor (`ENGINE=qdelay`) rather than added as delay characters — it swaps the palette to Clean/Diffuse/Duck. See [`docs/dev/qdelay-impl.md`](qdelay-impl.md).

- Still open: dotted/triplet display glyphs; a tempo-free fine (ms) mode.

## Files

`src/engine/delay/delay_engine.{h,cpp}`; build via `engine_select.h` (`SPK_ENGINE_DELAY`) + `Makefile` (`ENGINE=delay`, `make engine-delay`). Host test: `host/test_delay.cpp`.
