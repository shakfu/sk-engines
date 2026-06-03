// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/delay/delay_engine.h"
#include "engine/arena.h"

#include <cmath>

using namespace spotykach;

// --- Tap: one delay line over borrowed SDRAM ------------------------------------------------------
void DelayEngine::Tap::init(void* mem, float sr)
{
    buf   = static_cast<float*>(mem);
    len   = static_cast<size_t>(sr * 2.0f); // 2 s of float storage - dwarfed by the 42 s source buffer
    min_d = sr * 0.001f;                     // 1 ms floor
    max_d = sr * 1.0f;                       // 1 s ceiling (< len, leaves room for the fractional read)
    w = 0;
    s_delay = max_d; s_fb = 0.f; s_mix = 0.f; s_ratio = 1.f; ratio = 1.f; peak = 0.f;
    pw = 0; phase = 0.f;
    for (size_t i = 0; i < kPSWin; i++) ps[i] = 0.f;
    if (buf) for (size_t i = 0; i < len; i++) buf[i] = 0.f;
}

// Fractional read `off` samples behind the pitch-shifter write head.
float DelayEngine::Tap::readps(float off) const
{
    float rp = static_cast<float>(pw) - off;
    while (rp < 0.f) rp += static_cast<float>(kPSWin);
    const size_t i0 = static_cast<size_t>(rp);
    const float  f  = rp - static_cast<float>(i0);
    const size_t i1 = (i0 + 1 >= kPSWin) ? 0 : i0 + 1;
    return ps[i0] * (1.f - f) + ps[i1] * f;
}

// Crossfading two-head pitch shifter. The read offset advances at (1 - ratio)/sample so the read
// rate is `ratio` (output transposed by ratio); two heads half a window apart, raised-cosine
// crossfaded (gains sum to 1, zero at the wrap), make the wraparound seamless. Bypassed near unity.
float DelayEngine::Tap::pitch(float x)
{
    ps[pw] = x;
    if (++pw >= kPSWin) pw = 0;
    if (s_ratio > 0.999f && s_ratio < 1.001f) return x; // transparent at unity pitch

    phase += (1.f - s_ratio);
    while (phase >= kPSWin) phase -= kPSWin;
    while (phase < 0.f)     phase += kPSWin;
    float off1 = phase + kPSWin * 0.5f;
    if (off1 >= kPSWin) off1 -= kPSWin;

    constexpr float kTwoPi = 6.2831853f;
    const float u0 = phase / kPSWin;
    const float u1 = off1  / kPSWin;
    const float g0 = 0.5f * (1.f - std::cos(kTwoPi * u0));
    const float g1 = 0.5f * (1.f - std::cos(kTwoPi * u1));
    return readps(phase) * g0 + readps(off1) * g1;
}

// One sample: smooth controls, read a fractional tap, write input + feedback, return wet/dry mix.
float DelayEngine::Tap::process(float x)
{
    if (!buf) return x;
    constexpr float kSmooth = 0.0015f; // per-sample one-pole glide on time/feedback/mix (no zipper)

    const float target_d = min_d + time * (max_d - min_d);
    s_delay += (target_d - s_delay) * kSmooth;
    s_fb    += (fb       - s_fb)    * kSmooth;
    s_mix   += (mix      - s_mix)   * kSmooth;
    s_ratio += (ratio    - s_ratio) * kSmooth;

    float rp = static_cast<float>(w) - s_delay;
    while (rp < 0.f) rp += static_cast<float>(len);
    const size_t i0 = static_cast<size_t>(rp);
    const float  frac = rp - static_cast<float>(i0);
    const size_t i1 = (i0 + 1 >= len) ? 0 : i0 + 1;
    const float  wet = buf[i0] * (1.f - frac) + buf[i1] * frac;

    buf[w] = x + s_fb * wet;   // feedback unpitched, so the delay structure stays stable
    if (++w >= len) w = 0;

    const float a = x < 0.f ? -x : x;
    peak = a > peak ? a : peak * 0.9995f;

    return x * (1.f - s_mix) + pitch(wet) * s_mix;  // the heard taps are transposed by PITCH
}

// --- DelayEngine ----------------------------------------------------------------------------------
void DelayEngine::init(const EngineContext& ctx)
{
    // Sub-allocate two delay lines from the platform's opaque SDRAM arena (item: EngineBuffers
    // generalization). Each tap uses 2 s of float storage (must match Tap::init's `len`).
    Arena arena(ctx.arena);
    const size_t need = static_cast<size_t>(ctx.sample_rate * 2.0f);
    _tap[DeckRef::A].init(arena.alloc<float>(need), ctx.sample_rate);
    _tap[DeckRef::B].init(arena.alloc<float>(need), ctx.sample_rate);
}

void DelayEngine::process(const float* const* in, float** out, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        out[0][i] = _tap[DeckRef::A].process(in[0][i]);
        out[1][i] = _tap[DeckRef::B].process(in[1][i]);
    }
}

void DelayEngine::set_param(ParamId id, DeckRef::Ref deck, float v)
{
    const auto d = _safe(deck);
    _param[static_cast<size_t>(id)][d] = v;
    switch (id) {
        case ParamId::Size:  _tap[d].time = v;          break; // SIZE knob  -> delay time
        case ParamId::Pos:   _tap[d].fb   = v * 0.95f;  break; // POS knob   -> feedback (direct, no Alt)
        case ParamId::Mix:   _tap[d].mix  = v;          break; // SOS knob   -> wet/dry mix
        case ParamId::Speed: _tap[d].ratio = exp2f((v - 0.5f) * 2.f); break; // PITCH knob -> +/-1 octave (center = unity)
        default: break;                                        // delay ignores the other params
    }
}

float DelayEngine::param(ParamId id, DeckRef::Ref deck) const
{
    return _param[static_cast<size_t>(id)][_safe(deck)];
}

void DelayEngine::render(DisplayModel& m)
{
    m.clear();
    for (int c = 0; c < 2; c++) {
        m.ring[c].set_hex_color(0x00a0ff);                 // delay-time arc (cyan-blue)
        m.ring[c].set_segment(0.f, _tap[c].time * 0.999f);
        m.ring[c].set_updated();
        m.play[c] = { 0x00ff00, _tap[c].peak > 1e-3f ? 1.f : 0.15f }; // lit by input signal
    }
}
