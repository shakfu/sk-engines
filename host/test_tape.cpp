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
// The post-FX filter rides the grit-modifier knobs: cutoff = GritIntensity, resonance = GritMix.
void set_filter(TapeEngine& e, float cutoff, float reso) {
    e.set_param(ParamId::GritIntensity, DeckRef::A, cutoff);
    e.set_param(ParamId::GritMix,       DeckRef::A, reso);
}
float rms(const std::vector<float>& v) {
    double s = 0.0; for (float x : v) s += (double)x * x;
    return v.empty() ? 0.f : (float)std::sqrt(s / v.size());
}

} // namespace

int main() {
    host::TimeSource time;
    host::HostArena  arena;
    StubStream       stream;
    EngineContext ctx = host::make_context(arena, time);
    ctx.stream = &stream;

    // Build a fresh engine in its OWN arena, configure it, and run it. The Arena is a bump allocator
    // that restarts at ctx.arena.base every construction, so two engines built from one shared ctx would
    // alias the same kernel memory - harmless on the single-engine device, but it makes a test that
    // compares two engines silently compare the SAME (last-written) kernel. Isolating the arena per
    // engine is what makes the A/B comparisons below real.
    auto isolated = [&](auto cfg, int warmup, int blocks, bool& fin) {
        host::HostArena a; EngineContext c = host::make_context(a, time); c.stream = &stream;
        TapeEngine e; e.init(c); cfg(e);
        return run(e, warmup, blocks, fin);
    };

    // --- 1. Param round-trip -------------------------------------------------------------------
    {
        TapeEngine e; e.init(ctx);
        set_fx(e, 0.61f, 0.42f, 0.73f, 0.28f);
        check(approx(e.param(ParamId::Pos,      DeckRef::A), 0.61f), "POS (drive) round-trips");
        check(approx(e.param(ParamId::Size,     DeckRef::A), 0.42f), "SIZE (char) round-trips");
        check(approx(e.param(ParamId::ModAmp,   DeckRef::A), 0.73f), "MOD_AMT (wow) round-trips");
        check(approx(e.param(ParamId::ModSpeed, DeckRef::A), 0.28f), "MODFREQ (rate) round-trips");
        // filter knobs (grit-modifier): cutoff = GritIntensity, resonance = GritMix
        set_filter(e, 0.33f, 0.77f);
        check(approx(e.param(ParamId::GritIntensity, DeckRef::A), 0.33f), "filter cutoff (grit+PITCH) round-trips");
        check(approx(e.param(ParamId::GritMix,       DeckRef::A), 0.77f), "filter resonance (grit+MIX) round-trips");
        // a fresh engine boots cutoff OPEN (so the LP is inert) and resonance at 0
        TapeEngine fresh; fresh.init(ctx);
        check(approx(fresh.param(ParamId::GritIntensity, DeckRef::A), 1.0f), "filter cutoff boots OPEN (1.0)");
        check(approx(fresh.param(ParamId::GritMix,       DeckRef::A), 0.0f), "filter resonance boots 0");
    }

    // --- 2. Saturation: heavy drive changes the waveform, stays bounded + finite ----------------
    {
        stream.phase[0] = 0.0; bool f1 = true;
        const auto c = isolated([](TapeEngine& e){ set_fx(e, 0.0f, 0.3f, 0.0f, 0.4f); }, 40, 80, f1); // no drive
        stream.phase[0] = 0.0; bool f2 = true;
        const auto h = isolated([](TapeEngine& e){ set_fx(e, 1.0f, 0.3f, 0.0f, 0.4f); }, 40, 80, f2); // full drive
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
        stream.phase[0] = 0.0; bool f1 = true;
        const auto a = isolated([](TapeEngine& e){ set_fx(e, 0.0f, 0.3f, 0.0f, 0.5f); }, 60, 160, f1); // no wow
        stream.phase[0] = 0.0; bool f2 = true;
        const auto b = isolated([](TapeEngine& e){ set_fx(e, 0.0f, 0.3f, 1.0f, 0.5f); }, 60, 160, f2); // full wow
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

    // --- 5. Post-FX low-pass: low cutoff attenuates a high tone; resonance boosts near the corner ---
    {
        stream.inc = 2.0 * M_PI * 6000.0 / host::kSampleRate;   // 6 kHz source so the low-pass clearly bites
        // colouring FX off so the filter dominates; each engine in its own arena (see `isolated`).
        auto frms = [&](float cutoff, float reso, bool& fin) {
            stream.phase[0] = 0.0;
            return rms(isolated([&](TapeEngine& e){ set_fx(e, 0,0,0,0.4f); set_filter(e, cutoff, reso); }, 60, 80, fin));
        };
        bool f1 = true, f2 = true, f3 = true, f4 = true;
        const float ro = frms(1.0f, 0.0f, f1);   // cutoff open
        const float rc = frms(0.2f, 0.0f, f2);   // cutoff low
        std::printf("filter: rms_open=%.4f rms_closed=%.4f  finite=%d\n", ro, rc, (int)(f1 && f2));
        check(f1 && f2,           "filter output is finite");
        check(rc < 0.30f * ro,    "low cutoff attenuates a 6 kHz tone (low-pass is wired)");
        // resonance: a tone near the corner (cutoff~0.81 -> fc~6 kHz) is boosted at high Q vs flat,
        // and the output soft-limiter keeps even the resonant peak bounded near 0 dBFS.
        const float rflat = frms(0.81f, 0.0f, f3);   // Q ~ 0.7 (flat)
        stream.phase[0] = 0.0;
        const auto pkv = isolated([&](TapeEngine& e){ set_fx(e, 0,0,0,0.4f); set_filter(e, 0.81f, 0.9f); }, 60, 80, f4); // Q ~ 8
        const float rpeak = rms(pkv);
        std::printf("filter reso: rms_flat=%.4f rms_peak=%.4f  peak=%.3f  finite=%d\n", rflat, rpeak, peak(pkv), (int)(f3 && f4));
        check(f3 && f4,             "resonant filter output is finite");
        check(rpeak > 1.3f * rflat, "resonance boosts a tone near the cutoff (Q is wired)");
        check(peak(pkv) < 1.5f,     "soft-limiter bounds the resonant peak near 0 dBFS");
    }

    if (g_failures == 0) { std::printf("OK: all tape FX checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
