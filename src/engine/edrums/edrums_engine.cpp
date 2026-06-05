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
    // Four drums = two decks x two slots, all playing. Slot 0 is the boot-active (editable) drum;
    // slot 1 is the swap-in. Deck A = kick/tom (low spine), deck B = snare/hat (backbeat). All slots
    // are fully seeded here so the three the platform never applies to still sound from the first bar.
    //                d            slot  model         seed         pos    pitch  decay
    _init_slot(DeckRef::A, 0, sr, 0 /*kick */, 0x9e3779b9u, 0.30f, 0.50f, 0.50f);
    _init_slot(DeckRef::A, 1, sr, 4 /*tom  */, 0x85ebca6bu, 0.30f, 0.40f, 0.55f);
    _init_slot(DeckRef::B, 0, sr, 1 /*snare*/, 0x6d2b79f5u, 0.45f, 0.50f, 0.50f);
    _init_slot(DeckRef::B, 1, sr, 3 /*hat  */, 0xc2b2ae35u, 0.50f, 0.82f, 0.30f);
}

// Fully seed one drum so it sounds independently of the platform's knob apply (which only writes the
// active slot). Mirrors what set_param would do for Pos/Size/Env/Speed/Mix/ModAmp/ModSpeed/Aux, and
// caches each norm so a slot swap can re-seed the platform's knobs from param(). Pos is the value the
// platform reads back at boot (active slot only); the rest the platform overwrites for slot 0 with its
// own UI defaults (Size=1, Speed/Mix=.5, Env=0), which match the values used here -> no boot transient.
void EdrumsEngine::_init_slot(DeckRef::Ref d, int slot, float sr, int model, uint32_t seed,
                              float pos, float pitch, float decay)
{
    auto& t = _track[d][slot];
    t.voice.init(sr, 60.f, model, seed); // base_hz is set by set_pitch below; 60 is a placeholder
    t.pattern.set_length(16);            // SIZE=1.0 default (2 + round(1*14))
    t.pattern.set_shift(0.f);            // ENV=0 default (no rotation)
    t.voice.set_pitch(pitch);
    t.voice.set_decay(decay);

    using PI = ParamId;
    auto P = [](PI id) { return static_cast<size_t>(id); };
    _param[P(PI::Pos)][d][slot]      = pos;
    _param[P(PI::Size)][d][slot]     = 1.0f;
    _param[P(PI::Env)][d][slot]      = 0.0f;
    _param[P(PI::Speed)][d][slot]    = pitch;
    _param[P(PI::Mix)][d][slot]      = decay;
    _param[P(PI::ModAmp)][d][slot]   = 1.0f;  // 100% of onsets fire
    _param[P(PI::ModSpeed)][d][slot] = 0.0f;  // MODFREQ idx 0 -> 1/16 (div 1)
    _param[P(PI::Aux)][d][slot]      = static_cast<float>(model) / (kModelCount - 1); // inverse of set_param's round

    _prob[d][slot]  = 1.0f;
    _div[d][slot]   = 1;
    _model[d][slot] = static_cast<uint8_t>(model);
    _apply_density(d, slot);
}

// Re-derive the onset count from the stored POS fraction over the CURRENT pattern length, so density
// stays proportional when SIZE changes the length.
void EdrumsEngine::_apply_density(DeckRef::Ref d, int slot)
{
    auto& p = _track[d][slot].pattern;
    const float frac = _param[static_cast<size_t>(ParamId::Pos)][d][slot];
    p.set_onsets(static_cast<uint8_t>(std::round(frac * p.steps())));
}

// Transport sink (audio-block context). Each deck steps every `_div` ticks (its own rate -> polymeter
// with the per-deck length); on an onset it fires, gated by the randomness amount. A grid reset
// realigns both decks (step phase + pattern position) to the bar.
void EdrumsEngine::_on_tick(const TransportTick& e)
{
    // All four drums sequence here, independent of which slot is focused: focus only gates editing and
    // the ring display, never the audio. Each drum steps at its own _div (polymeter) over its own
    // length. A grid reset realigns every drum (step phase + pattern position) to the bar.
    if (e.reset) {
        _step_tick = 0;
        for (DeckRef::Ref d : { DeckRef::A, DeckRef::B })
            for (int s = 0; s < kSlots; s++) _track[d][s].pattern.reset();
    }
    if (!e.tick) return;

    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        for (int s = 0; s < kSlots; s++) {
            if (_div[d][s] == 0 || (_step_tick % _div[d][s]) != 0) continue;
            if (_track[d][s].pattern.trigger()) {
                const bool fire = _prob[d][s] >= 1.f || _rand01(_track[d][s].prob_rng) < _prob[d][s];
                if (fire) {
                    _pan[d][s] = _rand01(_track[d][s].prob_rng); // random pan for the Generative routing mode
                    _track[d][s].voice.trigger();
                    _flash[d][s] = kFlashFrames;
                }
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
    // Four voices now reach the bus. Stereo = all four summed to both outs (a mono drum bus);
    // DoubleMono = deck A's two slots -> left, deck B's two -> right; Generative = each hit randomly
    // panned. kBusTrim scales before SoftLimit so four simultaneous voices don't sit in constant
    // limiting the way the old two-voice sum avoided.
    for (size_t i = 0; i < size; i++) {
        const float a0 = _track[DeckRef::A][0].voice.process();
        const float a1 = _track[DeckRef::A][1].voice.process();
        const float b0 = _track[DeckRef::B][0].voice.process();
        const float b1 = _track[DeckRef::B][1].voice.process();
        float l, r;
        switch (_route) {
            case Route::DoubleMono:
                l = a0 + a1; r = b0 + b1; break;
            case Route::GenerativeStereo:
                l = a0 * (1.f - _pan[DeckRef::A][0]) + a1 * (1.f - _pan[DeckRef::A][1])
                  + b0 * (1.f - _pan[DeckRef::B][0]) + b1 * (1.f - _pan[DeckRef::B][1]);
                r = a0 * _pan[DeckRef::A][0] + a1 * _pan[DeckRef::A][1]
                  + b0 * _pan[DeckRef::B][0] + b1 * _pan[DeckRef::B][1];
                break;
            case Route::Stereo: default: {
                const float sum = a0 + a1 + b0 + b1; l = sum; r = sum;
            } break;
        }
        out[0][i] = daisysp::SoftLimit(l * kBusTrim);
        out[1][i] = daisysp::SoftLimit(r * kBusTrim);
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

// Rev pad (reverse=true): swap which of the deck's two drums is active (editable + shown). The other
// keeps playing - focus never gates audio. Flag a re-seed so the platform repoints that deck's knob
// pickup at the newly-focused drum's stored params (otherwise the next knob touch would jump it). The
// plain Play pad (reverse=false) has no role here. Return false to suppress the platform's "empty"
// flash: a synth drum engine has no buffer to be empty.
bool EdrumsEngine::on_play_pad(DeckRef::Ref deck, bool reverse)
{
    if (reverse) {
        const auto d = _safe(deck);
        _active_slot[d] ^= 1;
        _reseed_pending[d] = true;
    }
    return false;
}

// One-shot: report (and clear) whether the deck's active drum just changed, so the platform re-seeds
// its MValue cache for that deck exactly once per swap.
bool EdrumsEngine::take_param_reseed(DeckRef::Ref deck)
{
    const auto d = _safe(deck);
    const bool pending = _reseed_pending[d];
    _reseed_pending[d] = false;
    return pending;
}

// All edits target the deck's ACTIVE slot, so the knobs only ever touch the focused drum; the
// backgrounded slot keeps its stored params (and keeps playing).
void EdrumsEngine::set_param(ParamId id, DeckRef::Ref deck, float v)
{
    const auto d = _safe(deck);
    const int  s = _active_slot[d];
    _param[static_cast<size_t>(id)][d][s] = v;
    switch (id) {
        case ParamId::Pos:   _apply_density(d, s); break;                              // density (onsets)
        case ParamId::Size: {                                                          // pattern length
            const uint8_t len = static_cast<uint8_t>(2 + std::lround(v * 14.f));        // 2..16 steps
            _track[d][s].pattern.set_length(len);
            _apply_density(d, s);                                                       // keep density proportional
        } break;
        case ParamId::Env:   _track[d][s].pattern.set_shift(v); break;                 // rotation
        case ParamId::Speed: _track[d][s].voice.set_pitch(v);   break;                 // pitch + noise colour
        case ParamId::Mix:   _track[d][s].voice.set_decay(v);   break;                 // decay
        case ParamId::ModAmp: _prob[d][s] = std::clamp(v, 0.f, 1.f); break;            // probability an onset fires
        case ParamId::Aux: {                                                           // Alt+PITCH -> model
            const int mdl = std::clamp(static_cast<int>(std::lround(v * (kModelCount - 1))), 0, kModelCount - 1);
            _track[d][s].voice.set_model(mdl);
            if (mdl != _model[d][s]) { _model[d][s] = static_cast<uint8_t>(mdl); _model_show[d][s] = kModelShowFrames; }
        } break;
        default: break;                                                                // edrums ignores the rest
    }
}

float EdrumsEngine::param(ParamId id, DeckRef::Ref deck) const
{
    const auto d = _safe(deck);
    return _param[static_cast<size_t>(id)][d][_active_slot[d]];
}

// MODFREQ knob -> clock division (ticks per step). value 0..1 -> {1/16, 1/8, 1/4}. The norm is cached
// into _param[ModSpeed] so a slot swap can re-seed the platform's MODFREQ knob (it is not otherwise
// stored, since set_mod_speed writes the derived division directly).
void EdrumsEngine::set_mod_speed(DeckRef::Ref deck, float value, bool /*sync*/)
{
    const auto d = _safe(deck);
    const int  s = _active_slot[d];
    int idx = static_cast<int>(std::clamp(value, 0.f, 1.f) * 3.f);
    if (idx > 2) idx = 2;
    _div[d][s] = kDivTable[idx];
    _param[static_cast<size_t>(ParamId::ModSpeed)][d][s] = value;
}

void EdrumsEngine::render(DisplayModel& m)
{
    m.clear();
    // Per-(deck,slot) colours in two hue families, so deck identity AND which drum is focused both read
    // at a glance: deck A warm (amber / red-orange), deck B cool (cyan / violet). The ring + Play LED
    // show the focused drum; the Rev LED carries the backgrounded drum's colour.
    static const uint32_t kColor[DeckRef::Count][kSlots] = {
        { 0xff5500, 0xff1a00 }, // A: slot 0 amber, slot 1 red-orange
        { 0x00b0ff, 0x7a5cff }, // B: slot 0 cyan,  slot 1 violet
    };
    for (int c = 0; c < 2; c++) {
        const int sl  = _active_slot[c];
        const int inv = sl ^ 1;                 // the backgrounded slot
        const uint32_t col = kColor[c][sl];

        // Just after an Alt+PITCH model change, show the model number (model+1 white dots) for a moment.
        if (_model_show[c][sl] > 0) {
            _model_show[c][sl]--;
            m.ring[c].set_point_hex_color(0xffffff);
            for (uint8_t i = 0; i <= _model[c][sl]; i++) m.ring[c].set_point(static_cast<uint8_t>(i * 3), 1.f);
            m.ring[c].set_updated();
        }
        else {
            auto& tr = _track[c][sl];
            m.ring[c].set_point_hex_color(col);
            const uint8_t steps = tr.pattern.steps();
            const uint8_t pos   = tr.pattern.position();
            for (uint8_t s = 0; s < steps; s++) {
                float b = tr.pattern.step_is_onset(s) ? 0.5f : 0.06f;   // onset lit, rest dim
                if (s == pos) b = 1.f;                                  // playhead
                const uint8_t led = static_cast<uint8_t>(static_cast<uint16_t>(s) * 32u / steps); // length fills the ring
                m.ring[c].set_point(led, b);
            }
            m.ring[c].set_updated();
        }

        // Play LED = focused drum's colour, full on its hits. Rev LED = backgrounded drum's colour, a
        // dim steady presence (so you always see the other drum is there and which it is) that brightens
        // on its own hits. Decrement both flashes here - the backgrounded slot's flash is set in
        // _on_tick regardless of focus, so consuming it here keeps it from stalling.
        m.play[c] = { col,               _flash[c][sl]  > 0 ? 1.f : 0.15f };
        m.rev[c]  = { kColor[c][inv],    _flash[c][inv] > 0 ? 1.f : 0.12f };
        if (_flash[c][sl]  > 0) _flash[c][sl]--;
        if (_flash[c][inv] > 0) _flash[c][inv]--;
    }
}
