# shuttle engine

`ENGINE=shuttle` · `src/engine/shuttle/shuttle_engine.{h,cpp}` · class `ShuttleEngine`

A **buffer-based bipolar/reverse varispeed tape** - the random-access counterpart to the SD-streaming [`tape`](tape.md) engine. Where `tape` streams forward-only from the card, `shuttle` holds audio in SDRAM, so the read pointer is random-access and **reverse**, **freeze**, and **loop windowing** are trivial. The trade is a finite, RAM-capped tape length (currently 30 s per track).

It runs **four independent mono tape tracks** - two per deck (A/B). All four free-run at their own speeds and sum into the stereo bus; you flip a deck's edit focus between its two tracks with the **Rev pad** (the edrums mechanism), and re-align all four to a common downbeat with the **Seq pad**.

The headline control is **PITCH as a bipolar capstan-speed knob**: noon stops the tape (silence), clockwise plays forward up to +2x, counter-clockwise plays in reverse down to -2x. Unity (+1x) lands off-centre, so the **Play pad snaps** the focused track to normal speed.

> **Status: host-verified, pending hardware bring-up.** All nine host-test groups pass and > `make ENGINE=shuttle` builds + links on target (SRAM_EXEC ~82%, SDRAM arena reserved whole). Not yet > flashed. Implementation notes and the memory-variant menu live in `docs/dev/shuttle_impl.md`.

---

## Controls

| Control | `ParamId` / pad | Scope | Effect |
|---|---|---|---|
| **PITCH** | `Speed` | per-track | bipolar capstan speed: noon = stop (silence), CW = forward to +2x, CCW = reverse to -2x |
| **POS** | `Pos` | per-track | loop window **start** (slides the loop through the recording) |
| **SIZE** | `Size` | per-track | loop window **length** (full buffer down to a short stutter) |
| **MIX** | `Mix` | per-track | track volume |
| **Alt + PITCH** | `Aux` | per-track | tape-slot select (8 files/deck under `/shuttle/`) -> loads the slot into RAM |
| **Alt + POS** | `AltPos` | per-deck | pan (equal-power) |
| **Mix fader** | `Crossfade` | global | deck A/B blend |
| **Play pad** | - | per-deck (focused) | toggle rolling; **snaps the track to unity (+1x)** on engage |
| **Rev pad** | - | per-deck | **swap** which of the deck's two tracks is focused (the other keeps playing) |
| **Alt + Play** | - | per-deck (focused) | record the focused track (play XOR record) |
| **Seq pad** | - | global | **re-align all four tracks** to their loop start (one atomic, declicked gesture) |

The four tracks free-run independently, so they drift out of phase (organic tape phasing) - the Seq pad pulls them back to a common downbeat. Because the absolute pots can't physically move, the Play->unity snap holds via the platform's pickup soft-takeover (`CapPitchPickup`): after a snap, the PITCH pot must be swept across unity before it retakes the speed.

## Loop window (POS / SIZE)

Each track plays a window `[start, start+length)` of its buffer, not always the whole recording:

- **SIZE** sets the length (full buffer down to a `kMinLoopFrames` floor - a sub-loop / stutter).

- **POS** slides the start across the part of the buffer the window doesn't cover, so the window always stays inside the recording. At SIZE = full, POS is inert.

The varispeed read pointer wraps within the window (both directions); the Seq re-align snaps to the window start.

## Audio source: record + load

- **Record** (Alt+Play) captures the live input into the focused track's buffer (overwrite from the start), monitored as it records. Deck A records input A, deck B records input B.

- **Load** (Alt+PITCH selects a slot) drains an existing `/shuttle/` WAV from the card into the track's RAM buffer over a few main-loop passes, then shuttles it like any other take. Reuses the platform's streaming service via the `SPK_USE_STREAM` capability flag (shared with the `tape` engine).

## Architecture

- **Contiguous per-track buffers** carved from the SDRAM arena at `init()`; `kBufSeconds * sample_rate` frames each (30 s -> 1.44 M frames -> 5.76 MB; 4 tracks = 23 MB of the 48 MB arena).

- **Signed fractional read pointer** (`double`) per track - playback advances by a signed speed and linear-interpolates; reverse is a negative speed, freeze is speed 0 -> silence, ends wrap (loop).

- **Per-deck audio** = its two tracks summed at their volumes; then per-deck pan x A/B mix-fader -> stereo. The audio ISR (`process`) is just the four reads + the gains.

- **Re-align declick:** an audibly-rolling track dips through a ~1.3 ms fade (out -> jump at the gain minimum -> in) so the pointer jump is click-free; the snap is signalled by a flag and applied in the ISR, so it is race-free and all four tracks snap on the same block.

Capabilities: `CapOwnDisplay | CapDualDeck | CapAux | CapAltPos | CapPitchPickup`.

## Files

- `src/engine/shuttle/shuttle_engine.{h,cpp}` - the engine.

- `host/test_shuttle.cpp` - the headless test (9 groups).

- Platform: `CapPitchPickup` (`engine_params.h`, `ui/core.ui.{h,cpp}`); `SPK_USE_STREAM` capability flag gating the SD streaming service (`Makefile`, `app.cpp`, `hw/buffer.sdram.{h,cpp}`, `hw/stream_deck.cpp`, `hw/fat_file.cpp`, `memory/storage.h`).

## Build / test

```sh
make -j8 ENGINE=shuttle        # build the firmware
make -C host test-shuttle      # run the headless test (or `make -C host test` for all engines)
```

## Memory variants

The current config is 30 s / 48 kHz / `float32` (23 MB). `docs/dev/shuttle_impl.md` documents the full menu of experiments and their quantified memory/scope: 60 s buffers, `int16` storage (half the memory, no audible cost), lower storage sample rate, a chunked/paged pool allocator (pay-per-use, flexible per-track length), and a soft length cap. It also explains why "dynamic buffer sizing" frees nothing on a single-engine firmware (the arena is statically reserved).
