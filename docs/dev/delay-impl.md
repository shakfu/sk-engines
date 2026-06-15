# Dev notes — tempo-synced stereo delay (`delay` engine)

Implementation, the file map, and dev notes for `ENGINE=delay`. The user-facing reference (signal path, tempo sync, control map, characters, topologies) is [`docs/engines/delay.md`](../engines/delay.md).

## Tests

`make -C host test-delay` (`host/test_delay.cpp`): a fed-back delay rings out and more feedback lengthens the tail; the three characters are distinct + finite; ping-pong cross-feeds (driving deck A produces deck-B output) while DoubleMono does not; the ENV tone changes the output; and every character stays finite + bounded at maximum feedback.

## Notes / possible improvements

- Sync is always on; there is no free-running mode (retired). A tempo-free fine (ms) mode on an Alt layer is still open.

- Feedback is capped (`·0.95`); Tape/Shimmer additionally soft-clip the loop, and Freeze loops it at unity (the soft-clip keeps Tape/Shimmer frozen loops from running away while they evolve).

- The mod LFO is one per tap (MODFREQ rate / MOD_AMT depth); in Tape it has a rate+depth floor, which is that character's wow/flutter, so Tape warbles even with the mod knobs down.

- Still open: dotted/triplet display glyphs; ducking (sidechain the wet to the dry envelope); more characters (diffuse / reverse); a tempo-free fine (ms) mode.

## Files

`src/engine/delay/delay_engine.{h,cpp}`; build via `engine_select.h` (`SPK_ENGINE_DELAY`) + `Makefile` (`ENGINE=delay`, `make engine-delay`). Host test: `host/test_delay.cpp`.
