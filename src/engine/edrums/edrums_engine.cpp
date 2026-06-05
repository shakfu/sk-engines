// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/edrums/edrums_engine.h"

#include <cmath>
#include <algorithm>
#include <functional>

using namespace spotykach;

constexpr uint8_t EdrumsEngine::kDivTable[3];

// The 5 drum models (Alt+PITCH selects). Each sets the voice character; PITCH still drives body freq +
// noise centre and SOS the decay, within the model. tone = body<->noise, sweep = pitch-drop amount,
// decay_scale multiplies the SOS decay, noise_mult = noise band centre as a multiple of base_hz.
namespace {
struct ModelSpec { float tone; float sweep; float decay_scale; float noise_mult; int bursts; float burst_ms; };
const ModelSpec kModels[] = {
    { 0.00f, 3.0f, 1.20f,  8.f, 0,  0.f }, // 0 Kick  - body, big pitch drop
    { 0.55f, 0.6f, 0.70f, 10.f, 0,  0.f }, // 1 Snare - body + mid noise
    { 0.90f, 0.0f, 0.40f,  7.f, 3, 11.f }, // 2 Clap  - 3 extra noise bursts ~11 ms apart
    { 1.00f, 0.0f, 0.25f, 40.f, 0,  0.f }, // 3 Hat   - high noise, very short
    { 0.20f, 1.8f, 1.00f,  6.f, 0,  0.f }, // 4 Tom   - pitched body, mild drop
};
}

// --- Voice: one synthesized drum ----------------------------------------------------------------
void EdrumsEngine::Voice::init(float sample_rate, float default_hz, int default_model, uint32_t seed)
{
    sr = sample_rate;
    osc.init(sr);
    noise_bpf.Init(sr);
    noise_bpf.SetQ(1.2f);
    base_hz    = default_hz;
    rng        = seed;
    pitch_coef = std::exp(-1.f / (0.025f * sr)); // ~25 ms downward pitch sweep
    decay_norm = 0.5f;                           // sensible default until the SOS knob applies
    set_model(default_model);                    // sets tone/sweep/decay_scale/noise_mult + decay + centre
    amp = 0.f; pitch_env = 0.f;
}

void EdrumsEngine::Voice::set_model(int idx)
{
    idx = std::clamp(idx, 0, EdrumsEngine::kModelCount - 1);
    const ModelSpec& m = kModels[idx];
    tone = m.tone; sweep = m.sweep; decay_scale = m.decay_scale; noise_mult = m.noise_mult;
    burst_count = m.bursts;
    burst_gap   = m.burst_ms * 0.001f * sr;
    _recompute_decay();
    _noise_center();
}

void EdrumsEngine::Voice::_noise_center()
{
    noise_bpf.SetCutoff(std::clamp(base_hz * noise_mult, 300.f, sr * 0.45f));
}

void EdrumsEngine::Voice::_recompute_decay()
{
    const float T = (0.03f + decay_norm * decay_norm * 1.2f) * decay_scale; // 30 ms .. ~1.2 s, scaled
    amp_coef = std::exp(-1.f / (T * sr));
}

void EdrumsEngine::Voice::set_pitch(float norm)
{
    base_hz = 30.f * std::exp2(std::clamp(norm, 0.f, 1.f) * 4.f); // ~30..480 Hz body
    _noise_center();                                             // noise colour tracks pitch
}

void EdrumsEngine::Voice::set_decay(float norm)
{
    decay_norm = std::clamp(norm, 0.f, 1.f);
    _recompute_decay();
}

void EdrumsEngine::Voice::trigger()
{
    amp        = 1.f;
    pitch_env  = 1.f;
    osc.reset();
    burst_left  = burst_count;   // schedule the clap's extra bursts (0 for other models)
    burst_timer = burst_gap;
}

float EdrumsEngine::Voice::process()
{
    // Multi-burst (Clap): re-arm the amp env at each scheduled burst for the characteristic stutter.
    if (burst_left > 0) {
        burst_timer -= 1.f;
        if (burst_timer <= 0.f) { amp = 1.f; burst_left--; burst_timer += burst_gap; }
    }

    if (amp < 1.0e-4f) { amp = 0.f; return 0.f; } // idle

    // Body: sine whose frequency starts (1+kPitchSweep)x base and decays to base (the drum "thump").
    pitch_env *= pitch_coef;
    osc.set_freq(base_hz * (1.f + pitch_env * sweep));
    const float body = osc.process();

    // Noise: cheap LCG -> -1..1, band-passed (the bandpass attenuates, so compensate a little).
    rng = rng * 1664525u + 1013904223u;
    const float n_raw = static_cast<float>(rng >> 8) * (1.f / 8388608.f) - 1.f;
    const float n = noise_bpf.Process(n_raw) * 2.f;

    amp *= amp_coef;
    return (body * (1.f - tone) + n * tone) * amp;
}

// --- EdrumsEngine -------------------------------------------------------------------------------
void EdrumsEngine::init(const EngineContext& ctx)
{
    // Subscribe to the platform clock: the sequencer steps off its ticks (tempo comes from there too).
    _transport = ctx.transport;
    using namespace std::placeholders;
    _transport->set_on_tick(std::bind(&EdrumsEngine::_on_tick, this, _1));

    const float sr = ctx.sample_rate;
    // Deck A defaults to the Kick model, deck B to the Snare model (Alt+PITCH changes them live).
    _track[DeckRef::A].voice.init(sr, 55.f,  0 /*kick*/,  0x9e3779b9u);
    _track[DeckRef::B].voice.init(sr, 200.f, 1 /*snare*/, 0x6d2b79f5u);

    // Default densities. Pos is the one knob the platform seeds FROM the engine (_init_values reads
    // param(Pos)), so seed the cache; the live onsets are re-derived from it over the pattern length.
    _param[static_cast<size_t>(ParamId::Pos)][DeckRef::A] = 0.30f; // ~5 onsets over 16
    _param[static_cast<size_t>(ParamId::Pos)][DeckRef::B] = 0.45f; // ~7 onsets over 16
    _apply_density(DeckRef::A);
    _apply_density(DeckRef::B);

    // Probability defaults to 100% (every onset fires). The platform seeds the MOD_AMT knob from
    // param(ModAmp), so pre-seed it to 1.0 -> full clockwise = 100% as expected.
    _param[static_cast<size_t>(ParamId::ModAmp)][DeckRef::A] = 1.0f;
    _param[static_cast<size_t>(ParamId::ModAmp)][DeckRef::B] = 1.0f;

    // Default model selection (Alt+PITCH knob is seeded from param(Aux)): A = Kick (0), B = Snare (1).
    _param[static_cast<size_t>(ParamId::Aux)][DeckRef::A] = 0.0f;
    _param[static_cast<size_t>(ParamId::Aux)][DeckRef::B] = 0.25f; // round(0.25*4) = 1
}

// Re-derive the onset count from the stored POS fraction over the CURRENT pattern length, so density
// stays proportional when SIZE changes the length.
void EdrumsEngine::_apply_density(DeckRef::Ref d)
{
    auto& p = _track[d].pattern;
    const float frac = _param[static_cast<size_t>(ParamId::Pos)][d];
    p.set_onsets(static_cast<uint8_t>(std::round(frac * p.steps())));
}

// Transport sink (audio-block context). Each deck steps every `_div` ticks (its own rate -> polymeter
// with the per-deck length); on an onset it fires, gated by the randomness amount. A grid reset
// realigns both decks (step phase + pattern position) to the bar.
void EdrumsEngine::_on_tick(const TransportTick& e)
{
    if (e.reset) {
        _step_tick = 0;
        _track[DeckRef::A].pattern.reset();
        _track[DeckRef::B].pattern.reset();
    }
    if (!e.tick) return;

    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        if (_div[d] == 0 || (_step_tick % _div[d]) != 0) continue;
        if (_track[d].pattern.trigger()) {
            const bool fire = _prob[d] >= 1.f || _rand01(_track[d].prob_rng) < _prob[d];
            if (fire) {
                _pan[d] = _rand01(_track[d].prob_rng); // random pan for the Generative routing mode
                _track[d].voice.trigger();
                _flash[d] = kFlashFrames;
            }
        }
    }
    _step_tick++;
}

void EdrumsEngine::process(const float* const* /*in*/, float** out, size_t size)
{
    // A drum machine ignores the audio input. The routing switch picks how the two voices reach the
    // two outputs: Stereo = both summed to both (a mono drum bus), DoubleMono = A->left / B->right,
    // Generative = each hit randomly panned. SoftLimit keeps the summed bus in range.
    for (size_t i = 0; i < size; i++) {
        const float a = _track[DeckRef::A].voice.process();
        const float b = _track[DeckRef::B].voice.process();
        float l, r;
        switch (_route) {
            case Route::DoubleMono:       l = a; r = b; break;
            case Route::GenerativeStereo: l = a * (1.f - _pan[DeckRef::A]) + b * (1.f - _pan[DeckRef::B]);
                                          r = a * _pan[DeckRef::A]       + b * _pan[DeckRef::B];       break;
            case Route::Stereo: default:  l = a + b; r = a + b; break;
        }
        out[0][i] = daisysp::SoftLimit(l);
        out[1][i] = daisysp::SoftLimit(r);
    }
}

// Routing switch (set_config from _process_switches). Mirrors granular's int mapping so the panel's
// L/C/R positions read the same: 0 = Stereo, 1 = DoubleMono, 2 = GenerativeStereo.
bool EdrumsEngine::set_config(ConfigId id, DeckRef::Ref /*deck*/, int value)
{
    if (id == ConfigId::Route) {
        _route = (value == 2) ? Route::GenerativeStereo
               : (value == 1) ? Route::DoubleMono
                              : Route::Stereo;
    }
    return false;
}

void EdrumsEngine::set_param(ParamId id, DeckRef::Ref deck, float v)
{
    const auto d = _safe(deck);
    _param[static_cast<size_t>(id)][d] = v;
    switch (id) {
        case ParamId::Pos:   _apply_density(d); break;                                 // density (onsets)
        case ParamId::Size: {                                                          // pattern length
            const uint8_t len = static_cast<uint8_t>(2 + std::lround(v * 14.f));        // 2..16 steps
            _track[d].pattern.set_length(len);
            _apply_density(d);                                                          // keep density proportional
        } break;
        case ParamId::Env:   _track[d].pattern.set_shift(v); break;                    // rotation
        case ParamId::Speed: _track[d].voice.set_pitch(v);   break;                    // pitch + noise colour
        case ParamId::Mix:   _track[d].voice.set_decay(v);   break;                    // decay
        case ParamId::ModAmp: _prob[d] = std::clamp(v, 0.f, 1.f); break;               // probability an onset fires
        case ParamId::Aux: {                                                           // Alt+PITCH -> model
            const int mdl = std::clamp(static_cast<int>(std::lround(v * (kModelCount - 1))), 0, kModelCount - 1);
            _track[d].voice.set_model(mdl);
            if (mdl != _model[d]) { _model[d] = static_cast<uint8_t>(mdl); _model_show[d] = kModelShowFrames; }
        } break;
        default: break;                                                                // edrums ignores the rest
    }
}

float EdrumsEngine::param(ParamId id, DeckRef::Ref deck) const
{
    return _param[static_cast<size_t>(id)][_safe(deck)];
}

// MODFREQ knob -> clock division (ticks per step). value 0..1 -> {1/16, 1/8, 1/4}.
void EdrumsEngine::set_mod_speed(DeckRef::Ref deck, float value, bool /*sync*/)
{
    const auto d = _safe(deck);
    int idx = static_cast<int>(std::clamp(value, 0.f, 1.f) * 3.f);
    if (idx > 2) idx = 2;
    _div[d] = kDivTable[idx];
}

void EdrumsEngine::render(DisplayModel& m)
{
    m.clear();
    static const uint32_t kColor[2] = { 0xff5500, 0x00b0ff }; // A amber, B cyan
    for (int c = 0; c < 2; c++) {
        // Just after an Alt+PITCH model change, show the model number (model+1 white dots) for a moment.
        if (_model_show[c] > 0) {
            _model_show[c]--;
            m.ring[c].set_point_hex_color(0xffffff);
            for (uint8_t i = 0; i <= _model[c]; i++) m.ring[c].set_point(static_cast<uint8_t>(i * 3), 1.f);
            m.ring[c].set_updated();
            m.play[c] = { kColor[c], _flash[c] > 0 ? 1.f : 0.15f };
            if (_flash[c] > 0) _flash[c]--;
            continue;
        }
        auto& tr = _track[c];
        m.ring[c].set_point_hex_color(kColor[c]);
        const uint8_t steps = tr.pattern.steps();
        const uint8_t pos   = tr.pattern.position();
        for (uint8_t s = 0; s < steps; s++) {
            float b = tr.pattern.step_is_onset(s) ? 0.5f : 0.06f;   // onset lit, rest dim
            if (s == pos) b = 1.f;                                  // playhead
            const uint8_t led = static_cast<uint8_t>(static_cast<uint16_t>(s) * 32u / steps); // length fills the ring
            m.ring[c].set_point(led, b);
        }
        m.ring[c].set_updated();
        m.play[c] = { kColor[c], _flash[c] > 0 ? 1.f : 0.15f };    // flash on a hit
        if (_flash[c] > 0) _flash[c]--;
    }
}
