// Headless test for the tape engine's tape FX (Faust kernel: wow/flutter + Jiles-Atherton hysteresis).
// Driven through the public IEngine surface with a stub IStreamDeck that plays a steady sine, so the FX
// runs on a known playback signal. Covers:
//   1. Param round-trip for the four FX knobs (POS=drive, SIZE=char, MOD_AMT=wow, MODFREQ=rate).
//   2. Saturation: heavy drive changes the waveform and stays bounded/finite (the J-A self-limits).
//   3. Wow/flutter: depth > 0 pitch-modulates the output (diverges from the un-modulated case over time).
//   4. The FX runs only while the deck is playing (a stopped deck is silent).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/tape/tape_engine.h"
#include "engine/istreamdeck.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}
bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Stub stream: deck plays a continuous sine (per-deck phase); record path is inert. is_playing is
// togglable so we can test the playback gate.
struct StubStream : IStreamDeck {
    double phase[2] = { 0.0, 0.0 };
    double inc = 2.0 * M_PI * 220.0 / host::kSampleRate; // 220 Hz source tone
    bool   playing[2] = { true, false };                 // deck A plays, deck B idle (isolate A)

    uint32_t play_consume(DeckRef::Ref deck, uint8_t* dst, uint32_t n) override {
        const int i = (deck == DeckRef::A) ? 0 : 1;
        const uint32_t cnt = n / sizeof(float);
        float* f = reinterpret_cast<float*>(dst);
        for (uint32_t k = 0; k < cnt; k++) { f[k] = 0.5f * static_cast<float>(std::sin(phase[i])); phase[i] += inc; }
        return n;
    }
    uint32_t record_produce(DeckRef::Ref, const uint8_t*, uint32_t n) override { return n; }
    bool is_playing(DeckRef::Ref deck)   const override { return playing[(deck == DeckRef::A) ? 0 : 1]; }
    bool is_recording(DeckRef::Ref)      const override { return false; }
    bool start_play(DeckRef::Ref, const char*)   override { return true; }
    bool start_record(DeckRef::Ref, const char*) override { return true; }
    void stop(DeckRef::Ref) override {}
    void set_loop(DeckRef::Ref, bool) override {}
    uint32_t loop_frames(DeckRef::Ref) const override { return 0; }
    bool exists(const char*) const override { return true; }
};

// Run `blocks` audio blocks (after `warmup` to prime the resampler + fill the wow/flutter delay) and
// return the left-channel output. `finite` is cleared on any non-finite sample.
std::vector<float> run(TapeEngine& e, int warmup, int blocks, bool& finite) {
    float il[host::kBlock] = {0}, ir[host::kBlock] = {0}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    for (int b = 0; b < warmup; b++) e.process(in, out, host::kBlock);
    std::vector<float> v;
    for (int b = 0; b < blocks; b++) {
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            v.push_back(ol[i]);
        }
    }
    return v;
}
float sad(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.f; const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]);
    return s;
}
float peak(const std::vector<float>& v) { float p = 0.f; for (float x : v) p = std::fmax(p, std::fabs(x)); return p; }

// Set the four FX knobs (drive, char, wow, rate) on deck A.
void set_fx(TapeEngine& e, float drive, float chr, float wow, float rate) {
    e.set_param(ParamId::Pos,    DeckRef::A, drive);
    e.set_param(ParamId::Size,   DeckRef::A, chr);
    e.set_param(ParamId::ModAmp, DeckRef::A, wow);
    e.set_mod_speed(DeckRef::A, rate, false);
}

} // namespace

int main() {
    host::TimeSource time;
    host::HostArena  arena;
    StubStream       stream;
    EngineContext ctx = host::make_context(arena, time);
    ctx.stream = &stream;

    // --- 1. Param round-trip -------------------------------------------------------------------
    {
        TapeEngine e; e.init(ctx);
        set_fx(e, 0.61f, 0.42f, 0.73f, 0.28f);
        check(approx(e.param(ParamId::Pos,      DeckRef::A), 0.61f), "POS (drive) round-trips");
        check(approx(e.param(ParamId::Size,     DeckRef::A), 0.42f), "SIZE (char) round-trips");
        check(approx(e.param(ParamId::ModAmp,   DeckRef::A), 0.73f), "MOD_AMT (wow) round-trips");
        check(approx(e.param(ParamId::ModSpeed, DeckRef::A), 0.28f), "MODFREQ (rate) round-trips");
    }

    // --- 2. Saturation: heavy drive changes the waveform, stays bounded + finite ----------------
    {
        TapeEngine clean; clean.init(ctx); set_fx(clean, 0.0f, 0.3f, 0.0f, 0.4f); // no drive, no wow
        TapeEngine hot;   hot.init(ctx);   set_fx(hot,   1.0f, 0.3f, 0.0f, 0.4f); // full drive, no wow
        stream.phase[0] = 0.0; bool f1 = true; const auto c = run(clean, 40, 80, f1);
        stream.phase[0] = 0.0; bool f2 = true; const auto h = run(hot,   40, 80, f2);
        const float d = sad(c, h), pk = peak(h);
        std::printf("saturation: SAD(clean,hot)=%.3f  hot_peak=%.3f  finite=%d\n", d, pk, (int)(f1 && f2));
        check(f1 && f2,       "saturation output is finite");
        // The J-A is voiced for subtle tape coloration (the lib's -50 dB calibration), so this is a
        // wiring/stability guard, not an intensity judgement - the latter is a by-ear voicing call.
        check(d > 0.3f,       "drive changes the waveform (hysteresis is wired)");
        check(pk < 4.0f,      "saturated output stays bounded (J-A self-limits, no blow-up)");
    }

    // --- 3. Wow/flutter: depth > 0 pitch-modulates the output -----------------------------------
    {
        TapeEngine dry; dry.init(ctx); set_fx(dry, 0.0f, 0.3f, 0.0f, 0.5f); // no wow
        TapeEngine wet; wet.init(ctx); set_fx(wet, 0.0f, 0.3f, 1.0f, 0.5f); // full wow depth
        stream.phase[0] = 0.0; bool f1 = true; const auto a = run(dry, 60, 160, f1);
        stream.phase[0] = 0.0; bool f2 = true; const auto b = run(wet, 60, 160, f2);
        const float d = sad(a, b);
        std::printf("wow/flutter: SAD(dry,wet)=%.3f  finite=%d\n", d, (int)(f1 && f2));
        check(f1 && f2, "wow/flutter output is finite");
        check(d > 1.0f, "wow/flutter depth pitch-modulates the output");
    }

    // --- 4. FX (and audio) only while the deck is playing ---------------------------------------
    {
        TapeEngine e; e.init(ctx); set_fx(e, 0.8f, 0.3f, 0.5f, 0.4f);
        stream.playing[0] = false;                       // stop deck A
        bool fok = true; const auto v = run(e, 20, 40, fok);
        stream.playing[0] = true;                        // restore for any later runs
        std::printf("gate: stopped-deck peak=%.5f\n", peak(v));
        check(peak(v) < 1e-4f, "a stopped deck is silent (FX/audio gated on playback)");
    }

    if (g_failures == 0) { std::printf("OK: all tape FX checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
