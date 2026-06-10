# tape engine

`ENGINE=tape` · `src/engine/tape/tape_engine.{h,cpp}` · class `TapeEngine`

A **dual streaming tape deck**: two independent **mono** decks (A/B), each playing or recording its own arbitrarily long file on the SD card, removing the in-SDRAM loop-length cap that bounds the other engines (~42 s float / ~84 s int16, `kSourceMaxSeconds` in `src/config.h`). A deck is **play-XOR-record** (no overdub), so the two run like a pair of record decks - play deck A while recording deck B, then play both together and beat-match by ear with each deck's PITCH. It is a **linear** player/recorder, not a granular scrubber - random SD seek cannot meet the 2 ms audio deadline, so each tape only ever streams forward.

The engine is deliberately thin. In `process()` (the audio ISR) it only moves float frames between the platform's lock-free per-deck rings and the audio buffers, then applies the per-block pan/blend/volume gains; all the slow FatFs I/O happens off the audio path in the platform's `StreamDeck` pump (main loop).

> **Status: hardware-verified (core); latest control map pending re-flash.** Dual-deck record/playback, per-deck varispeed, routing/mix/volume, and the four ENV loop modes were confirmed on the device. The most recent additions - **pan moved to Alt+POS**, **tape-slot select on Alt+PITCH**, and per-deck **`/tapes/` slot files** - build clean and pass the host tests, but have not yet been re-flashed. The deeper build/bring-up narrative (and the resolved-bug writeups) lives in `docs/dev/tape-impl.md`.

---

## Status at a glance

- **Hardware-verified:** two decks record and play independently (A->L, B->R), varispeed, routing/pan/mix, and the loop modes all work on the device. The original in-SDRAM length cap is gone.

- Streaming core (rings + WAV codec + looping) **host-tested**: `make -C host test` includes `test-stream` (ISR -> ring -> WAV file -> ring -> ISR, byte-exact, plus the loop-rewind path) - all green.

- Firmware links and fits: `make -j8 ENGINE=tape` -> ~81.5% SRAM_EXEC, no overflow. Other engines build unchanged.

- SD throughput held in testing; mono-per-deck keeps two simultaneous streams at roughly the bandwidth of one old stereo stream. Sustained both-decks-at-2x is the remaining throughput unknown (if it underruns, int16 streaming jumps up the list).

---

## Audio I/O and routing

The hardware has two **mono** inputs (A, B; input A is normalled to B when B is unpatched), two mono outputs, and a stereo headphone monitor. The tape engine maps them as two independent decks blended to a stereo bus:

- **Inputs:** deck A records **input A** (`in[0]`), deck B records **input B** (`in[1]`). Independent - never summed.

- **Outputs:** the two decks are mixed to a stereo bus (`out[0]` = L, `out[1]` = R) that drives the headphone; the individual jacks tap the same bus.

Three knobs/controls place the decks in that bus:

| Control | `ParamId` / config | Effect |
|---|---|---|
| **Routing switch** | `ConfigId::Route` | topology (see below); also lights the L/C/R mode LED |
| **Alt + POS** (per deck) | `AltPos` | per-deck **pan** (equal-power, 0 = L, 0.5 = C, 1 = R) |
| **MIX knob** (per deck) | `Mix` | per-deck **playback volume** |
| **Mix fader** | `Crossfade` | **A/B blend** (DJ-style: centre = both full, ends = one deck only) |

(Bare **POS** is reserved for a future loop-start control and currently does nothing.)

The routing switch (mirrors the panel L/C/R, granular's int convention):

- **LEFT (`DoubleMono`):** each deck panned by **its own Alt+POS knob**.

- **CENTRE (`Stereo`):** both decks centered (summed equally to both outputs); POS ignored.

- **RIGHT (`GenerativeStereo`):** each deck at a random pan position (re-rolled on entering the mode).

Total per-deck gain into the bus = **MIX volume x mix-fader blend x pan(L/R)**.

---

## Controls

| Control | Action |
|---|---|
| **Play pad** (per deck) | play toggle |
| **Alt + Play pad** (per deck) | record toggle |
| **PITCH** (per deck, `Speed`) | varispeed playback (`exp2((v-0.5)*2)` -> 0.5x..2x, +/-1 octave, pitch+speed linked) |
| **Alt + PITCH** (per deck, `Aux`) | tape-slot select (visual selector while held, see below) |
| **Alt + POS** (per deck, `AltPos`) | pan (LEFT routing) |
| **MIX** (per deck, `Mix`) | playback volume |
| **ENV** (per deck, `Env`) | loop mode (4 quadrants, see below) |
| **Mix fader** (`Crossfade`) | A/B blend |
| **Routing switch** (`Route`) | pan topology |
| **POS** (per deck, `Pos`) | tape FX: **saturation drive** (see Tape FX) |
| **SIZE** (per deck, `Size`) | tape FX: **character** (J-A reversibility `c`) |
| **MOD_AMT** (per deck, `ModAmp`) | tape FX: **wow/flutter depth** |
| **MODFREQ** (per deck, `ModSpeed`) | tape FX: **wow/flutter rate** |

- Play and record are **mutually exclusive per deck**; each deck has 8 slot files under `/tapes/` (see "Tape slots" below). Control routes through `on_play_pad` (play) / `on_record_pad` (Alt+Play); the **Rev pad is inert** (reserved for future reverse playback). A 300 ms same-deck debounce guards capacitive-pad glitches.

- **Display:** per deck, idle = ring **off**; **bright green** = playing, **bright red** = recording; a rejected start (no file / card not mounted) flashes that ring **amber** ~1.2 s. Status also rides the **Play-pad LED** (the pad you press). The routing switch position shows on the **mode L/C/R** indicator. The live input is **monitored** to the deck's channel while recording.

`capabilities() = CapOwnDisplay | CapDualDeck | CapAux | CapAltPos`. `CapAux` claims Alt+PITCH (tape-slot select); `CapAltPos` claims Alt+POS (pan) - both are platform-gated remaps, so non-tape engines are unaffected. It does **not** advertise `CapTapeStorage` - the tape engine owns its own SD streaming, so the platform `Storage` service stays out of the way (it mounts the card at boot and, in the tape build, leaves it mounted for the stream; see Risks).

## Tape slots

Each deck has **8 slots**, files `/tapes/tape_a_1.wav` … `/tapes/tape_a_8.wav` (and `tape_b_`), so takes are non-destructive and recallable rather than overwriting one fixed file. **Alt+PITCH** selects the active slot; while Alt is held, the deck's ring shows the **8 slots as evenly-spaced dots with the selected one bright** (the same `set_aux_active` selector seam reso uses for its model picker). Selecting a slot sets the target for the next Play / record - it does not interrupt a deck already playing. Record writes the selected slot (overwriting only that one); Play reads it (amber if empty). The selector shows **recorded vs empty** slots (selected bright / recorded mid / empty dim) - the engine probes each slot file with `IStreamDeck::exists` (`f_stat`) in `prepare()` when the selector opens, and marks a slot used the moment its record starts. The `/tapes/` directory is created on first record. Single-digit slot numbers keep the names 8.3-safe.

To load **your own** audio into a slot, the file must be **mono 32-bit-float WAV at 48 kHz** - the engine does no on-device conversion, and a wrong-format file (16-bit / 32-bit-int / stereo / non-48k) is rejected with a strobing amber error LED. Convert source files with [`scripts/convert_tape_audio.py`](../../scripts/convert_tape_audio.py) or the ffmpeg/sox one-liners in [`docs/preparing-audio.md`](../preparing-audio.md).

---

## Loop modes (ENV knob, per deck)

The ENV knob picks one of four loop behaviors by quadrant, from fully CCW:

| ENV | Mode | Behavior |
|---|---|---|
| `< 0.25` | **None** | play once, stop at end |
| `< 0.5` | **Plain loop** | seamless repeat at full level |
| `< 0.75` | **Faded loop** | repeat with a ~50 ms fade across the seam (de-click) |
| `>= 0.75` | **Frippertronics** | each pass ~0.6x quieter; fades out over ~8 passes, then auto-stops |

**Mechanism:** looping lives in the stream - `PlayStream` rewinds the WAV at EOF (`WavStreamReader::rewind`) instead of finishing, gated by a per-deck `set_loop` flag the engine pushes from `prepare()` (main loop). The fade/decay shaping is engine-side: it reads the loop length (`IStreamDeck::loop_frames`), tracks source-frame position, and applies the seam fade (Faded) or per-pass decay (Frippertronics). Because `stop()` touches FatFs, a faded-out Frippertronics deck is flagged from the ISR and stopped in `prepare()`, never in the audio path.

Loops are the *recorded take's* length (free-run, not tempo-aligned). Three constants are first-cut/tunable: seam fade `kFadeFrames` (~50 ms), per-pass decay `kFrippDecay` (0.6), fade-out floor `kFrippFloor` (0.02).

### Varispeed

Playback is resampled in the ISR by a 2-frame linear interpolator: the engine advances `_speed` source frames per output frame and reads fractionally between the two, per deck. Pitch and speed move together (tape-style) - there is no time-stretch. Read-ahead and the SD pump scale with consumption automatically, so faster playback simply drains the ring faster.

---

## Tape FX (wow/flutter + hysteresis)

Each deck has its own analog-tape effect chain, applied to the **playback** signal only (not the record monitor, whose wow/flutter delay would add monitoring latency). The DSP is a **Faust kernel generated by cyfaust** (`src/engine/tape/tapefx.dsp` -> `faust_kernel_tapefx.h`, namespace `tfx_tapefx`), the same toolchain the reverb engine uses; one mono kernel per deck is placement-new'd into the SDRAM arena at `init()`.

Chain: **wow/flutter -> Jiles-Atherton hysteresis/saturation.**

- **Wow & flutter** - a modulated fractional delay (`de.fdelay`) wobbles playback pitch. Periodic, not random (reel rotation): a slow wow LFO plus a flutter LFO built from f + 2f + 3f harmonics (after ChowTape's `FlutterProcess`). The LFOs use the table-free recursive oscillator `os.oscrs` - `os.osc` would emit a 64K-entry static sine table (256 KB) that lands in SRAM and overflows the region. MOD_AMT sets depth, MODFREQ sets rate.

- **Hysteresis / saturation** - `hy.ja_processor` from Faust's `hysteresis.lib`: the **Jiles-Atherton** magnetic model, the same one in Chowdhury's ChowTapeModel but self-contained (fixed 4 substeps + cubic-Hermite interpolation = built-in antialiasing, no extra oversampling). In this model the saturation *is* the hysteresis: POS = drive (amount), SIZE = character (the reversibility `c`, 0.25 open/dynamic .. 0.9 compressed). The processor gain-compensates drive and DC-blocks internally, and clamps magnetization to [-1,1] so it stays bounded when driven hard.

**Licensing.** `hysteresis.lib` is LGPLv2.1 **with the Faust library exception** ("you may ... distribute the compiled code ... under your own copyright and license"), so the cyfaust-generated C++ ships under the project's MIT - the same basis as the reverb kernels. (C) 2025 Thomas Mandolini. **ChowTapeModel itself (`thirdparty/AnalogTapeModel`) is GPL-3 and is used only as an algorithm/parameter reference - never linked.**

**Voicing.** The J-A is intentionally subtle (the lib's -50 dB calibration voices it for musical tape coloration, not fuzz); louder loops and higher drive push it further. The voicing levers, to dial by ear on hardware: the drive range (`drive * 54` dB in the `.dsp`), the `Ms/a/alpha/k` ferromagnetic params, and the wow/flutter rate/depth scaling.

**CPU.** Not yet measured on hardware (`Meter::cpu`). The J-A runs 4 substeps/sample (~1 `tanh` + ~4 divisions each) x 2 decks; estimated ~10-25% of the 480 MHz budget. If too hot: swap `ma.tanh` for a polynomial Langevin approx, or fall back to an ADAA-tanh saturator. Measure before relying on it.

---

## Architecture

Producer/consumer split, per deck, mirroring the existing `Storage` service:

- **Audio ISR** (`TapeEngine::process()`) only ever touches the bounded per-deck SDRAM rings (`play_consume` / `record_produce`); it never blocks and never calls FatFs.

- **Main loop** (`AppImpl::Loop` -> `StreamDeck::process()`) does the slow FatFs chunked I/O - filling each playing deck's ring ahead of the ISR and draining each recording deck's ring to disk, both decks pumped sequentially.

`StreamDeck` holds **two independent `Deck` units** (ring + file handle + reader/writer + mode); the `IStreamDeck` contract is per-deck (`play_consume(deck, ...)`, `start_play(deck, path)`, `set_loop(deck, ...)`, ...). The contract encodes the threading split: the `*_consume`/`*_produce` calls are ISR-only and never block; `start_*`/`stop`/`set_loop` are main-loop-only; the state reads are safe from either side.

**SDRAM layout** (static `DSY_SDRAM_BSS` in `buffer.sdram.cpp`, tape-only): one **1 MB** ring **per deck** (each serves that deck's play OR record; ~5.5 s of mono read-ahead) plus a 32 KB chunk scratch shared by both decks (the pumps run sequentially). No increase over the original two-ring layout.

Streaming format is mono float WAV (one channel per deck), so neither play nor record needs sample conversion - the audio path is already float.

---

## Files

New core (host-tested, engine-agnostic):

- `src/memory/spsc_ring.h` - lock-free SPSC byte ring over an external buffer (kfifo-style, wrap-safe).

- `src/memory/audio_stream.h` - `IChunkSource` / `IChunkSink`, `PlayStream` (with looping), `RecordStream`.

- `src/memory/byte_file.h` - `IByteFile` seam (read / write / seek).

- `src/memory/wav_stream.h` - `WavStreamReader` (parse + rewind) / `WavStreamWriter` (mono-capable) over `IByteFile`.

- `host/test_stream.cpp` - the end-to-end host suite incl. the loop-rewind test (wired into `make -C host test`).

New device:

- `src/engine/istreamdeck.h` - the per-deck `IStreamDeck` contract.

- `src/hw/fat_file.{h,cpp}` - FatFs-backed `IByteFile` (body guarded `#if defined(SPK_ENGINE_TAPE)`).

- `src/hw/stream_deck.{h,cpp}` - the dual-deck `StreamDeck` platform service (guarded body).

- `src/engine/tape/tape_engine.{h,cpp}` - the engine: dual decks, varispeed, routing/pan/mix/volume, loop modes, LED feedback, and the per-deck tape FX (`TapeFx`).

- `src/engine/tape/tapefx.dsp` + `faust_kernel_tapefx.h` - the tape-FX Faust source and its **generated** kernel (wow/flutter + J-A hysteresis). Regenerate with `make faust-gen`.

- `src/engine/faust_arch.h` - the shared MIT Faust arch shim (`dsp`/`UI`/`Meta` base types), used by the tape and reverb kernels. Lifted here from `src/engine/reverb/` when a second engine started using Faust.

- `host/test_tape.cpp` - host test for the tape FX (param round-trip, saturation wired/bounded, wow/flutter modulates, playback gate), wired into `make -C host test`.

Edited:

- `src/engine/engine_context.h` - adds `IStreamDeck* stream = nullptr;`.

- `src/hw/buffer.sdram.{h,cpp}` - `streamMem()` + the per-deck SDRAM rings (guarded).

- `src/memory/wav.h` - `wav_header(size, channels=2)` (mono headers for the per-deck files).

- `src/memory/storage.h` - under `#if SPK_ENGINE_TAPE`, keep the SD card mounted (skip `_can_unmount()`'s unmount) so the tape stream can open files all session.

- `src/hw/fat_file.cpp` - `open_write` creates the parent dir (`/tapes`) before the file.

- `src/engine/engine_params.h` - adds `ParamId::AltPos` + `CapAltPos` (the Alt+POS knob layer, capability-gated).

- `src/ui/core.ui.{h,cpp}` - routes Alt+POS -> `AltPos` for `CapAltPos` engines (mirrors the Alt+PITCH->`Aux` gate); non-claiming engines keep POS->`Pos` byte-identically.

- `src/engine/granular/granular_engine.cpp` - `AltPos` no-op case (keeps the param switch `-Wswitch`-clean).

- `src/app.cpp` - construct / init / inject `StreamDeck`, pump it in `Loop` (all `#if SPK_ENGINE_TAPE`).

- `src/engine/engine_select.h`, `Makefile`, `CMakeLists.txt`, `Makefile.cmake` - register `tape`.

---

## Build / flash / test

```text
make faust-gen                                    # regenerate the tape-FX kernel (needs the .venv cyfaust)
make engine-tape                                  # clean + build + DFU flash (Make path)
make -f Makefile.cmake ENGINE=tape program-dfu    # CMake path
make -j8 ENGINE=tape                              # build only (~86.9% SRAM_EXEC, incl. tape FX)
make -C host test                                 # host suites incl. test-tape / test-stream (all green)
```

On hardware, after ~1 s for the SD to mount: pick a slot per deck with **Alt+PITCH** (ring shows the selector), then **Alt+Play(A)** records input A and **Alt+Play(B)** records input B into the selected slots; **Play(A)** / **Play(B)** play them back simultaneously (A->L, B->R). Turn each deck's **PITCH** for varispeed, **Alt+POS** to pan (LEFT routing), **MIX** for volume, **ENV** for the loop mode; the **mix fader** blends A/B and the **routing switch** picks the pan topology. Files land under **`/tapes/`** as `tape_a_<n>.wav` / `tape_b_<n>.wav`.

### Build-system note

All streaming code is `#if defined(SPK_ENGINE_TAPE)`, so non-tape engines are byte-identical (granular gains only +8 B from the shared `EngineContext` field). Because the guarded TUs (`stream_deck.o`, `fat_file.o`, `buffer.sdram.o`) are flag-dependent, the `Makefile` makes them depend on `build/.engine-stamp` (like `app.o`); otherwise a `make ENGINE=tape` over a stale non-tape build would relink empty objects. The `engine-tape` one-shot target runs `make clean` regardless.

---

## Risks / watch-items

- **Dropouts / underruns** (steady-state held; sustained both-decks-at-2x unproven). Mono-per-deck keeps two simultaneous streams near the bandwidth of one old stereo stream (~192 KB/s each, vs ~1.5 MB/s on the `MEDIUM_SLOW` 1-bit SDMMC). If they occur: raise SD speed / bus width (`card.cpp` `recognize()`) and/or deepen the rings.

- **SD mount lifecycle** (resolved). The per-deck files are at the volume root. `Storage` used to unmount the card after reading settings (via `_can_unmount()`), which killed the tape stream - the tape build now keeps it mounted. `FatFile` and `Storage`'s `Card` share one FatFs volume but never do I/O concurrently (Storage stays idle in the tape build; the tape engine owns all streaming I/O). See `docs/dev/tape-impl.md`.

- **Format = float WAV.** Twice the SD bandwidth of int16; int16 streaming is the main future throughput lever.

---

## What's left

### Hardening (known gaps)

- **SD-full / partial `f_write`** - `RecordStream::pump()` pulls bytes out of the ring before `sink->write()`; a short write (disk full) loses them. Don't advance the ring until written, or stop cleanly on SD-full.

- **Error feedback** - done (coarse): `start_play` / `start_record` failures flash the ring amber ~1.2 s. Still one colour for all causes (missing file / not mounted / disk full); per-reason feedback would need `StreamDeck` to report a reason code.

- **Mount-readiness** - partly: the card stays mounted for the session, so steady-state works. A press in the first ~1 s after boot (before the mount completes) still fails, but now flashes amber rather than dying silently.

- **Format validation** - `WavStreamReader` parses any WAV header, but the engine reinterprets the body as native mono float 48k; a differently-formatted file plays as garbage. Guard before allowing arbitrary-file playback. (Self-recorded files are internally consistent.)

- **Concurrency review** - play-finished + finalize + loop-rewind observed working on device; the remaining edge cases of the ISR <-> main-loop handshakes (stop during the `_finalizing` flush window, the Frippertronics ISR->prepare stop flag, underrun at a loop seam) are reasoned-through but not yet stress-tested.

### Feature backlog

- **More slots / a browser** - 8 slots per deck exist now (`/tapes/tape_<d>_<n>.wav`, Alt+PITCH select, with a recorded-vs-empty selector); more banks or a proper file browser are the next step.

- **Seek / scrub** within the long file (`f_lseek` + ring flush).

- **Transport sync** + tempo-aligned loop points; **reverse** playback (the Rev pad is reserved for it).

- **int16 streaming** - halves SD bandwidth, the main throughput lever.

- **Loop-mode tuning** - the seam-fade length and Frippertronics decay/floor are first-cut constants; tune by ear. A wider / quantized varispeed range; progress/level meters on the rings.
