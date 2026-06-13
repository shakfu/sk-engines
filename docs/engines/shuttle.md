# shuttle engine

`ENGINE=shuttle` · `src/engine/shuttle/shuttle_engine.{h,cpp}` · class `ShuttleEngine`

A **buffer-based bipolar/reverse varispeed tape** - the random-access counterpart to the SD-streaming [`tape`](tape.md) engine. Where `tape` streams forward-only from the card, `shuttle` holds audio in SDRAM, so the read pointer is random-access and **reverse**, **freeze**, and **loop windowing** are trivial. The trade is a finite, RAM-capped tape length (currently 30 s per track).

It runs **four independent mono tape tracks** - two per deck (A/B). All four free-run at their own speeds and sum into the stereo bus; you flip a deck's edit focus between its two tracks with the **Rev pad** (the edrums mechanism), and re-align all four to a common downbeat with the **Seq pad**.

The headline control is **PITCH as a bipolar capstan-speed knob**: noon stops the tape (silence), clockwise plays forward up to +2x, counter-clockwise plays in reverse down to -2x. Unity (+1x) lands off-centre, so the **Play pad snaps** the focused track to normal speed.

> **Status: host-verified, pending hardware bring-up.** All eleven host-test groups pass and > `make ENGINE=shuttle` builds + links on target (SRAM_EXEC ~89% with the tape FX, SDRAM arena reserved whole). Not yet > flashed; in particular the four-instance Jiles-Atherton CPU load (see [Tape FX](#tape-fx)) is still to be confirmed on a `METER=1` build. Implementation notes and the memory-variant menu live in `docs/dev/shuttle_impl.md`.

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
| **MOD_AMT** | `ModAmp` | per-track | tape-FX wow/flutter **depth** (see [Tape FX](#tape-fx)) |
| **MODFREQ** | `ModSpeed` | per-track | tape-FX wow/flutter **rate** |
| **grit + PITCH** | `GritIntensity` | per-track | tape-FX saturation **drive** |
| **grit + MIX** | `GritMix` | per-track | tape-FX saturation **character** |
| **flux + PITCH** | `FluxIntensity` | per-track | tape-FX filter **cutoff** (boots fully open) |
| **flux + MIX** | `FluxMix` | per-track | tape-FX filter **resonance** |
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

- **Load** (Alt+PITCH selects a slot) drains an existing `/shuttle/` WAV from the card into the track's RAM buffer over a few main-loop passes, then shuttles it like any other take. Reuses the platform's streaming service via the `SPK_USE_STREAM` capability flag (shared with the `tape` engine). A loaded file must be **mono 32-bit-float WAV at 48 kHz** (other formats are rejected); convert sources with [`scripts/convert_tape_audio.py`](../../scripts/convert_tape_audio.py) - pass `--engine shuttle` so it warns past the ~30 s RAM cap - or the one-liners in [`docs/preparing-audio.md`](../preparing-audio.md).

- **Boot preload.** On power-up, **all four tracks** are filled from the card if their files exist - track *t* of each deck loads slot *t*'s file (deck A: `tape_a_1/_2.wav` -> tracks 0/1; deck B: `tape_b_1/_2.wav`) - so a freshly powered shuttle is ready to jam without a manual load. (The RAM engine can't stream lazily the way the `tape` engine does, so it loads up front.) It is **per-track conditional** (a track whose slot file is absent is left empty) and, because a deck's two tracks share one SD stream, **serialized per deck** (the two decks load in parallel). Since the card mounts cooperatively early in boot, the load waits for the volume to come up, then is abandoned after a short deadline if there is no card / no files.

## Tape FX

Each track can run the same tape-FX kernel as the [`tape`](tape.md) engine - **wow/flutter** (a modulated fractional delay) -> **Jiles-Atherton hysteresis saturation** -> a **post-FX resonant low-pass** - shared verbatim from `src/engine/tape/tapefx.h`. The FX is **per-track** and addresses the focused track, like every other knob (it repoints on a Rev swap).

Because `shuttle` already uses POS/SIZE for the loop window (where the `tape` engine puts drive/character), the FX is driven from this engine's free controls - the two end-of-chain effect pads plus the MOD knobs:

- **grit pad = saturation:** hold **grit + PITCH** for drive (amount), **grit + MIX** for character (the J-A tone).
- **flux pad = filter:** hold **flux + PITCH** for cutoff, **flux + MIX** for resonance. Cutoff **boots fully open**, so the filter is inert until you sweep it down (the flux+PITCH pickup seeds open -> turn down to engage).
- **MOD_AMT / MODFREQ = wow/flutter** depth / rate.

**Neutral = bypassed.** With drive/character/wow at zero, cutoff fully open and resonance zero (the boot defaults), the kernel is **skipped entirely** rather than run flat: the wow/flutter delay line imposes a fixed ~25 ms delay even at zero depth and the filter is not bit-identity even fully open, so a "neutral" kernel would not be transparent. Skipping it keeps shuttle's varispeed playback **bit-faithful** when the FX is untouched, and confines the (non-trivial) Jiles-Atherton cost to tracks that are both rolling and actually using the FX. The summed bus is `SoftLimit`ed only while the FX is engaged (saturation + a resonant peak can overshoot 0 dBFS); with the FX off the bus stays a plain linear sum.

## Architecture

- **Contiguous per-track buffers** carved from the SDRAM arena at `init()`; `kBufSeconds * sample_rate` frames each (30 s -> 1.44 M frames -> 5.76 MB; 4 tracks = 23 MB of the 48 MB arena).

- **Signed fractional read pointer** (`double`) per track - playback advances by a signed speed and linear-interpolates; reverse is a negative speed, freeze is speed 0 -> silence, ends wrap (loop).

- **Per-track FX** (4 kernel instances) placement-new'd in the arena at `init()`, applied to each track's mono signal (when engaged + rolling) before the pan/sum. See [Tape FX](#tape-fx).

- **Per-track audio** is panned individually into the stereo bus (track volume x A/B mix-fader x per-track pan); the audio ISR (`process`) is the four reads, the optional per-track FX, then the gains.

- **Re-align declick:** an audibly-rolling track dips through a ~1.3 ms fade (out -> jump at the gain minimum -> in) so the pointer jump is click-free; the snap is signalled by a flag and applied in the ISR, so it is race-free and all four tracks snap on the same block.

Capabilities: `CapOwnDisplay | CapDualDeck | CapAux | CapAltPos | CapPitchPickup`.

## Files

- `src/engine/shuttle/shuttle_engine.{h,cpp}` - the engine.

- `host/test_shuttle.cpp` - the headless test (11 groups).

- `src/engine/tape/tapefx.h` - the shared tape-FX kernel wrapper (also used by the `tape` engine).

- Platform: `CapPitchPickup` (`engine_params.h`, `ui/core.ui.{h,cpp}`); `SPK_USE_STREAM` capability flag gating the SD streaming service (`Makefile`, `app.cpp`, `hw/buffer.sdram.{h,cpp}`, `hw/stream_deck.cpp`, `hw/fat_file.cpp`, `memory/storage.h`).

## Build / test

```sh
make -j8 ENGINE=shuttle        # build the firmware
make -C host test-shuttle      # run the headless test (or `make -C host test` for all engines)
```

## Memory variants

The current config is 30 s / 48 kHz / `float32` (23 MB). `docs/dev/shuttle_impl.md` documents the full menu of experiments and their quantified memory/scope: 60 s buffers, `int16` storage (half the memory, no audible cost), lower storage sample rate, a chunked/paged pool allocator (pay-per-use, flexible per-track length), and a soft length cap. It also explains why "dynamic buffer sizing" frees nothing on a single-engine firmware (the arena is statically reserved).
