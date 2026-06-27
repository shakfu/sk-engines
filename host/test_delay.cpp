// Headless test for the delay engine (tempo-synced; Clean/Tape/Shimmer characters; Stereo/DoubleMono/
// Ping-pong topologies). Exercised through the public IEngine surface. Transport is null in the host
// context, so the delay falls back to 120 BPM. The shortest division (SIZE = 0 -> 1/16T) keeps echoes
// fast (~4000 samples) so a short tail captures several.
//   1. A wet, fed-back delay rings out (tail energy); more feedback -> a longer tail.
//   2. The three characters (Clean/Tape/Shimmer) all stay finite and produce distinct output.
//   3. Ping-pong cross-feeds: feeding only deck A produces deck-B output; DoubleMono does not.
//   4. The ENV feedback-tone control changes the output.
//   5. Stability: high feedback in every mode stays finite and bounded.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/delay/delay_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool cond, const char* msg) { if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; } }

constexpr int kBurst = 10, kTail = 180;
struct LCG { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (int32_t(s) >> 8) * (1.f / 8388608.f); } };

struct Stereo { std::vector<float> l, r; };

// Run the engine over a noise burst (both channels unless mono_A) + a silent tail; capture L/R out.
Stereo run(DelayEngine& e, bool mono_A = false) {
    LCG rng{ 4242u };
    Stereo o;
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    for (int b = 0; b < kBurst + kTail; b++) {
        for (size_t i = 0; i < host::kBlock; i++) {
            const float x = (b < kBurst) ? 0.5f * rng.next() : 0.f;
            il[i] = x;
            ir[i] = mono_A ? 0.f : x; // mono_A: drive deck A only (for the ping-pong cross-feed test)
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

// Configure a fresh delay: SIZE=0 (fast div), mix/feedback, Clean, given route. Returns by out-param.
void cfg(DelayEngine& e, EngineContext& ctx, float fb, float mix, int mode, int route) {
    e.init(ctx);
    e.set_config(ConfigId::Route, DeckRef::A, route);
    for (auto d : { DeckRef::A, DeckRef::B }) {
        e.set_config(ConfigId::Mode, d, mode);
        e.set_param(ParamId::Size,  d, 0.0f); // shortest division
        e.set_param(ParamId::Pos,   d, fb);   // feedback
        e.set_param(ParamId::Mix,   d, mix);
        e.set_param(ParamId::Speed, d, 0.5f); // unity output pitch
        e.set_param(ParamId::Env,   d, 1.0f); // open tone
    }
}
} // namespace

int main() {
    host::TimeSource time; host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);
    ctx.transport = nullptr; // host has no transport service; the delay falls back to 120 BPM

    // --- 1. Wet + feedback rings out; more feedback -> longer tail ------------------------------
    {
        DelayEngine lo, hi, dry;
        cfg(lo,  ctx, 0.4f, 0.6f, DelayEngine::Clean, 1); cfg(hi, ctx, 0.85f, 0.6f, DelayEngine::Clean, 1);
        cfg(dry, ctx, 0.6f, 0.0f, DelayEngine::Clean, 1);
        const auto a = run(lo), b = run(hi), d = run(dry);
        std::printf("clean: tail(fb.4)=%.2f tail(fb.85)=%.2f tail(dry)=%.4f\n", tail(a.l), tail(b.l), tail(d.l));
        check(finite(a.l) && finite(b.l), "clean output finite");
        check(tail(a.l) > 1.0f, "a wet, fed-back delay rings out");
        check(tail(b.l) > tail(a.l), "more feedback -> longer tail");
        check(tail(d.l) < 1e-2f, "dry (mix 0) leaves no tail");
    }

    // --- 2. Characters: Clean / Tape / Shimmer are distinct and finite -------------------------
    {
        DelayEngine cl, tp, sh;
        cfg(cl, ctx, 0.7f, 0.7f, DelayEngine::Clean,   1);
        cfg(tp, ctx, 0.7f, 0.7f, DelayEngine::Tape,    1);
        cfg(sh, ctx, 0.7f, 0.7f, DelayEngine::Shimmer, 1);
        const auto c = run(cl), t = run(tp), s = run(sh);
        std::printf("modes: SAD(clean,tape)=%.2f SAD(clean,shimmer)=%.2f\n", sad(c.l, t.l), sad(c.l, s.l));
        check(finite(c.l) && finite(t.l) && finite(s.l), "all characters finite");
        check(sad(c.l, t.l) > 1.0f, "Tape differs from Clean");
        check(sad(c.l, s.l) > 1.0f, "Shimmer differs from Clean");
    }

    // --- 3. Ping-pong cross-feeds; DoubleMono does not -----------------------------------------
    {
        DelayEngine pp, dm;
        cfg(pp, ctx, 0.7f, 0.8f, DelayEngine::Clean, 2); // GenerativeStereo -> ping-pong
        cfg(dm, ctx, 0.7f, 0.8f, DelayEngine::Clean, 1); // DoubleMono
        const auto p = run(pp, /*mono_A=*/true), d = run(dm, /*mono_A=*/true); // drive deck A only
        std::printf("pingpong: deckB tail pp=%.2f  doublemono=%.4f\n", tail(p.r), tail(d.r));
        check(finite(p.l) && finite(p.r), "ping-pong output finite");
        check(tail(p.r) > 1.0f, "ping-pong: driving deck A produces deck-B output (cross-feedback)");
        check(tail(d.r) < 1e-2f, "DoubleMono: a silent deck B input stays silent (no cross-feed)");
    }

    // --- 4. ENV feedback-tone changes the output ------------------------------------------------
    {
        DelayEngine open, dark;
        cfg(open, ctx, 0.7f, 0.7f, DelayEngine::Clean, 1);
        cfg(dark, ctx, 0.7f, 0.7f, DelayEngine::Clean, 1);
        for (auto deck : { DeckRef::A, DeckRef::B }) dark.set_param(ParamId::Env, deck, 0.2f); // darken
        const auto o = run(open), k = run(dark);
        std::printf("tone: SAD(open,dark)=%.2f\n", sad(o.l, k.l));
        check(sad(o.l, k.l) > 1.0f, "the ENV feedback-tone control changes the output");
    }

    // --- 5. Stability at high feedback, every character -----------------------------------------
    for (int mode = 0; mode < DelayEngine::ModeCount; mode++) {
        DelayEngine e; cfg(e, ctx, 1.0f, 0.8f, mode, 1); // POS=1 -> feedback capped at 0.95
        const auto o = run(e);
        std::printf("stability[mode %d]: finite=%d maxabs=%.3f\n", mode, int(finite(o.l)), maxabs(o.l));
        check(finite(o.l), "high-feedback output stays finite");
        check(maxabs(o.l) < 8.0f, "high-feedback output stays bounded (soft-clipped, no runaway)");
    }

    // --- 6. Modulation: MODFREQ + MOD_AMT modulate the delay time -------------------------------
    {
        DelayEngine plain, modu;
        cfg(plain, ctx, 0.6f, 0.7f, DelayEngine::Clean, 1);
        cfg(modu,  ctx, 0.6f, 0.7f, DelayEngine::Clean, 1);
        for (auto d : { DeckRef::A, DeckRef::B }) { modu.set_mod_speed(d, 0.6f, false); modu.set_param(ParamId::ModAmp, d, 0.8f); }
        const auto a = run(plain), b = run(modu);
        std::printf("mod: SAD(plain,modulated)=%.2f\n", sad(a.l, b.l));
        check(finite(b.l), "modulated output finite");
        check(sad(a.l, b.l) > 1.0f, "MODFREQ + MOD_AMT modulate the delay time (chorus/vibrato)");
    }

    // --- 7. Freeze (Play pad): the loop sustains; an unfrozen delay decays ----------------------
    {
        auto late_tail = [&](bool freeze) {
            DelayEngine e; cfg(e, ctx, 0.3f, 1.0f, DelayEngine::Clean, 1); // fb 0.3 (decays fast), wet only
            LCG rng{ 99u };
            float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
            const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
            const int prime = 60, hold = 220; // prime > one delay period; hold = several periods of silence
            float late = 0.f;
            for (int b = 0; b < prime + hold; b++) {
                if (b == prime && freeze) { e.on_play_pad(DeckRef::A, false); e.on_play_pad(DeckRef::B, false); }
                for (size_t i = 0; i < host::kBlock; i++) { const float x = (b < prime) ? 0.5f * rng.next() : 0.f; il[i] = x; ir[i] = x; }
                e.process(in, out, host::kBlock);
                if (b >= prime + hold - 40) for (size_t i = 0; i < host::kBlock; i++) late += std::fabs(ol[i]);
            }
            return late;
        };
        const float fz = late_tail(true), nf = late_tail(false);
        std::printf("freeze: late-tail frozen=%.2f unfrozen=%.4f\n", fz, nf);
        check(fz > 1.0f, "Freeze sustains the loop");
        check(fz > nf * 5.0f, "Freeze sustains far longer than an unfrozen (decaying) delay");
    }

    // --- 8. Reverse (Rev pad): backward read differs from forward, stays finite/bounded ---------
    {
        // A forward delay and a reversed one, primed from the same noise burst. The Rev pad is toggled
        // on the reversed instance before the silent tail; their tails must diverge but both behave.
        auto run_rev = [&](bool rev) {
            DelayEngine e; cfg(e, ctx, 0.6f, 1.0f, DelayEngine::Clean, 1); // wet only, moderate feedback
            if (rev) { e.on_play_pad(DeckRef::A, true); e.on_play_pad(DeckRef::B, true); }
            return run(e);
        };
        const auto fwd = run_rev(false), rev = run_rev(true);
        std::printf("reverse: SAD(fwd,rev)=%.2f tail(rev)=%.2f maxabs(rev)=%.3f\n",
                    sad(fwd.l, rev.l), tail(rev.l), maxabs(rev.l));
        check(finite(rev.l) && finite(rev.r), "reverse output finite");
        check(tail(rev.l) > 1.0f, "a reversed delay still rings out");
        check(sad(fwd.l, rev.l) > 1.0f, "reverse read differs from forward");
        check(maxabs(rev.l) < 8.0f, "reverse output stays bounded");

        // Toggling the Rev pad twice returns to forward behaviour (idempotent gesture).
        DelayEngine e; cfg(e, ctx, 0.6f, 1.0f, DelayEngine::Clean, 1);
        e.on_play_pad(DeckRef::A, true); e.on_play_pad(DeckRef::A, true);
        e.on_play_pad(DeckRef::B, true); e.on_play_pad(DeckRef::B, true);
        const auto back = run(e);
        std::printf("reverse: SAD(fwd,toggled-back)=%.4f\n", sad(fwd.l, back.l));
        check(sad(fwd.l, back.l) < 1e-3f, "Rev toggled twice == forward");
    }

    if (g_failures == 0) { std::printf("OK: all delay checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
