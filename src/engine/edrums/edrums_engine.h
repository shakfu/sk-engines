// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "dsp/cpattern.h"
#include "dsp/lutsinosc.h"
#include "dsp/biquad.h"
#include "nocopy.h"

#include <cstddef>
#include <cstdint>

namespace spotykach {

// A dual Euclidean drum machine - the fourth engine, and the first to *sequence* off the shared
// platform transport (it subscribes to the clock's ticks rather than just reading tempo like the
// delay). Two independent drum tracks (deck A / deck B), each a Euclidean pattern (`dsp/CPattern`)
// driving a synthesized voice. Per-deck pattern length AND clock division make the two tracks run
// independently (polymeter); a transport grid reset realigns them to the bar.
//
// Audio is synthesized (no SD/samples): each Voice is a pitched sine body with a fast downward pitch
// sweep plus a band-passed noise component, shaped by an exponential amp envelope. Deck A defaults
// kick-ish (low, body), deck B snare/clap-ish (noise). The Voice is a small abstraction so sample
// playback could slot in later behind the same trigger()/process() seam.
//
// Knob map (the platform's per-deck direct knobs): POS = density (onsets), SIZE = length, ENV =
// rotation, PITCH = pitch + noise colour, SOS = decay, MOD_AMT = randomness (skip probability),
// MODFREQ = clock division. capabilities() = CapOwnDisplay | CapDualDeck.
class EdrumsEngine : public IEngine {
public:
    EdrumsEngine() = default;
    ~EdrumsEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override { return CapOwnDisplay | CapDualDeck | CapAux; }

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;
    void  set_mod_speed(DeckRef::Ref deck, float value, bool sync) override; // MODFREQ -> division
    bool  set_config(ConfigId id, DeckRef::Ref deck, int value) override;    // routing switch -> output mode
    Route route() const override { return _route; }                         // mode L/C/R LED

    void render(DisplayModel& m) override;

private:
    NOCOPY(EdrumsEngine)

    // One synthesized drum voice. process() runs per sample on the audio path; trigger() (from the
    // transport tick) just re-arms the envelopes, so it is allocation/lock free.
    struct Voice {
        LUTSinOsc osc;                 // pitched body
        infrasonic::BPF12 noise_bpf;   // band-pass that colours the noise (PITCH -> centre freq)
        float sr          = 48000.f;
        float base_hz     = 60.f;      // PITCH -> body frequency
        // Set by the drum model (set_model): body/noise mix, pitch-sweep amount, decay multiplier,
        // and the noise band centre as a multiple of base_hz.
        float tone        = 0.f;
        float sweep       = 3.f;
        float decay_scale = 1.f;
        float noise_mult  = 8.f;
        float decay_norm  = 0.5f;      // last SOS value, so set_model can recompute the decay
        float amp         = 0.f;       // current amp-env level (0 = idle)
        float amp_coef    = 0.f;       // per-sample amp-env decay multiplier
        float pitch_env   = 0.f;       // current pitch-sweep level
        float pitch_coef  = 0.f;       // per-sample pitch-sweep decay multiplier (fixed ~25 ms)
        uint32_t rng      = 0x1234567u;// LCG state for the noise component
        // Multi-burst (the Clap model re-arms the amp env a few times in quick succession).
        int   burst_count = 0;         // extra bursts after the first (model)
        float burst_gap   = 0.f;       // samples between bursts (model)
        int   burst_left  = 0;         // bursts still pending this hit
        float burst_timer = 0.f;       // samples until the next burst

        void  init(float sample_rate, float default_hz, int default_model, uint32_t seed);
        void  set_model(int idx);      // 0..4: kick / snare / clap / closed-hat / tom
        void  set_pitch(float norm);   // PITCH -> body freq + noise band centre
        void  set_decay(float norm);   // SOS   -> amp_coef (decay time)
        void  trigger();               // re-arm amp + pitch envelopes
        float process();               // render one sample
        void  _noise_center();         // noise band centre = base_hz * noise_mult
        void  _recompute_decay();      // amp_coef from decay_norm * the model's decay_scale
    };

    // One drum track = a Euclidean pattern + its voice + a per-track randomness RNG.
    struct Track {
        CPattern pattern;
        Voice    voice;
        uint32_t prob_rng = 0xC0FFEEu;
    };

    static DeckRef::Ref _safe(DeckRef::Ref d) { return d < DeckRef::Count ? d : DeckRef::A; }

    void  _on_tick(const TransportTick& e);   // transport sink: step patterns, trigger voices
    void  _apply_density(DeckRef::Ref d);     // re-derive onsets from the stored POS fraction + length
    static float _rand01(uint32_t& rng) { rng = rng * 1664525u + 1013904223u; return static_cast<float>(rng >> 8) * (1.f / 16777216.f); }

    static constexpr int     kModelCount      = 5;  // kick / snare / clap / closed-hat / tom
    static constexpr uint8_t kFlashFrames     = 6;  // play-LED hold (~render frames) after a hit
    static constexpr uint8_t kModelShowFrames = 45; // ring shows the model number for ~0.7 s after a change
    static constexpr uint8_t kDivTable[3] = { 1, 2, 4 }; // MODFREQ -> ticks/step: 1/16, 1/8, 1/4

    ITransport* _transport = nullptr;          // platform clock (subscribe + tempo), injected at init
    Track    _track[DeckRef::Count];
    Route    _route = Route::Stereo;            // routing switch: Stereo=mono-sum, DoubleMono=A|B split, Generative=random pan
    uint32_t _step_tick = 0;                    // shared step counter (reset on e.reset); div phase
    uint8_t  _div[DeckRef::Count]   = { 1, 1 };       // ticks per step per deck (MODFREQ)
    float    _prob[DeckRef::Count]  = { 1.f, 1.f };   // probability an onset fires per deck (MOD_AMT)
    float    _pan[DeckRef::Count]   = { 0.5f, 0.5f }; // per-deck pan, randomized per hit in Generative mode
    uint8_t  _flash[DeckRef::Count] = { 0, 0 };       // per-deck play-LED hit flash, decayed in render()
    uint8_t  _model[DeckRef::Count]      = { 0, 1 };   // current model index per deck (A=kick, B=snare)
    uint8_t  _model_show[DeckRef::Count] = { 0, 0 };   // frames remaining to show the model number on the ring
    float    _param[static_cast<size_t>(ParamId::Count)][DeckRef::Count] = {};
};

};
