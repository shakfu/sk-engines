# Engine ideas

A scratchpad of candidate engines for the Spotykach platform. Each entry is a hypothesis: a DSP idea mapped onto the fixed hardware (dual deck, 7 knobs per deck, pads, transport, CV/gate, MIDI, SD) through the `IEngine` contract. Nothing here is committed; the point is to rank fit and cost before anyone writes a `*_engine.cpp`.

Companion reading: `docs/engines/README.md` (the contract + the shared knob vocabulary), `docs/architecture.md` (platform/engine seam, memory model). Many of the DSP building blocks referenced below already exist as offline C++ in the sibling project `~/projects/personal/nanodsp` (signalsmith-dsp, DaisySP, STK, madronalib, fxdsp, vafilters) — that catalogue is the main idea source. The caveat throughout: nanodsp is offline float32 Python over large buffers; this platform is hard-real-time on a 480 MHz Cortex-M7 with a 96-sample block and no heap on the audio path. An idea being "in nanodsp" proves the algorithm, not that it fits the budget. The feasibility notes call out where that gap bites.

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
| 1 | **Resonator** | dual physical-model voice (string/mallet/modal), pluck or live-exciter | yes (2 voices) | optional (arp) | DaisySP `String`/`Pluck`, STK modal | low |
| 2 | **Ladder synth** | dual VA voice: band-limited osc + Moog ladder + env, sequenced | yes (2 voices) | yes (seq) | vafilters / DaisySP `MoogLadder`, PolyBLEP | low-med |
| 3 | **West-coast** | complex osc: wavefold + lowpass-gate, FM between the two decks | yes (2 osc) | optional | DaisySP `Oscillator`, fxdsp wavefold, DaisySP `Lpg` | low |
| 4 | **Vocoder** | deck A carrier x deck B modulator, channel-bank or formant | inherent (A/B roles) | no | nanodsp `vocoder`/`formant_filter` | medium |
| 5 | **Shimmer reverb** | FDN reverb with pitch-shifted feedback; dual = two spaces / send | yes (2 sends) | optional (gate) | madronalib FDN, nanodsp `shimmer_reverb` | medium |
| 6 | **Frequency shifter** | single-sideband shift / barber-pole, ring-mod, tempo-LFO sweep | yes (2 shifters) | optional (LFO) | fxdsp `frequency_shifter`, `ring_modulator` | low |
| 7 | **Tape echo** | wow/flutter + saturation + bitcrush over a synced delay | yes (2 lines) | yes (sync) | nanodsp `tape_echo`/`lofi`, DaisySP delay | low-med |
| 8 | **Drone bank** | additive/modal oscillator bank, slow evolving pad | yes (2 banks) | optional (LFO) | STK modal, madronalib generators | low-med |
| 9 | **Euclidean delay** | multi-tap delay whose taps sit on a Euclidean grid | yes (2 lines) | yes (subscribe) | `dsp/cpattern` + delay line | low |
| 10 | **Spectral freeze** | STFT freeze/blur of the live input, pad = capture | yes (2 freezes) | no | signalsmith STFT, nanodsp `spectral_freeze` | high |

"Cost" is rough per-sample CPU + memory + integration effort, low to high. Tiers below group them.

---

## Tier A — cheap, high-fit, build these first

### 1. Resonator (physical-model voice)

**Concept.** Each deck is one resonant voice — a plucked/struck string or a modal bar/bell — excited either by an internal impulse (Rev pad = pluck, or a Euclidean trigger off the transport) or, more interestingly, by the **live audio input** fed into the resonator's delay loop. The latter turns the instrument into a tuned resonant body you "play" by feeding it any signal: the Spotykach as a pair of sympathetic strings. Two decks = a two-note chord, or one string per channel.

**Why it fits.** Karplus-Strong is about the cheapest expressive voice there is (a delay line + one-pole damping filter). It is natively pitched, so `cv_voct` / `handle_midi_note` make it a playable modular/MIDI voice with no extra UI. Two decks are two voices without contortion; the edrums focus-swap trick could expand to a 4-note chord later.

**DSP.** DaisySP `String` / `Pluck` (already in the pinned `bleeptools` fork) or STK `Modal`/`Plucked` for the bell/bar timbres (proven in nanodsp's physical-modeling module). Excitation = noise burst, input audio, or impulse. Damping = one-pole in the loop. Optional body resonance = 1-2 biquads (`dsp/biquad` exists).

**Control map (per deck).**

| Knob | `ParamId` | Function |
|---|---|---|
| PITCH | `Speed` | pitch (also `cv_voct` / MIDI note) |
| SIZE | `Size` | decay / sustain (loop damping) |
| POS | `Pos` | excitation position / brightness (pluck point) |
| ENV | `Env` | excitation type (noise <-> input audio blend) |
| SOS | `Mix` | dry/wet (resonator vs input) |
| MOD_AMT | `ModAmp` | body resonance amount / inharmonicity |
| MODFREQ | `set_mod_speed` | vibrato / LFO-to-pitch rate |
| Alt+PITCH | `Aux` | model select (string / mallet / bell / bar / drum membrane) |

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux` (+ `CapTransport` if you add a built-in arp/euclidean trigger). **Cost: low.** Strongest first candidate — minimal DSP, immediately expressive, and `Aux` model-select reuses the exact seam edrums already proved.

### 3. West-coast complex oscillator

**Concept.** Each deck is a "complex oscillator" in the Buchla lineage: a primary band-limited oscillator whose output is run through a **wavefolder** and a **low-pass gate** (LPG, a vactrol-style combined VCF/VCA with a natural plucky decay). The two decks cross-modulate: deck A's oscillator FMs deck B (and/or vice versa) for clangorous metallic tones. Trigger the LPGs from the Rev pad, a gate input, or the transport.

**Why it fits.** Two oscillators is the canonical complex-oscillator pair; the platform's two decks map onto it 1:1, and the crossfader becomes the timbre/balance control. CV/gate and the transport drive the LPGs rhythmically. All the DSP is cheap and per-sample.

**DSP.** DaisySP `Oscillator` (band-limited) or PolyBLEP; fxdsp antialiased `wavefold` (1st/2nd-order antiderivative, in nanodsp's saturation module — important, naive folding aliases badly); DaisySP `Lpg`. FM is a single multiply-add into the phase increment.

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

**DSP.** fxdsp `frequency_shifter` and `ring_modulator` (both in nanodsp's composed/fxdsp modules). The Hilbert is an IIR all-pass pair — a handful of biquad-cost sections per sample.

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

**DSP.** vafilters Moog/Diode ladder (Faust-derived, in nanodsp) or DaisySP `MoogLadder`/`Svf`; PolyBLEP or DaisySP oscillators for the alias-free saw/square; DaisySP `Adsr`. All per-sample-cheap; the only watch-item is the ladder's oversampling if you want clean high resonance.

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

**DSP.** Reuse the delay line + pitch shifter already in `delay_engine`; add fxdsp/DaisySP saturation, a slow random LFO for wow/flutter (modulate the read pointer), DaisySP `bitcrush`/sample-rate reduce. nanodsp's `tape_echo` and `lofi` composed effects are the reference chain.

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

**DSP.** nanodsp `vocoder` / `formant_filter` reference; on-target it is N bandpass biquad pairs (analysis + synthesis) plus envelope followers — 12-16 bands is a few hundred biquads per sample, the heaviest of Tier B but within an H7 block if band count is tuned.

**Control map.** Deck A (carrier): PITCH = osc pitch, SIZE = carrier waveform/noise mix, POS = carrier brightness. Deck B (modulator): SIZE = band count/resolution, POS = formant shift, ENV = envelope-follower attack/release, SOS = sibilance/high-band passthrough. Crossfader = dry/vocoded.

**Capabilities.** `CapDualDeck | CapOwnDisplay | CapAux`. **Cost: medium** (band count is the CPU dial). The standout "two roles, not two voices" engine.

### 5. Shimmer reverb (FDN)

**Concept.** A feedback-delay-network reverb with a **pitch shifter inside the feedback loop**, so the tail blooms upward an octave (the Eno/shimmer sound). Dual deck = either two independent spaces (A small/plate, B cathedral) summed, or A = reverb send and B = its modulation/freeze.

**Why it fits.** A lush "always-on" texture engine that processes the live input. An optional gated mode (`CapTransport`) syncs the gate length to tempo for rhythmic-reverb / gated-verb effects.

**DSP.** madronalib FDN presets (room/hall/plate/chamber/cathedral) are the reference; on-target an 8x8 Hadamard FDN with damping one-poles + a crossfading pitch-shift tap in the loop. Memory: a few hundred ms of delay lines in SDRAM. nanodsp `shimmer_reverb` / `gated_reverb` are the composed references.

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

## Tier C — ambitious / research

### 10. Spectral freeze and smear

**Concept.** An STFT engine over the live input: capture the current spectral frame (Rev pad / gate) and sustain it indefinitely as a frozen pad, with spectral blur, partial randomisation, and a movable spectral filter. Two decks = two frozen layers, crossfaded — drone construction from any input. This is the spectral analogue of the granular freeze.

**Why it fits the *concept*, with caveats.** Hugely expressive and on-brand for a sampling/looping instrument, and the two-deck + crossfader morph is ideal. The caveat is purely budgetary (below).

**DSP.** signalsmith-dsp STFT (the backbone of nanodsp's spectral module): overlap-add, phase handling, freeze, blur. The H7 has the FFT muscle (CMSIS-DSP / ARM FFT) for, say, a 512/1024-point real FFT at modest overlap, but it must be fit into the 96-sample block budget with care — STFT is block-based and bursty, the opposite of the steady per-sample DSP the rest of the engines use.

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

1. **Resonator (#1)** — lowest cost, immediately expressive, reuses the `Aux` model-select seam. Best proof that the platform hosts a pitched physical-model voice.
2. **West-coast (#3)** or **Frequency shifter (#6)** — both cheap and distinctive; #3 if you want a synth, #6 if you want an input effect.
3. **Tape echo (#7)** — forks the working `delay` engine, so high value per effort.
4. **Ladder synth (#2)** — the most complete standalone instrument once the sequencer UI is in.
5. **Spectral freeze (#10)** — only after the cheap wins land, as a deliberate research milestone.

These are hypotheses to shoot down, not a roadmap. The open question for each is the same: does it play well within the two-deck / seven-knob / shared-clock grammar, or is it fighting it? The ones that fit (resonator, west-coast, vocoder's two-role split) are worth more than algorithmically fancier ideas that don't.
