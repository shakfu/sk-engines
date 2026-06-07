// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/delay/delay_engine.h"
#include "engine/arena.h"
#include "config.h"   // kTempoMinBpm (sizes the delay buffer for the longest division at min tempo)

#include <cmath>
#include <algorithm>

using namespace spotykach;

// Musical-division table (quarter-note beats), ascending so SIZE up = longer delay. Straight + dotted
// (.) + triplet (T): 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/4. 1/2. delay_time = (60/bpm)*beats.
static constexpr float kDivBeats[] = {
    1.f/6.f,  // 1/16T
    0.25f,    // 1/16
    1.f/3.f,  // 1/8T
    0.375f,   // 1/16.
    0.5f,     // 1/8
    2.f/3.f,  // 1/4T
    0.75f,    // 1/8.
    1.f,      // 1/4
    1.5f,     // 1/4.
    2.f       // 1/2
};
static constexpr int kDivCount = sizeof(kDivBeats) / sizeof(kDivBeats[0]);

// The buffer must hold the longest division (1/2 note = 2 beats) at the slowest tempo, plus a couple
// of samples of headroom for the fractional read. ~6 s at 20 BPM; trivially fits the 48 MB arena.
static constexpr float kMaxDelaySeconds = 2.f * 60.f / kTempoMinBpm;
static constexpr size_t kReadHeadroom   = 4;

// --- Tap: one delay line over borrowed SDRAM ------------------------------------------------------
void DelayEngine::Tap::init(void* mem, float sample_rate, size_t length)
{
    buf   = static_cast<float*>(mem);
    sr    = sample_rate;
    len   = length;
    min_d = sr * 0.001f;                              // 1 ms floor
    max_d = static_cast<float>(len - kReadHeadroom);  // buffer ceiling (room for the fractional read)
    div   = 0; beats = kDivBeats[0]; target_d = min_d;
    w = 0;
    s_delay = min_d; s_fb = 0.f; s_mix = 0.f; s_ratio = 1.f; ratio = 1.f; peak = 0.f;
    pw = 0; phase = 0.f;
    for (size_t i = 0; i < kPSWin; i++) ps[i] = 0.f;
    if (buf) for (size_t i = 0; i < len; i++) buf[i] = 0.f;
}

// SIZE knob -> nearest musical division (stepped). beats feeds the per-block delay-time computation.
void DelayEngine::Tap::set_div(float norm)
{
    div   = std::clamp(static_cast<int>(std::round(norm * (kDivCount - 1))), 0, kDivCount - 1);
    beats = kDivBeats[div];
}

// Lock the delay time to the transport tempo: target_d = sr*(60/bpm)*beats, clamped to the buffer.
void DelayEngine::Tap::set_target(float bpm)
{
    const float d = sr * (60.f / bpm) * beats;
    target_d = std::clamp(d, min_d, max_d);
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

    // target_d is the tempo-synced delay (set per block by set_target); glide so tempo changes ramp.
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
    // The delay time syncs to this clock (read-only); the platform owns it.
    _transport = ctx.transport;

    // SIZE (musical division) is now engine-seeded by the platform; carry delay's prior 1.0 default so
    // its startup division is unchanged (was a shared 1.0 literal in core.ui).
    _param[static_cast<size_t>(ParamId::Size)][DeckRef::A] = 1.0f;
    _param[static_cast<size_t>(ParamId::Size)][DeckRef::B] = 1.0f;

    // Sub-allocate two delay lines from the platform's opaque SDRAM arena. Each tap is sized to hold
    // the longest division at the slowest tempo (~6 s), dwarfed by the 48 MB arena.
    Arena arena(ctx.arena);
    const size_t len = static_cast<size_t>(ctx.sample_rate * kMaxDelaySeconds) + kReadHeadroom;
    _tap[DeckRef::A].init(arena.alloc<float>(len), ctx.sample_rate, len);
    _tap[DeckRef::B].init(arena.alloc<float>(len), ctx.sample_rate, len);
}

void DelayEngine::process(const float* const* in, float** out, size_t size)
{
    // Lock both taps to the current tempo once per block (the per-sample smoother ramps the change).
    const float bpm = _transport ? _transport->tempo() : 120.f;
    _tap[DeckRef::A].set_target(bpm);
    _tap[DeckRef::B].set_target(bpm);

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
        case ParamId::Size:  _tap[d].set_div(v);        break; // SIZE knob  -> musical division (tempo-synced)
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
        m.ring[c].set_hex_color(0x00a0ff);                 // division arc (cyan-blue), stepped by SIZE
        m.ring[c].set_segment(0.f, static_cast<float>(_tap[c].div + 1) / kDivCount);
        m.ring[c].set_updated();
        m.play[c] = { 0x00ff00, _tap[c].peak > 1e-3f ? 1.f : 0.15f }; // lit by input signal
    }
}
