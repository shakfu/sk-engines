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
    1.f/6.f, 0.25f, 1.f/3.f, 0.375f, 0.5f, 2.f/3.f, 0.75f, 1.f, 1.5f, 2.f
};
static constexpr int kDivCount = sizeof(kDivBeats) / sizeof(kDivBeats[0]);

// The buffer must hold the longest division (1/2 note) at the slowest tempo, plus a couple samples of
// headroom for the fractional read. ~6 s at the min tempo; trivially fits the 48 MB arena.
static constexpr float  kMaxDelaySeconds = 2.f * 60.f / kTempoMinBpm;
static constexpr size_t kReadHeadroom    = 4;

static constexpr float kTwoPi    = 6.2831853f;
static constexpr float kTapeDrive = 1.6f;  // feedback saturation amount in Tape mode

// --- Shifter: crossfading two-head pitch shifter --------------------------------------------------
float DelayEngine::Shifter::read(float off) const
{
    float rp = static_cast<float>(w) - off;
    while (rp < 0.f) rp += static_cast<float>(kWin);
    const size_t i0 = static_cast<size_t>(rp);
    const float  f  = rp - static_cast<float>(i0);
    const size_t i1 = (i0 + 1 >= kWin) ? 0 : i0 + 1;
    return buf[i0] * (1.f - f) + buf[i1] * f;
}

// Read offset advances at (1 - ratio)/sample so the read rate is `ratio` (output transposed by ratio);
// two heads half a window apart, raised-cosine crossfaded (gains sum to 1, zero at the wrap). Unity-safe.
float DelayEngine::Shifter::process(float x, float ratio)
{
    buf[w] = x;
    if (++w >= kWin) w = 0;
    if (ratio > 0.999f && ratio < 1.001f) return x; // transparent at unity

    phase += (1.f - ratio);
    while (phase >= kWin) phase -= kWin;
    while (phase < 0.f)   phase += kWin;
    float off1 = phase + kWin * 0.5f;
    if (off1 >= kWin) off1 -= kWin;

    const float g0 = 0.5f * (1.f - std::cos(kTwoPi * phase / kWin));
    const float g1 = 0.5f * (1.f - std::cos(kTwoPi * off1  / kWin));
    return read(phase) * g0 + read(off1) * g1;
}

// --- Tap: one delay line over borrowed SDRAM ------------------------------------------------------
void DelayEngine::Tap::init(void* mem, float sample_rate, size_t length)
{
    buf   = static_cast<float*>(mem);
    sr    = sample_rate;
    len   = length;
    min_d = sr * 0.001f;                              // 1 ms floor
    max_d = static_cast<float>(len - kReadHeadroom);  // buffer ceiling (room for the fractional read)
    div   = 0; beats = kDivBeats[0]; target_d = min_d;
    w = 0; mode = Clean;
    s_delay = min_d; s_fb = 0.f; s_mix = 0.f; s_ratio = 1.f;
    fb = 0.f; mix = 0.f; ratio = 1.f; tone_coef = 1.f;
    tone_lp = 0.f; peak = 0.f; _x = 0.f; _wet = 0.f;
    wow_ph = 0.f; wow_inc = kTwoPi * 0.7f / sr; wow_depth = 0.0015f * sr; // ~0.7 Hz wow, +/-1.5 ms
    out_shift.clear(); fb_shift.clear();
    if (buf) for (size_t i = 0; i < len; i++) buf[i] = 0.f;
}

void DelayEngine::Tap::set_div(float norm)
{
    div   = std::clamp(static_cast<int>(std::round(norm * (kDivCount - 1))), 0, kDivCount - 1);
    beats = kDivBeats[div];
}

// ENV 0..1 -> feedback one-pole low-pass coefficient. env=1 fully open (transparent); lower = darker.
void DelayEngine::Tap::set_tone(float env)
{
    if (env >= 0.98f) { tone_coef = 1.f; return; }
    const float fc = 40.f * std::exp2(env * 9.f);              // ~40 Hz .. ~20 kHz, exponential
    const float c  = 1.f - std::exp(-kTwoPi * fc / sr);
    tone_coef = std::clamp(c, 0.f, 1.f);
}

void DelayEngine::Tap::set_target(float bpm)
{
    const float d = sr * (60.f / bpm) * beats;
    target_d = std::clamp(d, min_d, max_d);
}

// Smooth controls, read the (optionally wow-modulated) fractional tap, and colorize the signal that
// will be fed back: tone low-pass (all modes), plus Tape saturation or Shimmer +12 shift. Returns the
// pre-feedback-gain signal; the heard wet (stashed in _wet) is the raw tap, transposed later by PITCH.
float DelayEngine::Tap::read_color(float x_in)
{
    _x = x_in;
    if (!buf) { _wet = 0.f; return 0.f; }
    constexpr float kSmooth = 0.0015f; // per-sample one-pole glide (no zipper)
    s_delay += (target_d - s_delay) * kSmooth;
    s_fb    += (fb       - s_fb)    * kSmooth;
    s_mix   += (mix      - s_mix)   * kSmooth;
    s_ratio += (ratio    - s_ratio) * kSmooth;

    float d = s_delay;
    if (mode == Tape) {                          // wow/flutter: a slow LFO on the read time
        wow_ph += wow_inc;
        if (wow_ph >= kTwoPi) wow_ph -= kTwoPi;
        d += std::sin(wow_ph) * wow_depth;
        if (d < min_d) d = min_d;
    }
    float rp = static_cast<float>(w) - d;
    while (rp < 0.f) rp += static_cast<float>(len);
    const size_t i0 = static_cast<size_t>(rp);
    const float  frac = rp - static_cast<float>(i0);
    const size_t i1 = (i0 + 1 >= len) ? 0 : i0 + 1;
    _wet = buf[i0] * (1.f - frac) + buf[i1] * frac;

    float s = _wet;
    tone_lp += (s - tone_lp) * tone_coef;        // feedback tone (ENV); transparent when tone_coef == 1
    s = tone_lp;
    if (mode == Tape)         s = std::tanh(s * kTapeDrive);
    else if (mode == Shimmer) s = fb_shift.process(s, 2.f); // +12 (octave up) each pass -> shimmer
    return s;
}

// Write input + feedback (cross-fed in ping-pong: fbsig may be the OTHER tap's), advance the write
// head, and return the wet/dry mix. The heard wet is the raw tap (_wet) transposed by PITCH.
float DelayEngine::Tap::write_out(float fbsig)
{
    if (!buf) return _x;
    const float w_in = _x + s_fb * fbsig;
    // Clean stays bit-for-bit; Tape/Shimmer soft-clip the loop so the colored feedback can't run away
    // (transparent below ~0.6, gently saturating above).
    buf[w] = (mode == Clean) ? w_in : std::tanh(w_in * 0.8f) * 1.25f;
    if (++w >= len) w = 0;

    const float a = _x < 0.f ? -_x : _x;
    peak = a > peak ? a : peak * 0.9995f;

    return _x * (1.f - s_mix) + out_shift.process(_wet, s_ratio) * s_mix;
}

// --- DelayEngine ----------------------------------------------------------------------------------
void DelayEngine::init(const EngineContext& ctx)
{
    _transport = ctx.transport; // delay time syncs to this clock (read-only); the platform owns it.

    // Seed the platform-engine-seeded knobs to sensible startup values (the rest default to 0 = dry,
    // no feedback): SIZE = 1/4-ish division, PITCH = unity, ENV = open tone (so Clean boots transparent).
    for (int d = 0; d < DeckRef::Count; d++) {
        _param[static_cast<size_t>(ParamId::Size)][d]  = 1.0f;
        _param[static_cast<size_t>(ParamId::Speed)][d] = 0.5f; // unity pitch
        _param[static_cast<size_t>(ParamId::Env)][d]   = 1.0f; // open feedback tone
    }

    // Sub-allocate two delay lines from the platform's opaque SDRAM arena (each ~6 s).
    Arena arena(ctx.arena);
    const size_t len = static_cast<size_t>(ctx.sample_rate * kMaxDelaySeconds) + kReadHeadroom;
    _tap[DeckRef::A].init(arena.alloc<float>(len), ctx.sample_rate, len);
    _tap[DeckRef::B].init(arena.alloc<float>(len), ctx.sample_rate, len);
}

// Derive a tap's per-block targets from the cached knob values for deck `src` (deck B reads A's when a
// linked route is active) plus that deck's character mode.
void DelayEngine::apply_params(Tap& t, DeckRef::Ref src)
{
    const size_t s = static_cast<size_t>(src);
    t.set_div(_param[static_cast<size_t>(ParamId::Size)][s]);
    t.fb    = _param[static_cast<size_t>(ParamId::Pos)][s] * 0.95f;          // capped to avoid runaway
    t.mix   = _param[static_cast<size_t>(ParamId::Mix)][s];
    t.ratio = std::exp2((_param[static_cast<size_t>(ParamId::Speed)][s] - 0.5f) * 2.f); // +/-1 octave
    t.set_tone(_param[static_cast<size_t>(ParamId::Env)][s]);
    t.mode  = _mode[s];
}

void DelayEngine::process(const float* const* in, float** out, size_t size)
{
    const float bpm      = _transport ? _transport->tempo() : 120.f;
    const bool  linked   = (_route != Route::DoubleMono);          // Stereo + Ping-pong share deck A's controls
    const bool  pingpong = (_route == Route::GenerativeStereo);    // cross-feedback on the 3rd route position

    apply_params(_tap[DeckRef::A], DeckRef::A);
    apply_params(_tap[DeckRef::B], linked ? DeckRef::A : DeckRef::B);
    _tap[DeckRef::A].set_target(bpm);
    _tap[DeckRef::B].set_target(bpm);

    for (size_t i = 0; i < size; i++) {
        const float fa = _tap[DeckRef::A].read_color(in[0][i]);
        const float fb = _tap[DeckRef::B].read_color(in[1][i]);
        if (pingpong) {                                            // echoes bounce L<->R
            out[0][i] = _tap[DeckRef::A].write_out(fb);
            out[1][i] = _tap[DeckRef::B].write_out(fa);
        } else {
            out[0][i] = _tap[DeckRef::A].write_out(fa);
            out[1][i] = _tap[DeckRef::B].write_out(fb);
        }
    }
}

void DelayEngine::set_param(ParamId id, DeckRef::Ref deck, float v)
{
    _param[static_cast<size_t>(id)][_safe(deck)] = v; // source of truth; applied per block in process()
}

float DelayEngine::param(ParamId id, DeckRef::Ref deck) const
{
    return _param[static_cast<size_t>(id)][_safe(deck)];
}

// ConfigId::Mode (the Reel/Slice/Drift switch, per deck) selects the character; ConfigId::Route selects
// the topology. Route wire values: 0=Stereo, 1=DoubleMono, 2=GenerativeStereo (see core.ui.cpp).
bool DelayEngine::set_config(ConfigId id, DeckRef::Ref deck, int value)
{
    if (id == ConfigId::Route) {
        const Route r = value == 2 ? Route::GenerativeStereo : value == 1 ? Route::DoubleMono : Route::Stereo;
        if (r == _route) return false;
        _route = r;
        return true;
    }
    if (id == ConfigId::Mode) {
        const auto    d = _safe(deck);
        const uint8_t m = static_cast<uint8_t>(std::clamp(value, 0, static_cast<int>(ModeCount) - 1));
        if (m == _mode[d]) return false;
        _mode[d] = m;
        return true;
    }
    return false;
}

void DelayEngine::render(DisplayModel& m)
{
    m.clear();
    // Ring tint by character: Clean = cyan-blue, Tape = amber, Shimmer = violet.
    static const uint32_t kModeColor[ModeCount] = { 0x00a0ffu, 0xff8000u, 0x8800ffu };

    // The 3 mode LEDs show the ROUTE (platform convention): DoubleMono / Stereo / GenerativeStereo.
    DisplayModel::Indicator* mode_led[3] = { &m.mode_left, &m.mode_center, &m.mode_right };
    const int route_led = (_route == Route::DoubleMono) ? 0 : (_route == Route::GenerativeStereo) ? 2 : 1;
    *mode_led[route_led] = { 0xffffffu, 0.8f };

    const bool linked = (_route != Route::DoubleMono);
    for (int c = 0; c < 2; c++) {
        const uint8_t md = _mode[linked ? DeckRef::A : c]; // linked routes show deck A's character on both
        m.ring[c].set_hex_color(kModeColor[md]);
        m.ring[c].set_segment(0.f, static_cast<float>(_tap[c].div + 1) / kDivCount); // division arc, stepped by SIZE
        m.ring[c].set_updated();
        m.play[c] = { 0x00ff00, _tap[c].peak > 1e-3f ? 1.f : 0.15f }; // lit by input signal
    }
}
