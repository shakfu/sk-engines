#include "engine/pstretch/pstretch_engine.h"

#include "engine/arena.h"
#include "daisysp.h"   // daisysp::SoftLimit

#include <cmath>

namespace spotykach {

void PstretchEngine::init(const EngineContext& ctx) {
    const float sr = ctx.sample_rate > 0.f ? ctx.sample_rate : 48000.f;
    Arena arena(ctx.arena);

    // The FFT working set is in on-chip SRAM (engine members), not the SDRAM arena - see the header. Only
    // the per-voice input ring (allocated below) lives in SDRAM.
    _fft.init(kWindow, _cos, _sin, _brev);
    float* window = _window, *invnorm = _invnorm;
    _shared.fft = &_fft; _shared.window = window; _shared.invnorm = invnorm;
    _shared.n = kWindow; _shared.hop = kHop;

    // cos/sin LUT for the phase smear (radians -> index via L/2pi; negative angles wrap via the mask).
    constexpr float twopi = 6.28318530717958647692f;
    for (int i = 0; i < kLut; i++) {
        _lutc[i] = std::cos(twopi * static_cast<float>(i) / static_cast<float>(kLut));
        _luts[i] = std::sin(twopi * static_cast<float>(i) / static_cast<float>(kLut));
    }
    _shared.lutc = _lutc; _shared.luts = _luts;
    _shared.lutmask = kLut - 1; _shared.lutscale = static_cast<float>(kLut) / twopi;

    // PaulStretch window: (1 - x^2)^1.25, x in [-1, 1] - flat-topped with smooth edges.
    for (int i = 0; i < kWindow; i++) {
        const float x = 2.f * static_cast<float>(i) / static_cast<float>(kWindow - 1) - 1.f;
        window[i] = std::pow(1.f - x * x, 1.25f);
    }
    // 50%-overlap COLA gain per output position: 1 / (w[i]^2 + w[i+hop]^2), so the windowed analysis +
    // synthesis double-windowing reconstructs to unity (modulo the incoherence from phase randomization).
    for (int i = 0; i < kHop; i++) {
        const float a = window[i], b = window[i + kHop];
        const float d = a * a + b * b;
        invnorm[i] = d > 1e-6f ? 1.f / d : 0.f;
    }

    for (int v = 0; v < 2; v++) {
        float* inring = arena.alloc<float>(kRing, 16);   // ~1 MB ring stays in SDRAM (sequential reads)
        // re/im/ola/fifo are on-chip SRAM (the FFT working set is the hot path).
        _voice[v].init(sr, &_shared, inring, kRing, _re[v], _im[v], _ola[v], _fifo[v], 2 * kHop,
                       v == 0 ? 0x12345678u : 0x2545F491u);
        _apply(v == 0 ? DeckRef::A : DeckRef::B);
    }
}

// Push the cached 0..1 control values into a voice as its real units.
void PstretchEngine::_apply(DeckRef::Ref d) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    _voice[i].set_stretch(std::pow(64.f, _stretch_n[i]));             // 1x .. 64x
    _voice[i].set_diffusion(_diffuse_n[i]);                           // 0 .. 1
    _voice[i].set_pitch(std::exp2((_pitch_n[i] - 0.5f) * 2.f));       // +/- 1 octave
    _voice[i].set_freeze(_frozen[i]);
}

void PstretchEngine::process(const float* const* in, float** out, size_t size) {
    const size_t n = size > kMaxFrames ? kMaxFrames : size;

    float wetA[kMaxFrames], wetB[kMaxFrames];
    // 1. Capture live input. 2. Advance the pipelined workers under a small SHARED per-block budget so a
    //    whole FFT never lands in one block: deck A takes what it needs (it idles ~1/3 of hops, leaving the
    //    budget to B), B gets the remainder. The 24-stage-per-FFT work spreads over the ~21 blocks a hop's
    //    output lasts, flattening the per-block load. 3. Drain each voice's FIFO.
    _voice[0].write_input(in[0], n);
    _voice[1].write_input(in[1], n);
    int budget = kWorkBudget;
    budget -= _voice[0].work(budget);
    if (budget > 0) _voice[1].work(budget);
    _voice[0].drain(wetA, n);
    _voice[1].drain(wetB, n);

    // Per-deck stereo placement from the routing switch (mirrors the radio/glitch engines).
    float pLa, pRa, pLb, pRb;
    switch (_route) {
        case Route::DoubleMono:        pLa = 1.f; pRa = 0.f; pLb = 0.f; pRb = 1.f; break;   // A left, B right
        case Route::GenerativeStereo:  pLa = _rndL[0]; pRa = _rndR[0]; pLb = _rndL[1]; pRb = _rndR[1]; break;
        case Route::Stereo: default:   pLa = pRa = pLb = pRb = kCenterGain; break;          // both centred
    }
    const float La = _gA * pLa, Ra = _gA * pRa, Lb = _gB * pLb, Rb = _gB * pRb;

    for (size_t i = 0; i < n; i++) {
        // dry/wet, then ENV tone low-pass per deck.
        float a = in[0][i] * (1.f - _wet[0]) + wetA[i] * _wet[0];
        float b = in[1][i] * (1.f - _wet[1]) + wetB[i] * _wet[1];
        _lp[0] += _tone[0] * (a - _lp[0]); a = _lp[0];
        _lp[1] += _tone[1] * (b - _lp[1]); b = _lp[1];

        out[0][i] = daisysp::SoftLimit(a * La + b * Lb);
        out[1][i] = daisysp::SoftLimit(a * Ra + b * Rb);
    }
}

// SIZE -> stretch, POS -> diffusion, PITCH -> pitch, ENV -> tone, MIX -> dry/wet, Crossfade -> A/B blend.
void PstretchEngine::set_param(ParamId id, DeckRef::Ref d, float v) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Size)           { _stretch_n[i] = v; _voice[i].set_stretch(std::pow(64.f, v)); }
    else if (id == ParamId::Pos)       { _diffuse_n[i] = v; _voice[i].set_diffusion(v); }
    else if (id == ParamId::Speed)     { _pitch_n[i] = v; _voice[i].set_pitch(std::exp2((v - 0.5f) * 2.f)); }
    else if (id == ParamId::Env)       { _tone[i] = v * v; }                       // 0 = dark, 1 = open
    else if (id == ParamId::Mix)       { _wet[i] = v; }
    else if (id == ParamId::Crossfade) { _xfade = v; _gA = v <= 0.5f ? 1.f : 2.f * (1.f - v);
                                                     _gB = v >= 0.5f ? 1.f : 2.f * v; }
}

float PstretchEngine::param(ParamId id, DeckRef::Ref d) const {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Size)  return _stretch_n[i];
    if (id == ParamId::Pos)   return _diffuse_n[i];
    if (id == ParamId::Speed) return _pitch_n[i];
    if (id == ParamId::Env)   return _tone[i];
    if (id == ParamId::Mix)   return _wet[i];
    return 0.f;
}

// Routing switch: 0=Stereo, 1=DoubleMono, 2=GenerativeStereo (mirrors granular/radio).
bool PstretchEngine::set_config(ConfigId id, DeckRef::Ref, int value) {
    if (id == ConfigId::Route) {
        const Route r = (value == 2) ? Route::GenerativeStereo
                      : (value == 1) ? Route::DoubleMono
                                     : Route::Stereo;
        if (r != _route) { _route = r; if (_route == Route::GenerativeStereo) _roll_random_pans(); }
    }
    return false;
}

// Play pad toggles FREEZE (stop the read head); Rev pad toggles CAPTURE/hold (grab the recent ring and
// loop the stretch through it). Both per deck, latched. Returns false (no is-empty LED for an effect).
bool PstretchEngine::on_play_pad(DeckRef::Ref d, bool reverse) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (!reverse) { _frozen[i]   = !_frozen[i];   _voice[i].set_freeze(_frozen[i]); }
    else          { _captured[i] = !_captured[i]; _voice[i].set_capture(_captured[i]); }
    return false;
}

void PstretchEngine::render(DisplayModel& m) {
    m.clear();
    for (DeckRef::Ref dk : { DeckRef::A, DeckRef::B }) {
        const int i = (dk == DeckRef::A) ? 0 : 1;
        // State colour: frozen = cyan (held drone), captured = amber (looping a grab), else green (live).
        const uint32_t c = _frozen[i] ? 0x00ffffu : (_captured[i] ? 0xff8000u : 0x00ff00u);
        m.play[i] = { c, 0.7f };
        // Ring: a marker whose position tracks the stretch amount, in the state colour (brighter when held).
        m.ring[i].set_hex_color(0x101010); m.ring[i].set_segment(0.f, 0.999f);
        m.ring[i].set_point_hex_color(c);
        m.ring[i].add_point(_stretch_n[i], (_frozen[i] || _captured[i]) ? 1.f : 0.6f);
        m.ring[i].set_updated();
    }
    if (_route == Route::DoubleMono)  m.mode_left   = { 0xffffff, 0.8f };
    else if (_route == Route::Stereo) m.mode_center = { 0xffffff, 0.8f };
    else                              m.mode_right  = { 0xffffff, 0.8f };
}

void PstretchEngine::_roll_random_pans() {
    for (int i = 0; i < 2; i++) {
        _rng = _rng * 1664525u + 1013904223u;
        const float p = static_cast<float>(_rng >> 8) * (1.f / 16777216.f);   // [0,1)
        _rndL[i] = std::cos(p * 1.57079632679f);
        _rndR[i] = std::sin(p * 1.57079632679f);
    }
}

} // namespace spotykach
