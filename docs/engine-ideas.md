# Engine ideas

A scratchpad of candidate engines for the Spotykach platform. Each entry is a hypothesis: a DSP idea mapped onto the fixed hardware (dual deck, 7 knobs per deck, pads, transport, CV/gate, MIDI, SD) through the `IEngine` contract. Nothing here is committed; the point is to rank fit and cost before anyone writes a `*_engine.cpp`.

Companion reading: `docs/engines/README.md` (the contract + the shared knob vocabulary), `docs/architecture.md` (platform/engine seam, memory model). Many of the DSP building blocks referenced below already exist as offline C++ in the sibling project [nanodsp](https://github.com/shakfu/nanodsp) (signalsmith-dsp, DaisySP, STK, madronalib, fxdsp, vafilters) — that catalogue is the main idea source. The caveat throughout: nanodsp is offline float32 Python over large buffers; this platform is hard-real-time on a 480 MHz Cortex-M7 with a 96-sample block and no heap on the audio path. An idea being "in nanodsp" proves the algorithm, not that it fits the budget. The feasibility notes call out where that gap bites.

---

## What makes a good engine here

The platform is opinionated. An engine that fights its shape is awkward to play; one that leans into it gets a polished instrument almost for free. The recurring affordances:

- **Two of everything.** Two decks, two knob banks, two rings, two Rev/Seq/Fx pad sets, a crossfader between them. The strongest ideas are *natively two-voiced*: A and B are two synth voices, two delay lines, two filters, or carrier-vs-modulator. `edrums` even folds four drums into the two decks via Rev-pad focus swap (`take_param_reseed`) — that pattern generalises to any "more voices than decks" engine.

- **A shared clock.** The transport (`ITransport`) is a platform service every engine can *read* (tempo) or *subscribe* to (ticks). Anything rhythmic — sequenced synth, tempo-synced modulation, arpeggiator, rhythmic delay — gets bar-locked sync, MIDI/CV clock-in, and tap tempo for free (`CapTransport`).

- **Seven continuous controls per deck, plus an Alt selector.** The default knob column gives `Pos/Env/Size/Speed/ModSpeed/ModAmp/Mix` with no modifier; `CapAux` claims Alt+PITCH as a categorical selector (edrums uses it for drum-model select). That is enough for a fairly deep voice without inventing new gestures.

- **CV/gate + MIDI note in.** `cv_voct`, `handle_midi_note`, `on_gate_trigger` make pitched/triggered engines (synth voices, physical models) playable from a modular rig or keyboard. `process_cv` gives two CV/LFO *outputs* back to the rack.

- **Own display.** `CapOwnDisplay` lets an engine paint the rings/LEDs directly (`DisplayModel`) instead of the granular `*_leds` query path — the right choice for any non-granular engine.

The bare-metal budget (see [Feasibility](#feasibility-the-shared-constraints)) rewards cheap-per-sample DSP: one-pole filters, biquads, delay lines, table oscillators, Karplus-Strong. It punishes per-block FFTs and long convolutions unless they are kept small and amortised.

---

## Candidates at a glance

| # | Engine | One-liner | Native 2-voice? | Transport | Primary DSP source | Cost |
|---|---|---|---|---|---|---|
| 1 | **Resonator / Pluck** | dual Karplus-Strong voice; reel/slice/drift switch applies continuous / plucked / scattered excitation | yes (2 voices, 4-6 via focus) | per-mode (strum/drift) | DaisySP `String`/`Pluck`, STK `Plucked`/modal | low |
| 2 | **Ladder synth** | dual VA voice: band-limited osc + Moog ladder + env, sequenced | yes (2 voices) | yes (seq) | vafilters / DaisySP `MoogLadder`, PolyBLEP | low-med |
| 3 | **West-coast** | complex osc: wavefold + lowpass-gate, FM between the two decks | yes (2 osc) | optional | DaisySP `Oscillator`, fxdsp wavefold, DaisySP `Lpg` | low |
| 4 | **Vocoder** | deck A carrier x deck B modulator, channel-bank or formant | inherent (A/B roles) | no | nanodsp `vocoder`/`formant_filter` over [signalsmith](https://github.com/shakfu/nanodsp/tree/main/thirdparty/signalsmith) biquads | medium |
| 5 | **Shimmer reverb** | FDN reverb with pitch-shifted feedback; dual = two spaces / send | yes (2 sends) | optional (gate) | [madronalib](https://github.com/shakfu/nanodsp/tree/main/thirdparty/madronalib) FDN | medium |
| 6 | **Frequency shifter** | single-sideband shift / barber-pole, ring-mod, tempo-LFO sweep | yes (2 shifters) | optional (LFO) | [fxdsp](https://github.com/shakfu/nanodsp/tree/main/thirdparty/fxdsp) `FreqShifter`, `RingModulator` | low |
| 7 | **Tape echo** | wow/flutter + saturation + bitcrush over a synced delay | yes (2 lines) | yes (sync) | nanodsp `tape_echo`/`lo_fi` over [DaisySP](https://github.com/shakfu/nanodsp/tree/main/thirdparty/DaisySP) delay | low-med |
| 8 | **Drone bank** | additive/modal oscillator bank, slow evolving pad | yes (2 banks) | optional (LFO) | STK modal, madronalib generators | low-med |
| 9 | **Euclidean delay** | multi-tap delay whose taps sit on a Euclidean grid | yes (2 lines) | yes (subscribe) | `dsp/cpattern` + delay line | low |
| 10 | **Spectral freeze** | STFT freeze/blur of the live input, pad = capture | yes (2 freezes) | no | [signalsmith](https://github.com/shakfu/nanodsp/tree/main/thirdparty/signalsmith) STFT | high |
| 11 | **Grain cloud** | polyphonic grain cloud (per-grain pitch/pan/position), the granular the looper *isn't* | yes (2 clouds) | optional | GrainflowLib | med-high |
| 12 | **Radio** *(shipped)* | dual virtual RadioMusic: two SD radios, free-running playhead, raw 16-bit stations | yes (2 radios) | no | SD streaming + raw codec | low |

"Cost" is rough per-sample CPU + memory + integration effort, low to high. Tiers below group them.

> **Shipped:** #12 **Radio** is implemented (`ENGINE=radio`) - it reuses the tape engine's `SPK_USE_STREAM` > streaming stack and adds a headerless raw-16-bit codec, a directory scan, and the free-running virtual > playhead. See [`docs/engines/radio.md`](engines/radio.md).

---

## Tier A — cheap, high-fit, build these first

### 1. Resonator / Pluck (string-model voice, three modes)

**Concept.** One pitched physical-model voice per deck, built on a single shared kernel — a delay-line + one-pole-damping loop (Karplus-Strong / digital waveguide). The same kernel covers a *fed* resonant body and a *plucked* string; only how the loop is excited and read differs. So rather than two engines, this is **one engine whose three voicing modes are chosen by the reel/slice/drift switch** — the 3-position L/C/R mode selector that sits idle on every non-granular engine (edrums and `delay` already repurpose it). The switch picks the *excitation behaviour*; the existing `Aux` model-select (string / mallet / bell / bar / membrane) picks the *material*, orthogonally. Two axes over one cheap kernel — a richer instrument than either source idea alone, for the same DSP.

The three modes are not a metaphor for the switch's labels — they *are* those labels, applied one layer up. Granular reels/slices/drifts a *recorded buffer*; this engine reels/slices/drifts the *excitation feeding the string*. Same three playback paradigms (continuous tape / discrete sampler / evolving granular), same mono/poly/texture character they already imply, only the object changes from buffer to exciter:

**Reel — sympathetic body (continuous excitation).** The "play it by feeding audio" idea: the **live audio input** (or an internal noise/bow source) drives the loop continuously, so the string rings as long as energy flows in — a tuned resonant body, the Spotykach as a pair of sympathetic strings. Monophonic per deck (as Reel is mono); two decks = two sympathetic strings tuned to a chord, fed any signal.

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | tune (also `cv_voct` / MIDI note) |
| SIZE | `Size` | ring time / loop Q (decay) |
| POS | `Pos` | excitation position / brightness |
| ENV | `Env` | excitation source (internal noise/bow <-> live input) |
| MOD_AMT | `ModAmp` | body resonance / inharmonicity |
| MODFREQ | `set_mod_speed` | vibrato rate |
| SOS | `Mix` | dry/wet (input vs resonator) |
| Alt+PITCH | `Aux` | material: string / mallet / bell / bar / membrane |

**Slice — pluck / strum (triggered, polyphonic).** The extended Karplus-Strong (Jaffe-Smith EKS) pluck: a filtered noise/impulse burst into the loop, with a **pick-position comb** for the characteristic notch, a pluck-hardness lowpass, and a dynamic-level filter so harder plucks are brighter. Discrete plucks from the Rev pad / gate / MIDI note; up to 3-voice (as Slice is up-to-3 poly), or 4-6 via the edrums focus-swap. "Independent pitch" carries over from granular Slice but means something specific here: each voice holds *its own* pitch, set per-trigger — by the MIDI note, a per-step value, or CV at trigger time — not by a polyphonic knob. The `PITCH` knob sets the tonic/transpose for the manually-played or focused voice; the others retain the pitch they were struck at, so a held chord or a running arp keeps distinct notes while you retune the front voice. A transport-locked **strum/arpeggiate** rolls the decks; per-string detune gives a 12-string/chorus shimmer.

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | pitch (also `cv_voct` / MIDI note) |
| SIZE | `Size` | decay / sustain (loop damping) |
| POS | `Pos` | pick position (comb notch) |
| ENV | `Env` | pluck hardness (dynamic brightness) |
| MOD_AMT | `ModAmp` | per-string detune (12-string) + inharmonicity / stiffness |
| MODFREQ | `set_mod_speed` | strum / arp rate (transport-synced) |
| SOS | `Mix` | mix / string level |
| Alt+PITCH | `Aux` | single / strum / arp; string set (nylon / steel / harp / koto) |

**Drift — scatter / modal (evolving texture).** The granular analogue, in the excitation domain: tiny re-plucks **sprayed in time** with randomized pitch/pan/timing (grains *of excitation* rather than of a buffer), or a high-Q **modal bank**, slowly drifting under the LFO/CV machinery. Two decks = two evolving resonance clouds, crossfade-morphed — the ambient/drone use-case, folding in the spirit of the old "drone bank" idea while staying on the string-model kernel.

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | root pitch |
| SIZE | `Size` | spread / inharmonicity |
| POS | `Pos` | partial tilt / scatter centre |
| ENV | `Env` | re-excite window / grain duration |
| MOD_AMT | `ModAmp` | randomization depth (pitch + pan spray) |
| MODFREQ | `set_mod_speed` | drift / re-excite density (transport-syncable) |
| SOS | `Mix` | level / wet |
| Alt+PITCH | `Aux` | scale-chord, or bank type: modal vs scattered-pluck; material |

**Why fold the two ideas.** All three modes are the *same* delay-line + one-pole loop; only the excitation front-end changes (continuous input, pick burst, sprayed re-trigger). That is the opposite of the second-granular case (#11), where the cloud needed a genuinely different architecture and so stays a separate engine. Here a single engine is strictly less code than two, and it claims an otherwise-idle top-level gesture (the mode switch). Karplus-Strong is the cheapest expressive voice there is, natively pitched, so `cv_voct` / `handle_midi_note` make all three modes playable from a modular rig or keyboard with no extra UI.

**Legend — why the silkscreen stays honest.** The L/C/R silkscreen reads "Reel/Slice/Drift", and here those words are *literally correct*, not a borrowed mnemonic: each names the playback paradigm applied to the string's excitation — continuous (Reel), triggered (Slice), scattered/granular (Drift). So the printed word, the only physically fixed element, still describes what the switch does. The colour legend (yellow/blue/purple) is not fixed — it is the engine-driven WS2812 mode LED (`route()` already reports the position), so it can be repainted per mode; but since position (L/C/R) already carries the identity and those hues act as a cross-engine position constant, leaving them is the cheaper, more coherent choice. Optionally flash a one-shot mode glyph on the ring at switch-time (the edrums model-number readout pattern) for confirmation. Capabilities are static per engine, so advertise the union and let each mode ignore what it does not use.

**DSP (shared).** DaisySP `String` / `Pluck` (in the pinned `bleeptools` fork) or STK `Plucked` / `Twang` / `Modal` (wrapped in nanodsp over [DaisySP](https://github.com/shakfu/nanodsp/tree/main/thirdparty/DaisySP) / [STK](https://github.com/shakfu/nanodsp/tree/main/thirdparty/stk)). Loop damping = one-pole; fine tuning = a one-section fractional-delay allpass; pick-position comb = a single feedforward delay (Slice); body resonance = 1-2 biquads (`dsp/biquad` exists); sympathetic coupling = sum a fraction of one deck's loop into the other. Only the excitation front-end changes per mode; everything is per-sample-cheap.

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux | CapTransport`. **Cost: low.** Strongest first candidate — minimal DSP, immediately expressive, reuses the edrums `Aux` model-select seam *and* the idle mode switch, and folds two roadmap ideas into one engine with a free 3-way gesture.

### 3. West-coast complex oscillator

**Concept.** Each deck is a "complex oscillator" in the Buchla lineage: a primary band-limited oscillator whose output is run through a **wavefolder** and a **low-pass gate** (LPG, a vactrol-style combined VCF/VCA with a natural plucky decay). The two decks cross-modulate: deck A's oscillator FMs deck B (and/or vice versa) for clangorous metallic tones. Trigger the LPGs from the Rev pad, a gate input, or the transport.

**Why it fits.** Two oscillators is the canonical complex-oscillator pair; the platform's two decks map onto it 1:1, and the crossfader becomes the timbre/balance control. CV/gate and the transport drive the LPGs rhythmically. All the DSP is cheap and per-sample.

**DSP.** DaisySP `Oscillator` (band-limited) or PolyBLEP; [fxdsp](https://github.com/shakfu/nanodsp/tree/main/thirdparty/fxdsp) antialiased `wavefold` (1st/2nd-order antiderivative — important, naive folding aliases badly); DaisySP `Lpg`. FM is a single multiply-add into the phase increment.

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | oscillator pitch (CV/MIDI) |
| SIZE | `Size` | wavefold amount (timbre) |
| POS | `Pos` | waveshape / symmetry |
| ENV | `Env` | LPG decay time |
| MOD_AMT | `ModAmp` | cross-FM depth (this deck -> other) |
| MODFREQ | `set_mod_speed` | FM ratio |
| SOS | `Mix` | LPG response (VCF<->VCA blend) / level |
| Alt+PITCH | `Aux` | trigger source (Rev / gate / transport division) |

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux | CapTransport`. **Cost: low.** Distinctive, modular-friendly, and the inter-deck FM is a genuinely novel use of having two decks.

### 6. Frequency shifter / barber-pole

**Concept.** A true single-sideband frequency shifter (not pitch shift — it shifts every partial by a fixed *Hz*, breaking harmonicity for metallic/clangy and slow-phasing effects). Small shifts (<5 Hz) give lush phasing; large shifts give ring-mod-like timbres. A "barber-pole" mode uses a tempo-synced LFO over the shift for an endless-glide illusion. Deck A and B shift independently (e.g. opposite directions for a widening effect).

**Why it fits.** Cheap (a Hilbert transform via an all-pass network + a complex multiply by an oscillator), processes the live input so it needs no synth, and the two decks give a natural stereo/dual treatment. Transport sync makes the barber-pole LFO musical.

**DSP.** [fxdsp](https://github.com/shakfu/nanodsp/tree/main/thirdparty/fxdsp) `FreqShifter` and `RingModulator` (wrapped by nanodsp's `freq_shift` / `ring_mod`). The Hilbert is an IIR all-pass pair — a handful of biquad-cost sections per sample.

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | shift amount (bipolar, centre = 0 Hz) |
| SIZE | `Size` | shift range / coarse-fine |
| SOS | `Mix` | dry/wet |
| MODFREQ | `set_mod_speed` | barber-pole LFO rate (transport-syncable) |
| MOD_AMT | `ModAmp` | LFO depth |
| POS | `Pos` | feedback (regeneration -> spiralling) |
| ENV | `Env` | ring-mod blend (SSB <-> AM) |
| Alt+PITCH | `Aux` | mode: SSB up / SSB down / barber / ring |

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux | CapTransport`. **Cost: low.** A lot of distinctive character for very little code; pairs well as an "effect engine" alongside the synth voices.

---

## Tier B — moderate cost, strong instruments

### 2. Ladder synth (subtractive voice)

**Concept.** The classic subtractive voice: band-limited oscillator(s) -> virtual-analog Moog ladder filter -> amp envelope, one per deck (so paraphonic two-note, or a bass on A and a lead on B). It *sequences off the transport* the way edrums does — each deck runs a short step sequencer (reuse the `CapStepSequencer` pads + `dsp/cpattern` or a pitch-per-step grid), giving a self-contained acid/sequence box.

**Why it fits.** Reuses two existing platform investments: the step-sequencer pad layer and the transport tick subscription. The VA filter's self-oscillating resonance is the headline control. CV/MIDI in for live playing when the sequencer is off.

**DSP.** [vafilters](https://github.com/shakfu/nanodsp/tree/main/thirdparty/vafilters) Moog/Diode ladder (Faust-derived, wrapped in nanodsp) or DaisySP `MoogLadder`/`Svf`; PolyBLEP or DaisySP oscillators for the alias-free saw/square; DaisySP `Adsr`. All per-sample-cheap; the only watch-item is the ladder's oversampling if you want clean high resonance.

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | oscillator tune / sequence transpose |
| SIZE | `Size` | filter cutoff |
| POS | `Pos` | resonance |
| ENV | `Env` | filter envelope amount |
| SOS | `Mix` | amp decay / accent |
| MOD_AMT | `ModAmp` | osc detune / sub-osc / waveshape |
| MODFREQ | `set_mod_speed` | LFO-to-cutoff (transport-syncable) |
| Alt+PITCH | `Aux` | osc waveform / filter model (Moog/Diode/Korg35) |

**Capabilities.** `CapDualDeck | CapStepSequencer | CapTransport | CapOwnDisplay | CapAux | CapLaunchQuant`. **Cost: low-med.** The most "instrument-like" of the set; biggest effort is the per-deck pitch sequencer UI, but the transport + seq-pad scaffolding already exists.

### 7. Tape echo / lo-fi

**Concept.** A delay engine like the existing `delay`, but the wet path is run through a **tape model**: wow/flutter pitch modulation, soft saturation, bandwidth limiting, and optional bitcrush — the Space-Echo / lo-fi-dub character the clean delay deliberately omits. Two synced lines (A->L, B->R) with cross-feedback for ping-pong.

**Why it fits.** Direct evolution of `delay` (which already borrows SDRAM, tempo-syncs, and has a pitch shifter on the wet tap). The new parts are cheap modulators and a saturator on a signal that already exists. The `delay` doc itself lists "ping-pong / cross-feedback" and "lo-fi mode" as wanted.

**DSP.** Reuse the delay line + pitch shifter already in `delay_engine`; add fxdsp/DaisySP saturation, a slow random LFO for wow/flutter (modulate the read pointer), DaisySP `bitcrush`/sample-rate reduce. nanodsp's [`tape_echo`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) and [`lo_fi`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) are the reference chain (recipe), composed over [DaisySP](https://github.com/shakfu/nanodsp/tree/main/thirdparty/DaisySP) + [fxdsp](https://github.com/shakfu/nanodsp/tree/main/thirdparty/fxdsp).

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| SIZE | `Size` | division (synced) / free time on Alt |
| POS | `Pos` | feedback (with cross-feed mode) |
| SOS | `Mix` | wet/dry |
| PITCH | `Speed` | wet pitch (as in `delay`) |
| MOD_AMT | `ModAmp` | wow/flutter depth |
| MODFREQ | `set_mod_speed` | flutter rate |
| ENV | `Env` | tape age (saturation + LP bandwidth + crush) |
| Alt+PITCH | `Aux` | sync/free, ping-pong on/off |

**Capabilities.** `CapDualDeck | CapTransport | CapOwnDisplay | CapAux`. **Cost: low-med.** High value-per-effort because it forks the working `delay` engine rather than starting clean.

### 4. Vocoder

**Concept.** The two decks take on *roles* rather than being symmetric voices: deck A is the **carrier** (an internal oscillator/noise, or input-left), deck B is the **modulator** (input-right — voice, drums, anything). A bank of bandpass filters analyses B's spectral envelope and imposes it on A. The crossfader blends dry-carrier to vocoded.

**Why it fits.** A clean, classic use of two decks as two signal roles, and the per-deck knob banks naturally split into "carrier shaping" (A) and "modulator/analysis" (B). Plays the live stereo input.

**DSP.** nanodsp [`vocoder`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) / [`formant_filter`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) are the reference (recipe), built from [signalsmith](https://github.com/shakfu/nanodsp/tree/main/thirdparty/signalsmith) biquads; on-target it is N bandpass biquad pairs (analysis + synthesis) plus envelope followers — 12-16 bands is a few hundred biquads per sample, the heaviest of Tier B but within an H7 block if band count is tuned.

**Control map.** Deck A (carrier): PITCH = osc pitch, SIZE = carrier waveform/noise mix, POS = carrier brightness. Deck B (modulator): SIZE = band count/resolution, POS = formant shift, ENV = envelope-follower attack/release, SOS = sibilance/high-band passthrough. Crossfader = dry/vocoded.

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux`. **Cost: medium** (band count is the CPU dial). The standout "two roles, not two voices" engine.

### 5. Shimmer reverb (FDN)

**Concept.** A feedback-delay-network reverb with a **pitch shifter inside the feedback loop**, so the tail blooms upward an octave (the Eno/shimmer sound). Dual deck = either two independent spaces (A small/plate, B cathedral) summed, or A = reverb send and B = its modulation/freeze.

**Why it fits.** A lush "always-on" texture engine that processes the live input. An optional gated mode (`CapTransport`) syncs the gate length to tempo for rhythmic-reverb / gated-verb effects.

**DSP.** madronalib FDN presets (room/hall/plate/chamber/cathedral) are the reference; on-target an 8x8 Hadamard FDN with damping one-poles + a crossfading pitch-shift tap in the loop. Memory: a few hundred ms of delay lines in SDRAM. nanodsp [`shimmer_reverb`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) / [`gated_reverb`](https://github.com/shakfu/nanodsp/blob/main/src/nanodsp/effects/composed.py) are the composed references (recipe), built on [madronalib](https://github.com/shakfu/nanodsp/tree/main/thirdparty/madronalib) FDN.

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| SIZE | `Size` | decay / RT60 |
| POS | `Pos` | pre-delay |
| ENV | `Env` | damping (HF rolloff) |
| MOD_AMT | `ModAmp` | shimmer amount (pitched feedback) |
| MODFREQ | `set_mod_speed` | shimmer interval (+oct / +5th) / modulation rate |
| SOS | `Mix` | wet/dry |
| PITCH | `Speed` | size-shift / diffusion |
| Alt+PITCH | `Aux` | preset (room/plate/hall/cathedral), gated mode |

**Capabilities.** `CapDualDeck | CapTransport | CapOwnDisplay | CapAux`. **Cost: medium.** Memory-bound more than CPU-bound; the pitch-shift-in-loop is the one fiddly part.

### 8. Drone bank (additive / modal)

**Concept.** Each deck is a bank of detuned partials — an additive oscillator stack or a set of high-Q modal resonators — forming a slowly evolving drone/pad. Per-deck LFOs (the existing modulation infrastructure + `process_cv` outputs) drift the partials. Two decks = two stacked chords; the crossfader morphs between them.

**Why it fits.** Leans on the modulation/LFO and CV-out machinery already in the platform, is pitched (CV/MIDI), and the "two evolving textures + morph" maps onto the dual deck + crossfader perfectly.

**DSP.** STK modal voices or a bank of `dsp/lutsinosc` partials (the sine table already exists); madronalib generators for the slow modulation. Cost scales with partial count — 8-16 per deck is comfortable.

**Control map (per deck).** PITCH = root, SIZE = spread/inharmonicity, POS = partial tilt (which partials dominate), ENV = attack/release, MOD_AMT/MODFREQ = drift depth/rate, SOS = level, Alt+PITCH = chord/scale or bank type (additive vs modal).

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux`. **Cost: low-med.** A natural companion to the resonator; together they cover the "ambient" use-case.

### 9. Euclidean delay

**Concept.** A bridge between `delay` and `edrums`: a multi-tap delay whose taps are placed on a **Euclidean grid** locked to the transport, so echoes arrive in a distributed rhythmic pattern rather than at even intervals. Each deck has its own pattern (length/density/rotation), giving polyrhythmic, evolving repeats.

**Why it fits.** Recombines two things the codebase already has — the `delay` line and `dsp/cpattern` (the edrums Euclidean generator) — and *subscribes* to transport ticks exactly as edrums does. Low novelty risk because both halves are proven.

**Control map (per deck).** SIZE = pattern length, POS = density (number of taps), ENV = rotation, MODFREQ = base division, MOD_AMT = feedback, SOS = mix, PITCH = per-tap pitch smear, Alt+PITCH = tap envelope / reverse taps.

**Capabilities.** `CapDualDeck | CapStepSequencer | CapTransport | CapOwnDisplay | CapAux`. **Cost: low.** Mostly integration of existing parts; good "combine two working things" project.

---

### 11. Grain cloud (GrainflowLib)

**Concept.** A *second* granular engine — but a genuinely different model from the existing one. The shipping `granular` engine, despite its name, is a **dual tape-loop scanner**: even its cloud-like Drift mode runs a single voice with up to six overlapping windows that **share one pitch, one start position, one speed** (the "spray" is start-position jitter only). It cannot do per-grain pitch, per-grain pan, or dense polyphony. A grain-cloud engine fills exactly that gap: dozens of independent grains, each with its own pitch/transpose, buffer position, pan, duration, direction, and per-grain randomization — a synthesizer-style grain cloud rather than a looper. Two decks = two clouds (or one cloud + its modulator), morphed by the crossfader.

**Why a second granular is justified.** The two are different instruments, not variants. Anything else here would be a tape looper; this is the one engine that turns a recorded buffer into a true polyphonic cloud — scattering grains with independent transposition and spatialisation, glisson (per-grain pitch glide), and probabilistic density. It reuses the platform's recording/SD/buffer machinery (`CapRecording | CapTapeStorage`) the same way granular does, so the capture side is solved.

**DSP source.** [GrainflowLib](https://github.com/shakfu/nanodsp/tree/main/thirdparty/GrainflowLib) — a header-only C++17 grain-cloud engine (~3,380 lines, 12 headers). Architecture: a `gf_grain_collection<T, Blocksize, SigType>` owns a fixed array of `gf_grain` instances distributed across streams; each grain resets on the zero-crossing of an external grain-clock signal and reads its source via a traversal phasor, with sample-accurate FM (pitch) and AM (amplitude) inputs. Per-grain parameters (`gf_param_name`, ~23 of them): `rate`/`transpose`, `glisson`, `delay`, `window`, `space`, `density`, `direction`, `amplitude`, `start_point`/`stop_point`, `vibrato_*`, `stream`, each with `base + random + offset·grain_id`. This is the model the existing engine lacks.

**Control map (per deck = per cloud).**

| Knob | `ParamId` | Function |
|---|---|---|
| POS | `Pos` | cloud centre position in the buffer |
| SIZE | `Size` | position spray (spread of grain start points) |
| PITCH | `Speed` | grain transpose (centre = unity; CV/MIDI) |
| ENV | `Env` | grain duration / window |
| MODFREQ | `set_mod_speed` | grain density (grains/sec; transport-syncable) |
| MOD_AMT | `ModAmp` | pitch spray + pan spray (per-grain randomization depth) |
| SOS | `Mix` | dry/wet (or grain count / cloud level) |
| Alt+PITCH | `Aux` | direction (fwd/back/random) / glisson / quantize-to-semitone |

**Capabilities.** `CapRecording | CapTapeStorage | CapDualDeck | CapOwnDisplay | CapAux` (+ `CapTransport` if grain density locks to tempo).

**Cost: medium-high.** The DSP loop is heap-free and STL-free in `process()`, which is the important part. The porting work and risks:

- **Pre-allocate, don't resize.** The grain array (`std::unique_ptr<gf_grain[]>`) and a per-grain `std::unique_ptr<phasor>` vibrato must become fixed at `init()` (max grain count, `Blocksize = 96`). One `throw()` in `param_set` to remove (`-fno-exceptions`). Verify `std::atomic<bool>` is lock-free on the M7 (it is).

- **Cut the `AudioFile.h` dependency** and feed the existing SDRAM buffer through the `gf_i_buffer_reader` seam (it exists for exactly this — clean).

- **The real gate is SDRAM access, not CPU.** A cloud does *scattered* reads (each grain reads a different buffer position with interpolation), the opposite of the looper's few contiguous playheads. The H7's SDRAM rewards bursts and punishes random access; N grains scattering reads per 96-sample block is the make-or-break number, and the desktop profile does not tell you it. **Benchmark N scattered grain reads/block headless in `host/` before committing** — that single measurement decides feasibility and the max grain count.

**Alternative framing — extend instead of import.** Rather than port GrainflowLib wholesale (template-heavy, an external model to maintain), give the existing `Window` per-window pitch/pan/position and let Drift spawn many of them. That recovers ~80% of the cloud character, stays in the in-house Vox/Window idiom, and reuses everything already there — at the cost of glisson, multi-stream spatialisation, and the rich randomization model. **Decision rule:** port GrainflowLib if the goal is a deep, distinct cloud instrument *and* the SDRAM benchmark passes; extend the existing Vox/Window if you want cloud-ish texture cheaply. Either way the first step is the same benchmark.

## Tier C — ambitious / research

### 10. Spectral freeze and smear

**Concept.** An STFT engine over the live input: capture the current spectral frame (Rev pad / gate) and sustain it indefinitely as a frozen pad, with spectral blur, partial randomisation, and a movable spectral filter. Two decks = two frozen layers, crossfaded — drone construction from any input. This is the spectral analogue of the granular freeze.

**Why it fits the *concept*, with caveats.** Hugely expressive and on-brand for a sampling/looping instrument, and the two-deck + crossfader morph is ideal. The caveat is purely budgetary (below).

**DSP.** [signalsmith-dsp](https://github.com/shakfu/nanodsp/tree/main/thirdparty/signalsmith) STFT (the backbone of nanodsp's spectral module): overlap-add, phase handling, freeze, blur. The H7 has the FFT muscle (CMSIS-DSP / ARM FFT) for, say, a 512/1024-point real FFT at modest overlap, but it must be fit into the 96-sample block budget with care — STFT is block-based and bursty, the opposite of the steady per-sample DSP the rest of the engines use.

**Cost: high.** The real work is engineering an FFT that coexists with a 96-sample/2 ms audio block without overrunning — likely processing one hop across several audio blocks, double-buffered in SDRAM. Worth prototyping headless (`host/`) first. Treat as a milestone, not a quick win.

---

## Feasibility: the shared constraints

Every idea above is gated by the same platform realities (see `docs/architecture.md`):

- **Hard real-time, no heap on the audio path.** `process()` runs in the audio ISR on a 96-sample block (~2 ms at 48 kHz). All buffers come from the injected SDRAM `arena` at `init()`; nothing allocates per block. This favours fixed-size DSP (delay lines, filter banks, oscillators) and rules out anything that wants to `malloc` mid-stream.

- **CPU budget is per-sample-cheap, not per-block-cheap.** One-pole filters, biquads, table oscillators, Karplus-Strong loops, and Hadamard FDNs are comfortable. The expensive engines are the band-heavy (vocoder) and the block-bursty (spectral) — both need their work-per-block explicitly bounded and measured.

- **SDRAM, not unlimited.** `EngineBuffers` is currently granular-shaped; a non-granular engine sub-allocates from the engine arena the way `delay` does (it borrows ~6 s of delay line). Reverb tails, multi-tap delays, and STFT double-buffers all live here and compete — budget memory up front.

- **The contract grows reluctantly.** edrums added exactly one method (`take_param_reseed`) to support four voices. Prefer ideas expressible through the existing `IEngine` surface (params, `Aux`, pads, CV, transport, `render`); if an idea needs a new contract method, that is a design cost to weigh, not a free move.

- **Validate headless first.** The `host/` harness runs engines off-target (e.g. `make -C host test-edrums`). Any non-trivial DSP — especially the vocoder and spectral engines — should be proven there (finite output, correct response) before it touches hardware.

A practical implication: prototype the DSP in nanodsp (offline, fast to iterate, already has these algorithms), confirm the sound, *then* port the kernel to a fixed-point/fixed-buffer real-time form here. nanodsp is the lab; sk-engines is the instrument.

---

## Suggested order

1. **Resonator / Pluck (#1)** — lowest cost, immediately expressive, reuses the `Aux` model-select seam *and* the idle reel/slice/drift mode switch. Best proof that the platform hosts a pitched physical-model voice. Build the **Slice** (pluck/strum) mode first as the headline; **Reel** (sympathetic body) and **Drift** (scatter/modal) then reuse the same loop kernel for almost nothing — three voicing modes over one cheap DSP core.

2. **West-coast (#3)** or **Frequency shifter (#6)** — both cheap and distinctive; #3 if you want a synth, #6 if you want an input effect.

3. **Tape echo (#7)** — forks the working `delay` engine, so high value per effort.

4. **Ladder synth (#2)** — the most complete standalone instrument once the sequencer UI is in.

5. **Spectral freeze (#10)** — only after the cheap wins land, as a deliberate research milestone.

These are hypotheses to shoot down, not a roadmap. The open question for each is the same: does it play well within the two-deck / seven-knob / shared-clock grammar, or is it fighting it? The ones that fit (resonator, west-coast, vocoder's two-role split) are worth more than algorithmically fancier ideas that don't.
