# Edrums — four-drum Euclidean drum machine

`ENGINE=edrums` · `src/engine/edrums/edrums_engine.{h,cpp}` · class `EdrumsEngine`

A drum machine whose hits are placed by **Euclidean rhythms** and voiced by **synthesized** drums (no samples). It is the first engine to *sequence* off the shared platform transport — it subscribes to the clock's ticks rather than just reading tempo. Status: **scaffold** (works on hardware; this doc is both the as-built record and the design/roadmap for improving it).

**Four drums, two editable at a time.** Each of the two decks holds **two drums** (a "slot" pair), and all four sequence and sound at once. The platform stays two-deck: a deck's knobs and ring address whichever of its two drums is **focused**, and the **Rev pad** swaps the focus. The other drum keeps playing in the background. So you build a four-piece kit but only ever edit two voices at a moment — see [Four drums](#four-drums-two-per-deck).

---

## Concept

- Two independent tracks, deck **A** and deck **B**, each = a Euclidean pattern + a drum voice.

- The pattern is a grid of steps; some steps are *onsets* (hits) distributed as evenly as possible (the Euclidean property). On each clock step the track advances one grid slot and, on an onset, fires its voice.

- All four drums step off the same transport clock, so the rhythm locks to the instrument's tempo and clock source (internal / TS4 / MIDI). `e.reset` realigns every pattern to the bar.

## Four drums: two per deck

The two physical decks each carry **two drums** in a slot pair (`_track[deck][slot]`, slot 0/1). All four always sequence (their own length, density, division) and always sound — focus never gates audio.

- **What focus changes:** only which drum a deck's seven knobs and its ring address, and which one the platform's knob-pickup cache tracks. The backgrounded drum holds its stored parameters and keeps playing.

- **The swap gesture:** tapping a deck's **Rev pad** toggles that deck's focus between its two drums. On a swap the engine asks the platform to re-seed that deck's knob pickup from the newly-focused drum's values, so the pots take over cleanly (a knob does nothing until moved to the new drum's value, then catches — no jumps).

- **Why two-at-a-time:** the panel has two of everything (two rings, two knob banks, two Rev pads). Mapping deck→slot inside the engine keeps the whole platform two-deck and the `IEngine` contract unchanged; the only addition is a one-shot "re-seed requested" poll the platform already honours generically.

- **Seeing the background drum:** the ring shows the focused drum's pattern in its colour; the **Rev LED** carries the backgrounded drum's colour (dim steady, brightening on its hits), so you always see the other drum is there and which it is. Colours are per `(deck, slot)`: deck A warm (slot 0 amber, slot 1 red-orange), deck B cool (slot 0 cyan, slot 1 violet).

Defaults form a usable four-piece kit: deck A = **Kick** (slot 0) + **Tom** (slot 1), deck B = **Snare** (slot 0) + **Hat** (slot 1).

## Audio: the synthesized voice

Each drum has one `Voice` (a small abstraction so sample playback could replace it later behind the same `trigger()` / `process()` seam) — four in all. The voice is:

- **Body** — a sine (`dsp/LUTSinOsc`) with a fast (~25 ms) downward **pitch sweep** on trigger (`freq = base_hz · (1 + pitch_env·3)`, `pitch_env` decaying) — the classic drum "thump".

- **Noise** — a cheap inline LCG, for snare/clap/hat character.

- **Amp envelope** — exponential decay (`amp *= amp_coef` per sample); `trigger()` re-arms it.

- **Tone** — a fixed per-deck blend of body vs noise: `out = (body·(1−tone) + noise·tone) · amp`.

### Drum models (Alt + PITCH, live)

The voice has **5 models**, selected live by holding **Alt** and turning **PITCH** (no commit step — the change applies on the next hit; release Alt and PITCH is pitch again). Each model sets the voice character (body/noise mix, pitch-sweep amount, decay scaling, noise-band centre); PITCH still drives body frequency + noise colour and SOS the decay *within* the model. Per deck, independently.

| # | Model | Character |
|---|---|---|
| 0 | Kick | body, large pitch drop, longer decay |
| 1 | Snare | body + mid-band noise |
| 2 | Clap | noisy, multi-burst (3 quick re-triggers), no pitch sweep |
| 3 | Closed hat | high-band noise, very short |
| 4 | Tom | pitched body, mild drop |

Defaults, per drum: deck A = **Kick** (slot 0) + **Tom** (slot 1); deck B = **Snare** (slot 0) + **Hat** (slot 1). Each slot is fully seeded at init (length, density, pitch, decay, model), so the two background drums sound from the first bar without waiting on a knob move.

> Why a knob and not a load-pad: synth model switching is instantaneous and free, so it applies live > — the select-then-load ceremony is only needed for expensive operations (sample loading), and is > reserved for that (see roadmap). The same Alt+PITCH control will select sample slots later, the only > difference being a deferred/debounced load instead of an instant apply.

The live model select rides a new platform seam: `CapAux` + `ParamId::Aux`. The platform routes Alt+PITCH to `Aux` only for engines that advertise `CapAux`; granular (which uses Alt+PITCH for pitch-quantize) is unaffected.

The sequencer triggers run in the transport-tick callback (the audio-block context); `trigger()` only re-arms envelope state, so it is allocation/lock free.

### Output routing (the hardware routing switch, control 26)

The 3-position routing switch picks how the four voices reach the two outputs (via `set_config(Route)`, mirroring granular's int mapping so the panel's L/C/R read the same). A pre-limiter trim then a final `SoftLimit` keep the summed bus in range — four simultaneous voices sum hotter than the original two, so the trim avoids constant limiting:

| Switch | `Route` | Edrums behavior |
|---|---|---|
| Centre | `Stereo` | all four voices **summed to both outputs** (a mono drum bus) — the conventional default |
| Left | `DoubleMono` | deck A's two drums → left, deck B's two → right (the split) |
| Right | `GenerativeStereo` | each hit randomly panned across the stereo field |

`route()` reports the mode for the panel's L/C/R LED.

## Sequencer: `dsp/CPattern`

`CPattern` is a fixed **16-step** Euclidean generator (Christoffel-word algorithm). Used **only** by edrums, so it is free to evolve for this engine.

- `set_onsets(n)` — density: distribute `n` onsets over the 16 slots.

- `set_shift(frac)` — rotation: offset the read position.

- `trigger()` — advance one step; returns whether the (shifted) slot is an onset.

- `reset()` — return to slot 0 (called on `e.reset`).

- `step_is_onset(s)` / `position()` — read-only views for the LED display (no side effects).

## Control map (as built)

The platform gives each deck 7 knobs (see [README](README.md#knobs-how-a-physical-control-reaches-an-engine)); edrums uses all of them:

| Knob | `ParamId` (routing) | Function |
|---|---|---|
| **POS** | `Pos` | density — Euclidean onsets, as a fraction of the current length |
| **SIZE** | `Size` | **pattern length** (2..16 steps) |
| **ENV** | `Env` | **rotation** (pattern shift) |
| **PITCH** | `Speed` | drum pitch — sine body (~30..480 Hz) **and** the noise band centre |
| **SOS** | `Mix` | decay (amp-env time, ~30 ms..1.2 s) |
| **MOD_AMT** | `ModAmp` | **probability** an onset fires (0..100%, default 100% = full clockwise) |
| **MODFREQ** | `set_mod_speed` | **clock division** — 1/16, 1/8, 1/4 (per deck) |
| **Alt + PITCH** | `Aux` | **drum model** select (live; see below) |

All seven knobs act on the deck's **focused** drum; the **Rev pad** swaps focus to the other drum (see [Four drums](#four-drums-two-per-deck)). Density is stored as a fraction and re-derived over the active length, so changing SIZE keeps the relative fill. `POS` and `MOD_AMT` are engine-seeded (the platform reads `param(Pos)` / `param(ModAmp)` for their initial values), so init pre-seeds them: density A≈5/16, B≈7/16, and probability 100% (so the knob defaults to "every onset fires" with full clockwise = 100%). The display draws the focused drum's pattern over the active length (the length fills the 32-LED ring; onset lit, playhead bright) in that drum's colour, with a play-LED flash on each hit; the Rev LED carries the backgrounded drum's colour. Colours are per `(deck, slot)`: A slot 0 amber / slot 1 red-orange, B slot 0 cyan / slot 1 violet.

### Polymeter

Per-deck length **and** division make the two tracks cycle independently (a deck's cycle = `length × division` ticks). On the **external** clock a transport grid reset (`e.reset`) realigns both decks (step phase + pattern position) to the bar; on the **internal** clock there is no reset, so the decks free-run and realign naturally at their least-common-multiple. Tempo/clock-source are the shared transport's (TAP / tap-hold+MODFREQ_A / Alt+TAP — see [README](README.md#clock-control-player-facing)).

---

## Feedback from first hardware test (2026-06-05) and resolutions

**Status: P1 + P2 implemented (2026-06-05) — see the as-built control map above. This section is kept as the design rationale.** The notes below were the agreed improvement set. The enabling fact: **each deck actually has 7 knobs, not 4** — `ENV`, `MODFREQ`, `MOD_AMT` are unused and already routed to `ParamId`s the engine can claim. So every item below fits without modifier layers.

### 1. SIZE should set pattern *length*, not rotation

Rotation on the big SIZE knob is unintuitive, and there is no length control at all. Proposed:

- Make `CPattern` **variable length** (`set_length(n)`, 1..16; regenerate the Euclidean distribution over `n` active slots instead of the fixed 16). Safe — edrums is its only user.

- **SIZE → length**, **ENV → rotation**. Different lengths per deck give polymeter; `e.reset` realigns both at the bar.

### 2. PITCH does not affect the "clap" (deck B)

Not a routing bug — PITCH_A→A, PITCH_B→B is correct. Deck B is noise-dominated, so PITCH only moves the small sine body and the noise stays put. Proposed: PITCH also drives a **bandpass centre on the noise** (reuse `dsp/biquad`), so pitch shifts the *color* of noise-based voices. Then PITCH is meaningful for both kick and clap/snare/hat.

### 3. Probability (0–100%) per step

A per-deck chance that a scheduled onset actually fires — humanizes / varies the grid. Proposed: **MOD_AMT → probability**. On an onset, fire only if `rand() < prob` (a cheap per-track LCG; seed so it is deterministic across a reset if desired).

### 4. Different time division per deck

Yes, possible and cheap via `TransportTick.index` (a monotonic 16th counter): a per-deck **divisor** makes a track step every Nth tick. Proposed: **MODFREQ → division** (1/16, 1/8, 1/4, dotted, triplet from a small table), overriding `set_mod_speed`. Deck A on 1/8 + deck B on 1/16, etc.

### Proposed control map (target)

| Knob | Function | Routing |
|---|---|---|
| POS | density (onsets) | `Pos` |
| SIZE | **length** | `Size` |
| ENV | **rotation** | `Env` |
| PITCH | pitch + **noise color** | `Speed` |
| SOS | decay | `Mix` |
| MOD_AMT | **probability** | `ModAmp` |
| MODFREQ | **time division** | `set_mod_speed` |

(Tempo stays on TAP / tap-hold+MODFREQ_A as a transport gesture — see [README](README.md#clock-control-player-facing).)

---

## Roadmap

- **P1 — control remap + free controls.** DONE: variable-length `CPattern`, SIZE→length, ENV→rotation, MOD_AMT→randomness, MODFREQ→division, polymeter, render over the active length.

- **P2 — voice depth.** DONE: PITCH→noise bandpass; **5 selectable drum models** (kick/snare/clap/hat/ tom) live via Alt+PITCH (`CapAux`/`ParamId::Aux`); the Clap is a **multi-burst** voice (re-arms the amp env 3× ~11 ms apart); a model change briefly shows the **model number** (model+1 white dots) on the ring. Still open: per-voice accent/velocity; per-model voice tuning.

- **P4 — four drums (two per deck).** DONE: each deck holds two drums (slot pair); all four sequence and sound; the **Rev pad** swaps which drum a deck edits/shows; the platform re-seeds the deck's knob pickup on a swap (clean takeover, no jumps); per-`(deck, slot)` colours; the **Rev LED** shows the backgrounded drum. Contract cost: one defaulted `IEngine::take_param_reseed`. Still open: a panel cue that a deck *has* a second drum beyond the Rev-LED colour; possibly Alt+Rev to reach further banks. Tuning: the `kBusTrim` (0.6) and the two hue families are first guesses, to confirm by ear/eye on hardware.

- **P3 — timing.** Triggers land on the audio-block boundary (~2 ms quantization, set in the tick callback). For tighter feel, schedule the sample-offset within the block.

- **Later.** More division values (currently 3: 1/16, 1/8, 1/4) incl. triplets; sample playback behind the `Voice` seam (SD/Card path); per-step ratchet/roll; swing (the transport `Divider` supports it); pattern save/recall.

## Known limitations (as built)

- Step timing is block-quantized (~2 ms) — see P3.

- Both decks share the PITCH knob *default*, so distinct pitch needs per-deck dial-in; the kick vs snare/clap distinction comes from the fixed per-deck `tone` (body vs band-passed noise).

- Clock division is 3 values (1/16, 1/8, 1/4); no triplets yet.

- Polymeter realigns to the bar only on the external clock (internal clock has no grid reset).

## Files

- `src/engine/edrums/edrums_engine.h` — `EdrumsEngine` + nested `Voice` / `Track`.

- `src/engine/edrums/edrums_engine.cpp` — voice synthesis, transport sink, param map, render.

- `src/dsp/cpattern.{h,cpp}` — the Euclidean pattern generator (edrums-only).

- `src/dsp/lutsinosc.h` — the sine body.

- `host/test_edrums_slots.cpp` — headless test of the slot model (focus swap, slot independence, the re-seed one-shot, finite four-voice output). Run: `make -C host test-edrums`.

- Build: `engine_select.h` (`SPK_ENGINE_EDRUMS`), `Makefile` (`ENGINE=edrums`, `make engine-edrums`).
