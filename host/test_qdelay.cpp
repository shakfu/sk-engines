// Headless test for the QDelay engine (delay grammar with a Clean/Diffuse/Duck palette + Reverse).
// Exercised through the public IEngine surface. Transport is null, so it falls back to 120 BPM.
//   1. A wet, fed-back delay rings out; more feedback -> a longer tail.
//   2. The three characters (Clean/Diffuse/Duck) are distinct and finite.
//   3. Diffuse smears the feedback -> differs from Clean, stays bounded.
//   4. Duck suppresses the wet under continuous input, then the tail rings out once input stops.
//   5. Ping-pong cross-feeds; DoubleMono does not.
//   6. Reverse (Rev pad) differs from forward and toggles back exactly.
//   7. Stability: high feedback in every character stays finite and bounded.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/qdelay/qdelay_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool cond, const char* msg) { if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; } }

constexpr int kBurst = 10, kTail = 180;
struct LCG { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (int32_t(s) >> 8) * (1.f / 8388608.f); } };

struct Stereo { std::vector<float> l, r; };

Stereo run(QdelayEngine& e, bool mono_A = false) {
    LCG rng{ 4242u };
    Stereo o;
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    for (int b = 0; b < kBurst + kTail; b++) {
        for (size_t i = 0; i < host::kBlock; i++) {
            const float x = (b < kBurst) ? 0.5f * rng.next() : 0.f;
            il[i] = x;
            ir[i] = mono_A ? 0.f : x;
        }
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) { o.l.push_back(ol[i]); o.r.push_back(orr[i]); }
    }
    return o;
}

bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
float maxabs(const std::vector<float>& v) { float m = 0.f; for (float x : v) m = std::fmax(m, std::fabs(x)); return m; }
float tail(const std::vector<float>& v) { float s = 0.f; for (size_t i = size_t(kBurst) * host::kBlock; i < v.size(); i++) s += std::fabs(v[i]); return s; }
float sad(const std::vector<float>& a, const std::vector<float>& b) { float s = 0.f; const size_t n = std::min(a.size(), b.size()); for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]); return s; }

void cfg(QdelayEngine& e, EngineContext& ctx, float fb, float mix, int mode, int route) {
    e.init(ctx);
    e.set_config(ConfigId::Route, DeckRef::A, route);
    for (auto d : { DeckRef::A, DeckRef::B }) {
        e.set_config(ConfigId::Mode, d, mode);
        e.set_param(ParamId::Size,  d, 0.0f); // shortest division
        e.set_param(ParamId::Pos,   d, fb);
        e.set_param(ParamId::Mix,   d, mix);
        e.set_param(ParamId::Speed, d, 0.5f); // unity output pitch
        e.set_param(ParamId::Env,   d, 1.0f); // open tone
    }
}
} // namespace

int main() {
    host::TimeSource time; host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);
    ctx.transport = nullptr;

    // --- 1. Wet + feedback rings out; more feedback -> longer tail ------------------------------
    {
        QdelayEngine lo, hi, dry;
        cfg(lo,  ctx, 0.4f, 0.6f, QdelayEngine::Clean, 1); cfg(hi, ctx, 0.85f, 0.6f, QdelayEngine::Clean, 1);
        cfg(dry, ctx, 0.6f, 0.0f, QdelayEngine::Clean, 1);
        const auto a = run(lo), b = run(hi), d = run(dry);
        std::printf("clean: tail(fb.4)=%.2f tail(fb.85)=%.2f tail(dry)=%.4f\n", tail(a.l), tail(b.l), tail(d.l));
        check(finite(a.l) && finite(b.l), "clean output finite");
        check(tail(a.l) > 1.0f, "a wet, fed-back delay rings out");
        check(tail(b.l) > tail(a.l), "more feedback -> longer tail");
        check(tail(d.l) < 1e-2f, "dry (mix 0) leaves no tail");
    }

    // --- 2/3. Characters Clean / Diffuse / Duck distinct, finite, bounded ------------------------
    {
        QdelayEngine cl, df, dk;
        cfg(cl, ctx, 0.7f, 0.7f, QdelayEngine::Clean,   1);
        cfg(df, ctx, 0.7f, 0.7f, QdelayEngine::Diffuse, 1);
        cfg(dk, ctx, 0.7f, 0.7f, QdelayEngine::Duck,    1);
        const auto c = run(cl), f = run(df), k = run(dk);
        std::printf("modes: SAD(clean,diffuse)=%.2f SAD(clean,duck)=%.2f maxabs(diffuse)=%.3f\n",
                    sad(c.l, f.l), sad(c.l, k.l), maxabs(f.l));
        check(finite(c.l) && finite(f.l) && finite(k.l), "all characters finite");
        check(sad(c.l, f.l) > 1.0f, "Diffuse differs from Clean");
        check(sad(c.l, k.l) > 1.0f, "Duck differs from Clean");
        check(maxabs(f.l) < 8.0f, "Diffuse stays bounded");
    }

    // --- 4. Duck: wet suppressed under continuous input; tail rings out once input stops --------
    {
        // (a) continuous loud input, wet-only: Clean passes the delay; Duck should suppress it.
        auto steady = [&](int mode) {
            QdelayEngine e; cfg(e, ctx, 0.6f, 1.0f, mode, 1); // mix=1 -> output is the (ducked) wet
            LCG rng{ 7u };
            float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
            const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
            const int blocks = 200; float e_steady = 0.f;
            for (int b = 0; b < blocks; b++) {
                for (size_t i = 0; i < host::kBlock; i++) { const float x = 0.5f * rng.next(); il[i] = x; ir[i] = x; }
                e.process(in, out, host::kBlock);
                if (b >= blocks - 60) for (size_t i = 0; i < host::kBlock; i++) e_steady += std::fabs(ol[i]);
            }
            return e_steady;
        };
        const float clean_e = steady(QdelayEngine::Clean), duck_e = steady(QdelayEngine::Duck);
        std::printf("duck: steady wet-energy clean=%.2f duck=%.2f\n", clean_e, duck_e);
        check(duck_e < clean_e * 0.5f, "Duck suppresses the wet under continuous input");

        // (b) burst then silence: the ducked tail rings out once the input releases.
        QdelayEngine e; cfg(e, ctx, 0.6f, 1.0f, QdelayEngine::Duck, 1);
        const auto o = run(e);
        std::printf("duck: post-input tail=%.2f\n", tail(o.l));
        check(finite(o.l) && tail(o.l) > 1.0f, "Duck tail rings out after the input stops");
    }

    // --- 5. Ping-pong cross-feeds; DoubleMono does not -----------------------------------------
    {
        QdelayEngine pp, dm;
        cfg(pp, ctx, 0.7f, 0.8f, QdelayEngine::Clean, 2);
        cfg(dm, ctx, 0.7f, 0.8f, QdelayEngine::Clean, 1);
        const auto p = run(pp, /*mono_A=*/true), d = run(dm, /*mono_A=*/true);
        std::printf("pingpong: deckB tail pp=%.2f doublemono=%.4f\n", tail(p.r), tail(d.r));
        check(tail(p.r) > 1.0f, "ping-pong: driving deck A produces deck-B output");
        check(tail(d.r) < 1e-2f, "DoubleMono: a silent deck B stays silent");
    }

    // --- 6. Reverse (Rev pad) differs from forward and toggles back exactly ----------------------
    {
        auto run_rev = [&](bool rev) {
            QdelayEngine e; cfg(e, ctx, 0.6f, 1.0f, QdelayEngine::Clean, 1);
            if (rev) { e.on_play_pad(DeckRef::A, true); e.on_play_pad(DeckRef::B, true); }
            return run(e);
        };
        const auto fwd = run_rev(false), rev = run_rev(true);
        std::printf("reverse: SAD(fwd,rev)=%.2f maxabs(rev)=%.3f\n", sad(fwd.l, rev.l), maxabs(rev.l));
        check(finite(rev.l) && tail(rev.l) > 1.0f, "a reversed delay still rings out");
        check(sad(fwd.l, rev.l) > 1.0f, "reverse read differs from forward");
        check(maxabs(rev.l) < 8.0f, "reverse output stays bounded");

        QdelayEngine e; cfg(e, ctx, 0.6f, 1.0f, QdelayEngine::Clean, 1);
        e.on_play_pad(DeckRef::A, true); e.on_play_pad(DeckRef::A, true);
        e.on_play_pad(DeckRef::B, true); e.on_play_pad(DeckRef::B, true);
        const auto back = run(e);
        check(sad(fwd.l, back.l) < 1e-3f, "Rev toggled twice == forward");
    }

    // --- 7. Stability at high feedback, every character -----------------------------------------
    for (int mode = 0; mode < QdelayEngine::ModeCount; mode++) {
        QdelayEngine e; cfg(e, ctx, 1.0f, 0.8f, mode, 1);
        const auto o = run(e);
        std::printf("stability[mode %d]: finite=%d maxabs=%.3f\n", mode, int(finite(o.l)), maxabs(o.l));
        check(finite(o.l), "high-feedback output stays finite");
        check(maxabs(o.l) < 8.0f, "high-feedback output stays bounded");
    }

    if (g_failures == 0) { std::printf("OK: all qdelay checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
