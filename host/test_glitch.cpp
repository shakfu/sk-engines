// Headless test for the glitch engine (dual-deck lo-fi noise; 12 Noisferatu algorithms). Drives the
// GlitchEngine through its public IEngine surface and checks, for every algorithm: finite + bounded
// output, that it actually produces signal, that the two algorithm params and master pitch are wired,
// that Alt+PITCH (Aux) selects the algorithm with correct readback, that the Play-pad buffer regen and
// the routing switch behave, and that the two decks are independent (decorrelated by seed).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/glitch/glitch_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

struct Stereo { std::vector<float> l, r; };

// Run `blocks` audio blocks; collect both output channels; clears `finite` on any non-finite sample.
Stereo run(GlitchEngine& e, int blocks, bool& finite) {
    float il[host::kBlock] = {0}, ir[host::kBlock] = {0}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    Stereo s;
    for (int b = 0; b < blocks; b++) {
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            s.l.push_back(ol[i]); s.r.push_back(orr[i]);
        }
    }
    return s;
}
float peak(const std::vector<float>& v) { float p = 0.f; for (float x : v) p = std::fmax(p, std::fabs(x)); return p; }
float energy(const std::vector<float>& v) { float s = 0.f; for (float x : v) s += std::fabs(x); return s; }
float sad(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.f; size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]);
    return s;
}

const char* kNames[] = {
    "SparseGlitch", "WanderWindow", "BitMangle", "TriXorTri", "SquareNand", "FmNoise",
    "RingMod", "PhrygianTri", "PentaBlips", "BernoulliTris", "Dust", "NoiseRhythm"
};

// Select an algorithm on a deck via the Aux selector (the platform's Alt+PITCH channel).
void select_algo(GlitchEngine& e, DeckRef::Ref d, int idx) {
    e.set_param(ParamId::Aux, d, (static_cast<float>(idx) + 0.5f) / static_cast<float>(glitch::kAlgoCount));
}

} // namespace

int main() {
    host::TimeSource time;
    auto make = [&](GlitchEngine& e) {
        host::HostArena a; EngineContext c = host::make_context(a, time);
        e.init(c);
    };

    check(glitch::kAlgoCount == 12, "there are 12 curated algorithms");

    // 1. Every algorithm: finite, bounded near 0 dBFS, and actually produces signal. Deck B is muted so
    //    only deck A's algorithm under test reaches the bus. Params set toward "active" settings.
    for (int idx = 0; idx < glitch::kAlgoCount; idx++) {
        GlitchEngine e; make(e);
        e.set_param(ParamId::Mix, DeckRef::B, 0.f);          // mute the other deck
        e.set_param(ParamId::Mix, DeckRef::A, 1.f);
        select_algo(e, DeckRef::A, idx);
        e.set_param(ParamId::Size, DeckRef::A, 0.7f);        // p1
        e.set_param(ParamId::Pos,  DeckRef::A, 0.6f);        // p2
        bool fin = true;
        const Stereo s = run(e, 400, fin);                   // ~38k samples: enough to hit sparse material
        char msg[96];
        std::snprintf(msg, sizeof(msg), "%s: output finite", kNames[idx]);   check(fin, msg);
        std::snprintf(msg, sizeof(msg), "%s: output bounded", kNames[idx]);  check(peak(s.l) <= 1.05f, msg);
        std::snprintf(msg, sizeof(msg), "%s: produces signal", kNames[idx]); check(energy(s.l) > 0.f, msg);
    }

    // 2. Aux selects the algorithm and reads back the selected index.
    {
        GlitchEngine e; make(e);
        select_algo(e, DeckRef::A, 5);   // FmNoise
        check(e.param(ParamId::Aux, DeckRef::A) > 5.f / 12.f && e.param(ParamId::Aux, DeckRef::A) < 6.f / 12.f,
              "aux: readback reports the selected algorithm");
        select_algo(e, DeckRef::A, 0);
        check(e.param(ParamId::Aux, DeckRef::A) < 1.f / 12.f, "aux: re-select moves to a different algorithm");
    }

    // 3. The two params and master pitch are wired: changing each changes the output of a deterministic
    //    (non-stochastic) tonal algorithm. TriXorTri's timbre is pure phase, so a param sweep must differ.
    {
        auto render_tri = [&](float p1, float p2, float pitch) {
            GlitchEngine e; make(e);
            e.set_param(ParamId::Mix, DeckRef::B, 0.f);
            select_algo(e, DeckRef::A, 3);   // TriXorTri
            e.set_param(ParamId::Size, DeckRef::A, p1);
            e.set_param(ParamId::Pos,  DeckRef::A, p2);
            e.set_param(ParamId::Speed, DeckRef::A, pitch);
            bool fin = true; return run(e, 60, fin).l;
        };
        const auto base = render_tri(0.3f, 0.3f, 0.5f);
        check(sad(base, render_tri(0.8f, 0.3f, 0.5f)) > 1.f, "p1 (SIZE) changes the output");
        check(sad(base, render_tri(0.3f, 0.8f, 0.5f)) > 1.f, "p2 (POS) changes the output");
        check(sad(base, render_tri(0.3f, 0.3f, 0.8f)) > 1.f, "master pitch (PITCH) changes the output");
    }

    // 4. ENV tone control: closing it (dark) attenuates a noisy algorithm vs fully open.
    {
        auto render_noise = [&](float env) {
            GlitchEngine e; make(e);
            e.set_param(ParamId::Mix, DeckRef::B, 0.f);
            select_algo(e, DeckRef::A, 10);   // Dust
            e.set_param(ParamId::Size, DeckRef::A, 1.f);   // dense
            e.set_param(ParamId::Pos,  DeckRef::A, 1.f);   // open dust filter
            e.set_param(ParamId::Env,  DeckRef::A, env);
            bool fin = true; return run(e, 120, fin).l;
        };
        const float open = energy(render_noise(1.0f));
        const float dark = energy(render_noise(0.05f));
        check(open > dark, "ENV tone: closing the low-pass reduces high-frequency energy");
    }

    // 5. Play pad regenerates a buffer-player algorithm's buffer; output stays finite/bounded after.
    {
        GlitchEngine e; make(e);
        e.set_param(ParamId::Mix, DeckRef::B, 0.f);
        select_algo(e, DeckRef::A, 0);   // SparseGlitch
        bool fin = true; run(e, 50, fin);
        e.on_play_pad(DeckRef::A, /*reverse=*/false);   // regen
        const Stereo s = run(e, 200, fin);
        check(fin, "regen: output finite after a Play-pad buffer regenerate");
        check(peak(s.l) <= 1.05f, "regen: output bounded after regenerate");
    }

    // 6. Routing: DoubleMono places deck A hard-left and deck B hard-right (no bleed across channels when
    //    one deck is muted).
    {
        GlitchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);    // DoubleMono
        check(e.route() == Route::DoubleMono, "route: switch position 1 -> DoubleMono");
        select_algo(e, DeckRef::A, 10);                  // Dust on A
        e.set_param(ParamId::Size, DeckRef::A, 1.f);
        e.set_param(ParamId::Mix, DeckRef::B, 0.f);      // mute B
        bool fin = true; const Stereo s = run(e, 120, fin);
        check(energy(s.l) > 0.f, "route: deck A reaches the left channel");
        check(energy(s.r) == 0.f, "route: muted deck B leaves the right channel silent (no bleed)");
    }

    // 7. Deck independence: the same stochastic algorithm on both decks, hard-panned, must decorrelate
    //    (the two Voices carry distinct PRNG seeds - the global-state bug the per-instance port fixes).
    {
        GlitchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);    // DoubleMono: A->L, B->R
        for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
            select_algo(e, d, 11);                       // NoiseRhythm (rng-driven)
            e.set_param(ParamId::Size, d, 0.6f);
            e.set_param(ParamId::Pos,  d, 0.5f);
        }
        bool fin = true; const Stereo s = run(e, 200, fin);
        check(fin, "independence: output finite");
        check(sad(s.l, s.r) > 1.f, "independence: decks A and B decorrelate (per-instance state)");
    }

    if (g_failures == 0) { std::printf("OK: all glitch checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
