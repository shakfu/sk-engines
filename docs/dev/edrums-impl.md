# Dev notes — four-drum Euclidean drum machine (`edrums` engine)

Implementation/bring-up notes for `ENGINE=edrums`. The user-facing reference (concept, the four-drum
layout, drum models, output routing, the control map, and how to play it) is
[`docs/engines/edrums.md`](../engines/edrums.md); this file holds the file map, the sequencer internals,
the hardware-test feedback/resolutions, the roadmap, the known limitations, and the QSPI-persist
mechanics.

## Sequencer: `dsp/CPattern`

`CPattern` is a fixed **16-step** Euclidean generator (Christoffel-word algorithm). Used **only** by edrums, so it is free to evolve for this engine.

- `set_onsets(n)` — density: distribute `n` onsets over the 16 slots.

- `set_shift(frac)` — rotation: offset the read position.

- `trigger()` — advance one step; returns whether the (shifted) slot is an onset.

- `reset()` — return to slot 0 (called on `e.reset`).

- `step_is_onset(s)` / `position()` — read-only views for the LED display (no side effects).

## Presets (QSPI auto-persist) — mechanics

The full state of all four drums - model, pitch, gain, decay, the four timbre macros, and the sequencer params (density/length/rotation/probability/division) - plus the route and per-deck focus is captured as a small POD blob (`EdrumsEngine::KitData`) and written via libDaisy `PersistentStorage`.

- **Save** is debounced: any kit change starts a ~1.5 s timer, and the flash erase+write happens once changes settle, from the main loop (never the audio ISR), so a knob sweep coalesces into a single write.

- **Load** happens at `init()`, before the platform seeds the knob pickups, so a recalled kit takes over cleanly (no knob jump); `MValue` pickup then holds each value until you move that pot. The first-ever boot (no saved kit) keeps the built-in defaults.

- **Storage layout.** A dedicated QSPI region (offset `0x10000`, slug `edk`) separate from the calibration `Settings` (offset 0) and the app image (offset `0x40000`). The `PersistentStorage` template version is pinned at **1 permanently** - bumping it would hit libDaisy's `bkpt`-on-version-mismatch; layout changes are instead versioned by `KitData::version`, which `apply()` rejects on mismatch (falling back to defaults). serialize/apply are pure and host-tested (round-trip); the QSPI read/write is target-only (`#ifndef TEST`) and **pending hardware verification**.

- **Reset to defaults.** Holding **Alt + Seq** on a deck for ~1.5 s (the platform's clear-sequence gesture) resets that deck's two drums to the factory kit - model, pitch, decay, the macros (back to neutral), and the pattern (back to silent) - from the same `kBootKit` table `init()` uses, refocuses the deck to slot 0, and marks the kit dirty so the auto-save overwrites the stored preset (the reset persists). It is **per deck** (it maps to the per-deck gesture): hold on deck A to reset kick/tom, on deck B for snare/hat, or both for a full kit reset. A pickup re-seed is requested so the knobs take over the reset values cleanly (the platform's `_reseed_focus` now also re-seeds the grit/flux macro channels, which fixes the same stale-pickup gap on a Rev swap).

Not yet: multiple named presets / banks (would be SD-card files with a save/select gesture); this is a single auto-persisted "current kit".

## Feedback from first hardware test (2026-06-05) and resolutions

**Status: P1 + P2 implemented (2026-06-05) — see the as-built control map in [edrums.md](../engines/edrums.md). This section is kept as the design rationale.** The notes below were the agreed improvement set. The enabling fact: **each deck actually has 7 knobs, not 4** — `ENV`, `MODFREQ`, `MOD_AMT` are unused and already routed to `ParamId`s the engine can claim. So every item below fits without modifier layers.

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

(Tempo stays on TAP / tap-hold+MODFREQ_A as a transport gesture — see [README](../engines/README.md#clock-control-player-facing).)

---

## Roadmap

- **P1 — control remap + free controls.** DONE: variable-length `CPattern`, SIZE→length, ENV→rotation, MOD_AMT→randomness, MODFREQ→division, polymeter, render over the active length.

- **P2 — voice depth.** DONE: PITCH→noise bandpass; **5 selectable drum models** (kick/snare/clap/hat/ tom) live via Alt+PITCH (`CapAux`/`ParamId::Aux`); the Clap is a **multi-burst** voice (re-arms the amp env 3× ~11 ms apart); a model change briefly shows the **model number** (model+1 white dots) on the ring. **Voice rework (synthesis depth):** independent body/noise decays, per-model pitch-envelope time, a high-passed hat (metallic, vs the old narrow band-pass), an optional second detuned body partial (snare/tom), gentle body saturation, an attack click (kick beater / tom mallet), and a per-model `base_scale` so each drum tunes to its own range. The whole kit is voiced from one `kModels` table; host-checked finite/bounded/audible per model + a hat-vs-kick brightness assertion, but the **voicing values are first-principles starting points pending an on-hardware listen** (the table is the tuning surface). **Live performance controls:** SOS is now per-drum **gain** (decay moved to `grit+SOS`), and the grit/flux modifiers expose four per-drum **timbre macros** (drive, pitch-sweep, brightness, body↔noise) as bipolar offsets on the model baseline — see [Live sound macros](../engines/edrums.md#live-sound-macros). **Preset persistence:** the whole kit auto-saves to QSPI and reloads at boot - see [Presets](../engines/edrums.md#presets-qspi-auto-persist) (built, pending hardware verification). Still open: per-voice accent/velocity; an independent body-vs-noise snap decay (needs a sixth modifier channel); multiple named presets/banks on SD.

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
