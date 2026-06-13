# Edrums — four-drum Euclidean drum machine

`ENGINE=edrums` · `src/engine/edrums/edrums_engine.{h,cpp}` · class `EdrumsEngine`

A drum machine whose hits are placed by **Euclidean rhythms** and voiced by **synthesized** drums (no samples). It is the first engine to *sequence* off the shared platform transport — it subscribes to the clock's ticks rather than just reading tempo. Status: **working** — four drums (two per deck), confirmed on hardware and covered by the headless slot test (`make -C host test-edrums`); this doc is the as-built record plus the design/roadmap for improving it.

**Four drums, two editable at a time.** Each of the two decks holds **two drums** (a "slot" pair), and all four sequence and sound at once. The platform stays two-deck: a deck's knobs and ring address whichever of its two drums is **focused**, and the **Rev pad** swaps the focus. The other drum keeps playing in the background. So you build a four-piece kit but only ever edit two voices at a moment — see [Four drums](#four-drums-two-per-deck).

---

## Concept

- Four independent drums, two on each deck (**A** / **B**), each = a Euclidean pattern + a drum voice. A deck edits one of its two drums at a time (the **focused** one); see [Four drums](#four-drums-two-per-deck).

- The pattern is a grid of steps; some steps are *onsets* (hits) distributed as evenly as possible (the Euclidean property). On each clock step the drum advances one grid slot and, on an onset, fires its voice.

- All four drums step off the same transport clock, so the rhythm locks to the instrument's tempo and clock source (internal / TS4 / MIDI). `e.reset` realigns every pattern to the bar.

## Four drums: two per deck

The two physical decks each carry **two drums** in a slot pair (`_track[deck][slot]`, slot 0/1). All four always sequence (their own length, density, division) and always sound — focus never gates audio.

- **What focus changes:** only which drum a deck's seven knobs and its ring address, and which one the platform's knob-pickup cache tracks. The backgrounded drum holds its stored parameters and keeps playing.

- **The swap gesture:** tapping a deck's **Rev pad** toggles that deck's focus between its two drums. On a swap the engine asks the platform to re-seed that deck's knob pickup from the newly-focused drum's values, so the pots take over cleanly (a knob does nothing until moved to the new drum's value, then catches — no jumps).

- **Why two-at-a-time:** the panel has two of everything (two rings, two knob banks, two Rev pads). Mapping deck→slot inside the engine keeps the whole platform two-deck; the `IEngine` contract grows by exactly one defaulted method (`take_param_reseed`, the one-shot re-seed poll), so the other engines are unaffected.

- **Seeing the background drum:** the ring shows the focused drum's pattern in its colour; the **Rev LED** carries the backgrounded drum's colour (dim steady, brightening on its hits), so you always see the other drum is there and which it is. Colours are per `(deck, slot)`: deck A warm (slot 0 amber, slot 1 magenta), deck B cool (slot 0 cyan, slot 1 violet).

Defaults assign a four-piece kit — deck A = **Kick** (slot 0) + **Tom** (slot 1), deck B = **Snare** (slot 0) + **Hat** (slot 1) — but the kit **boots silent**: every drum is seeded with zero onsets (`POS=0`). The player builds the groove up from nothing by raising **POS** on each drum (and Rev-swapping each deck to reach the slot-1 pair). Because only the onset count is withheld — model, pitch, decay, and length are all pre-seeded — a drum is in tune and correctly voiced the instant its first onset appears.

## Audio: the synthesized voice

Each drum has one `Voice` (a small abstraction so sample playback could replace it later behind the same `trigger()` / `process()` seam) — four in all. The voice is:

- **Body** — a sine (`dsp/LUTSinOsc`) with a downward **pitch sweep** on trigger (`freq = base_hz · (1 + pitch_env·sweep)`, `pitch_env` decaying over a **per-model time**), plus an optional **second detuned partial** (a ratio of the body, for snare/tom richness) and a gentle **saturation** (`drive` → soft-clip) for punch and harmonics. The classic drum "thump".

- **Noise** — a cheap inline LCG → a 2-pole filter that **band-passes** the noise (snare/clap/tom) or **high-passes** it (the hat's metallic "tss"); the centre/corner tracks pitch (`base_hz · noise_mult`).

- **Two amp envelopes** — body and noise decay **independently** (exponential, `amp *= amp_coef` / `namp *= namp_coef`), so a voice's snap and its tonal ring can have different lengths (e.g. a snare whose noise rings past a short body). `trigger()` re-arms both.

- **Attack click** — an optional brief (~2 ms) bright transient at onset (the kick beater / tom mallet), summed on top.

- **Tone** — the body/noise blend: `out = body·(1−tone) + noise·tone + click`. Each model also carries a per-model `base_scale`, so every drum tunes to its own sensible range under the one shared PITCH knob.

### Drum models (Alt + PITCH, live)

The voice has **5 models**, selected live by holding **Alt** and turning **PITCH** (no commit step — the change applies on the next hit; release Alt and PITCH is pitch again). Each model sets the full voice character — body/noise balance, pitch-sweep amount **and time**, body frequency offset, body **and** noise decay, noise centre/Q and band-vs-high-pass, the second partial, saturation, and attack click. PITCH still drives body frequency + noise colour *within* the model, and the grit/flux macros (see [Live sound macros](#live-sound-macros)) offset the model's drive/sweep/brightness/balance/decay. Per deck, independently. The model baselines all live in the single `kModels` table in `edrums_engine.cpp` — the one surface to tune the kit by ear.

| # | Model | Character |
|---|---|---|
| 0 | Kick | deep sine body, long (~55 ms) pitch drop, beater click + drive, longer decay |
| 1 | Snare | two-partial body + bright band-pass noise that rings past the body |
| 2 | Clap | noisy, multi-burst (3 quick re-triggers ~11 ms apart), tail rings |
| 3 | Closed hat | **high-passed** noise (metallic), very short |
| 4 | Tom | two-partial pitched body, mild drop, mallet click |

Defaults, per drum: deck A = **Kick** (slot 0) + **Tom** (slot 1); deck B = **Snare** (slot 0) + **Hat** (slot 1). Each slot is fully voiced at init (length, pitch, decay, model) but seeded with **zero onsets**, so the kit boots silent and the player builds it up with POS — no drum waits on a knob move to be correctly voiced once raised.

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
| **SOS** | `Mix` | **per-drum gain** (level) |
| **MOD_AMT** | `ModAmp` | **probability** an onset fires (0..100%, default 100% = full clockwise) |
| **MODFREQ** | `set_mod_speed` | **clock division** — 1/16, 1/8, 1/4 (per deck) |
| **Alt + PITCH** | `Aux` | **drum model** select (live; see below) |
| **grit + SOS** | `GritMix` | **decay** (amp-env time, ~30 ms..1.2 s) |
| **grit + PITCH** | `GritIntensity` | **drive** (saturation) — see [Live sound macros](#live-sound-macros) |
| **flux + PITCH** | `FluxIntensity` | **pitch-sweep amount** (±2 oct on the model) |
| **flux + SOS** | `FluxMix` | **body↔noise balance** |
| **flux + POS** | `FluxFb` | **noise brightness** (filter cutoff, ±2 oct) |
| **hold Alt + Seq** (~1.5 s) | `clear_sequence` | **reset that deck's two drums to factory defaults** — see [Presets](#presets-qspi-auto-persist) |

The seven plain knobs act on the deck's **focused** drum; the **Rev pad** swaps focus to the other drum (see [Four drums](#four-drums-two-per-deck)). Density is stored as a fraction and re-derived over the active length, so changing SIZE keeps the relative fill. `POS` and `MOD_AMT` are engine-seeded (the platform reads `param(Pos)` / `param(ModAmp)` for their initial values), so init pre-seeds them: density **0** on every drum (the silent boot — POS seeds to minimum, so turning it up adds hits) and probability 100% (so the knob defaults to "every onset fires" with full clockwise = 100%). The display draws the focused drum's pattern over the active length (the length fills the 32-LED ring; onset lit, playhead bright) in that drum's colour, with a play-LED flash on each hit; the Rev LED carries the backgrounded drum's colour. Colours are per `(deck, slot)`: A slot 0 amber / slot 1 magenta, B slot 0 cyan / slot 1 violet.

### Live sound macros

The plain knobs above run the sequencer + pitch/gain; the per-drum **sound shaping** lives on the **grit** and **flux** pad modifiers (hold the pad, turn PITCH/SOS/POS), so the kit is tweakable in performance rather than fixed per model. Each macro is a **bipolar offset on the model's baseline** — at noon the drum is exactly as the model voiced it, so a fresh kit is unchanged until you move a knob — and like every other knob it addresses the **focused** drum (repointing on a Rev swap):

| Gesture | Macro | Range |
|---|---|---|
| **grit + SOS** | decay (amp-env time) | ~30 ms .. 1.2 s |
| **grit + PITCH** | drive (saturation) | clean .. crunch |
| **flux + PITCH** | pitch-sweep amount | x the model's sweep, ±2 oct |
| **flux + SOS** | body↔noise balance | ±0.5 around the model |
| **flux + POS** | noise brightness (filter cutoff) | ±2 oct |

With PITCH (pitch), SOS (gain) and Alt+PITCH (model), that is **eight live axes per drum** — and all four drums voice independently. The macro channels route through the platform's existing grit/flux held-modifier layer (`GritIntensity`/`GritMix`/`FluxIntensity`/`FluxMix`/`FluxFb`), which is pickup-seeded from `param()`, so no platform change was needed. (Decay sits on `grit+SOS` rather than `Alt+SOS` because the `Feedback` channel `Alt+SOS` routes to is not pickup-seeded.) Not yet exposed: an **independent body-vs-noise (snap) decay** — the voice has separate envelopes for it, but it needs a sixth modifier channel (a small platform addition), so the one decay currently scales both.

### Presets (QSPI auto-persist)

The whole **kit auto-saves to QSPI flash**, so your tweaks survive a power cycle with no save gesture. The full state of all four drums - model, pitch, gain, decay, the four timbre macros, and the sequencer params (density/length/rotation/probability/division) - plus the route and per-deck focus is captured as a small POD blob (`EdrumsEngine::KitData`) and written via libDaisy `PersistentStorage`.

- **Save** is debounced: any kit change starts a ~1.5 s timer, and the flash erase+write happens once changes settle, from the main loop (never the audio ISR), so a knob sweep coalesces into a single write.

- **Load** happens at `init()`, before the platform seeds the knob pickups, so a recalled kit takes over cleanly (no knob jump); `MValue` pickup then holds each value until you move that pot. The first-ever boot (no saved kit) keeps the built-in defaults.

- **Storage layout.** A dedicated QSPI region (offset `0x10000`, slug `edk`) separate from the calibration `Settings` (offset 0) and the app image (offset `0x40000`). The `PersistentStorage` template version is pinned at **1 permanently** - bumping it would hit libDaisy's `bkpt`-on-version-mismatch; layout changes are instead versioned by `KitData::version`, which `apply()` rejects on mismatch (falling back to defaults). serialize/apply are pure and host-tested (round-trip); the QSPI read/write is target-only (`#ifndef TEST`) and **pending hardware verification**.

- **Reset to defaults.** Holding **Alt + Seq** on a deck for ~1.5 s (the platform's clear-sequence gesture) resets that deck's two drums to the factory kit - model, pitch, decay, the macros (back to neutral), and the pattern (back to silent) - from the same `kBootKit` table `init()` uses, refocuses the deck to slot 0, and marks the kit dirty so the auto-save overwrites the stored preset (the reset persists). It is **per deck** (it maps to the per-deck gesture): hold on deck A to reset kick/tom, on deck B for snare/hat, or both for a full kit reset. A pickup re-seed is requested so the knobs take over the reset values cleanly (the platform's `_reseed_focus` now also re-seeds the grit/flux macro channels, which fixes the same stale-pickup gap on a Rev swap).

Not yet: multiple named presets / banks (would be SD-card files with a save/select gesture); this is a single auto-persisted "current kit".

### Polymeter

Per-drum length **and** division make all four drums cycle independently (a drum's cycle = `length × division` ticks). On the **external** clock a transport grid reset (`e.reset`) realigns every drum (step phase + pattern position) to the bar; on the **internal** clock there is no reset, so the drums free-run and realign naturally at their least-common-multiple. Tempo/clock-source are the shared transport's (TAP / tap-hold+MODFREQ_A / Alt+TAP — see [README](README.md#clock-control-player-facing)).

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

- **P2 — voice depth.** DONE: PITCH→noise bandpass; **5 selectable drum models** (kick/snare/clap/hat/ tom) live via Alt+PITCH (`CapAux`/`ParamId::Aux`); the Clap is a **multi-burst** voice (re-arms the amp env 3× ~11 ms apart); a model change briefly shows the **model number** (model+1 white dots) on the ring. **Voice rework (synthesis depth):** independent body/noise decays, per-model pitch-envelope time, a high-passed hat (metallic, vs the old narrow band-pass), an optional second detuned body partial (snare/tom), gentle body saturation, an attack click (kick beater / tom mallet), and a per-model `base_scale` so each drum tunes to its own range. The whole kit is voiced from one `kModels` table; host-checked finite/bounded/audible per model + a hat-vs-kick brightness assertion, but the **voicing values are first-principles starting points pending an on-hardware listen** (the table is the tuning surface). **Live performance controls:** SOS is now per-drum **gain** (decay moved to `grit+SOS`), and the grit/flux modifiers expose four per-drum **timbre macros** (drive, pitch-sweep, brightness, body↔noise) as bipolar offsets on the model baseline — see [Live sound macros](#live-sound-macros). **Preset persistence:** the whole kit auto-saves to QSPI and reloads at boot - see [Presets](#presets-qspi-auto-persist) (built, pending hardware verification). Still open: per-voice accent/velocity; an independent body-vs-noise snap decay (needs a sixth modifier channel); multiple named presets/banks on SD.

- **P4 — four drums (two per deck).** DONE: each deck holds two drums (slot pair); all four sequence and sound; the **Rev pad** swaps which drum a deck edits/shows; the platform re-seeds the deck's knob pickup on a swap (clean takeover, no jumps); per-`(deck, slot)` colours; the **Rev LED** shows the backgrounded drum. Contract cost: one defaulted `IEngine::take_param_reseed`. Still open: a panel cue that a deck *has* a second drum beyond the Rev-LED colour; possibly Alt+Rev to reach further banks. Tuning: the `kBusTrim` (0.6) and the two hue families are first guesses, to confirm by ear/eye on hardware.

- **P3 — timing.** Triggers land on the audio-block boundary (~2 ms quantization, set in the tick callback). For tighter feel, schedule the sample-offset within the block.

- **Later.** More division values (currently 3: 1/16, 1/8, 1/4) incl. triplets; sample playback behind the `Voice` seam (SD/Card path); per-step ratchet/roll; swing (the transport `Divider` supports it); pattern save/recall.

## Known limitations (as built)

- Step timing is block-quantized (~2 ms) — see P3.

- Each of the four drums is seeded with its own pitch (and model), but a deck's PITCH knob only edits its **focused** drum; re-pitching the backgrounded drum means swapping focus to it first (its timbre still differs via the selected model — body vs band-passed noise).

- The four voices share one bus with a fixed pre-limiter trim (`kBusTrim`, 0.6). Per-drum **level** now exists (SOS = gain), but there is still no per-hit **accent/velocity** (every onset fires at the drum's set gain).

- Clock division is 3 values (1/16, 1/8, 1/4); no triplets yet.

- A deck gives no panel cue that it *has* a second drum beyond the Rev-LED colour; the focused/background split is only legible once you know to read the Rev LED.

- Polymeter realigns to the bar only on the external clock (internal clock has no grid reset).

## Files

- `src/engine/edrums/edrums_engine.h` — `EdrumsEngine` + nested `Voice` / `Track`.

- `src/engine/edrums/edrums_engine.cpp` — voice synthesis, transport sink, param map, render, and the kit serialize/apply + QSPI auto-persist (target-only).

- Platform: `EngineContext::qspi` (`engine/engine_context.h`, set in `app.cpp`) — the opaque QSPI handle the kit preset persists through; and `CoreUI::_reseed_focus` (`ui/core.ui.cpp`) now re-seeds the grit/flux macro pickups on a focus swap / reset.

- `src/dsp/cpattern.{h,cpp}` — the Euclidean pattern generator (edrums-only).

- `src/dsp/lutsinosc.h` — the sine body (one per voice, plus a second for the detuned partial).

- `src/dsp/biquad.h` — the noise filter (`BiquadSection`, switched band-pass / high-pass per model).

- `host/test_edrums_slots.cpp` — headless test of the slot model (focus swap, slot independence, the re-seed one-shot, finite four-voice output) plus a per-model voicing group (each model renders finite/bounded/audible; the high-passed hat is far brighter than the kick). Run: `make -C host test-edrums`.

- Build: `engine_select.h` (`SPK_ENGINE_EDRUMS`), `Makefile` (`ENGINE=edrums`, `make engine-edrums`).
