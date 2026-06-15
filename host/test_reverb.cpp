// Headless test for the reverb engine (Faust-generated Dattorro plate + Zita-rev1 hall; the physical
// Reel/Slice/Drift mode switch selects per deck; route-aware). Exercised through the public IEngine surface:
//   1. Param round-trip (set_param/param) for the six knob roles, plus Mode and Route change/no-change
//      semantics (deck B's mode switch is a live per-deck selector now).
//   2. Both algorithms: output is finite, a wet setting rings out (a tail), wet tail > dry tail.
//   3. Every reverb binds all SIX knob roles - sweeping each role 0->1 changes the output. This is the
//      regression guard for the one silent failure mode of the bind-table design: a role bound to no
//      Faust zone would no-op a knob with no compile error.
//   4. The Mode switch selects the live reverb by index and swaps the kernel - plate and hall differ.
//   5. DoubleMono is plate-only (the cap): the Mode switch is ignored in DoubleMono (forced to plate, so
//      two heavy voices can never contend) but honored in a stereo route; and the two mono sides are
//      channel-isolated (a deck with a silent input stays silent).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/reverb/reverb_engine.h"
#include "engine/display_model.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}
bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

constexpr int kBurst = 10;   // blocks of noise excitation (~20 ms)
constexpr int kTail  = 200;  // blocks of silence to capture the ring-out (~0.4 s)

// Deterministic pseudo-noise so two runs differ only by the parameter under test.
struct LCG {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return (static_cast<int32_t>(s) >> 8) * (1.f / 8388608.f); }
};

// Run one reverb config to completion and return the left-channel output (burst + tail). A fresh
// engine per call (re-init zeroes the kernel state); calls are sequential so sharing the arena is fine.
// aux selects the algorithm; the six knobs are set to mid, then `mix` and (if not Count) `role` override.
std::vector<float> response(EngineContext& ctx, int voice, float mix, ParamId role, float roleval) {
    ReverbEngine e;
    e.init(ctx);
    e.set_config(ConfigId::Mode, DeckRef::A, voice); // pick algorithm first (re-applies cached knobs)
    e.set_param(ParamId::Mix,    DeckRef::A, mix);
    e.set_param(ParamId::Pos,    DeckRef::A, 0.5f);
    e.set_param(ParamId::Env,    DeckRef::A, 0.5f);
    e.set_param(ParamId::Speed,  DeckRef::A, 0.5f);
    e.set_param(ParamId::Size,   DeckRef::A, 0.5f);
    e.set_param(ParamId::ModAmp, DeckRef::A, 0.5f);
    if (role != ParamId::Count) e.set_param(role, DeckRef::A, roleval);

    LCG rng{ 12345u };
    std::vector<float> out;
    out.reserve(static_cast<size_t>(kBurst + kTail) * host::kBlock);
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* o[2] = { ol, orr };
    for (int b = 0; b < kBurst + kTail; b++) {
        for (size_t i = 0; i < host::kBlock; i++) { const float x = (b < kBurst) ? 0.5f * rng.next() : 0.f; il[i] = x; ir[i] = x; }
        e.process(in, o, host::kBlock);
        // Sum both channels: a stereo-only effect (e.g. Greyhole's per-channel modulation, or a spread that
        // detunes only the right channel) must still register as driving the output, which left-only misses.
        for (size_t i = 0; i < host::kBlock; i++) out.push_back(ol[i] + orr[i]);
    }
    return out;
}

bool all_finite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
float sad(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.f; const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]);
    return s;
}
float tail_energy(const std::vector<float>& v) {
    float s = 0.f;
    for (size_t i = static_cast<size_t>(kBurst) * host::kBlock; i < v.size(); i++) s += std::fabs(v[i]);
    return s;
}

const char* kAlgo[3] = { "plate", "hall", "greyhole" };
const struct { ParamId id; const char* name; } kRoles[6] = {
    { ParamId::Mix,    "Mix"    }, { ParamId::Speed, "Decay" }, { ParamId::Env,    "Damp"  },
    { ParamId::Pos,    "Tone"   }, { ParamId::Size,  "SizeA" }, { ParamId::ModAmp, "SizeB" },
};

} // namespace

int main() {
    host::TimeSource time;
    host::HostArena  arena;
    EngineContext ctx = host::make_context(arena, time);

    // --- 1. Param round-trip -------------------------------------------------------------------
    {
        ReverbEngine e; e.init(ctx);
        for (auto r : kRoles) { e.set_param(r.id, DeckRef::A, 0.37f);
            check(approx(e.param(r.id, DeckRef::A), 0.37f), "knob param round-trips"); }
        check(e.set_config(ConfigId::Mode, DeckRef::A, 1),  "Mode switch to a new voice reports a change");
        check(!e.set_config(ConfigId::Mode, DeckRef::A, 1), "Mode switch to the same voice reports no change");
        check(e.set_config(ConfigId::Mode, DeckRef::B, 1),  "deck B's mode switch is a live per-deck selector");
        check(e.set_config(ConfigId::Route, DeckRef::A, 1),  "Route change reports a change");
        check(!e.set_config(ConfigId::Route, DeckRef::A, 1), "same Route reports no change");
    }

    // --- 2. Both algorithms: finite, wet rings out, wet tail > dry tail -------------------------
    for (int a = 0; a < ReverbEngine::kReverbCount; a++) {
        const auto wet = response(ctx, a, 1.0f, ParamId::Count, 0.f);
        const auto dry = response(ctx, a, 0.0f, ParamId::Count, 0.f);
        const float wt = tail_energy(wet), dt = tail_energy(dry);
        std::printf("[%s] finite=%d  wet_tail=%.3f  dry_tail=%.3f\n", kAlgo[a], (int)(all_finite(wet) && all_finite(dry)), wt, dt);
        check(all_finite(wet) && all_finite(dry), "output is finite");
        check(wt > 1.0f, "wet setting produces a reverb tail");
        check(wt > dt * 2.0f, "wet tail exceeds dry tail (Mix wired + reverberation present)");
    }

    // --- 3. Every reverb binds all six knob roles (sweeping each 0->1 changes the output) -------
    for (int a = 0; a < ReverbEngine::kReverbCount; a++) {
        for (auto r : kRoles) {
            std::vector<float> lo, hi;
            if (r.id == ParamId::Mix) { lo = response(ctx, a, 0.0f, ParamId::Count, 0.f); hi = response(ctx, a, 1.0f, ParamId::Count, 0.f); }
            else                      { lo = response(ctx, a, 1.0f, r.id, 0.0f);          hi = response(ctx, a, 1.0f, r.id, 1.0f); }
            const float d = sad(lo, hi);
            std::printf("[%s] role %-5s sweep SAD=%.3f\n", kAlgo[a], r.name, d);
            check(d > 1.0f, "knob role audibly drives the output (role is bound to a Faust zone)");
        }
    }

    // --- 4. Mode switch (deck A) selects the voice; the two voices differ -----------------------
    {
        const auto plate = response(ctx, 0, 1.0f, ParamId::Count, 0.f); // Mode pos 0 -> plate
        const auto hall  = response(ctx, 1, 1.0f, ParamId::Count, 0.f); // Mode pos 1 -> hall
        std::printf("mode: SAD(plate,hall)=%.3f\n", sad(plate, hall));
        check(sad(plate, hall) > 1.0f, "plate and hall produce different output (Mode switch swaps the kernel)");
    }

    // --- 5. DoubleMono = plate-only cap; hall/greyhole are stereo-only --------------------------
    {
        float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
        const float* in[2] = { il, ir }; float* o[2] = { ol, orr };

        // Run deck A in DoubleMono with a given mode, return its left-channel response.
        auto dm_deckA = [&](int modeA) {
            ReverbEngine e; e.init(ctx);
            e.set_config(ConfigId::Route, DeckRef::A, 1); // DoubleMono
            e.set_config(ConfigId::Mode, DeckRef::A, modeA);
            e.set_param(ParamId::Mix, DeckRef::A, 1.0f); e.set_param(ParamId::Speed, DeckRef::A, 0.5f);
            e.set_param(ParamId::Env, DeckRef::A, 0.5f);  e.set_param(ParamId::Pos, DeckRef::A, 0.5f);
            e.set_param(ParamId::Size, DeckRef::A, 0.5f); e.set_param(ParamId::ModAmp, DeckRef::A, 0.5f);
            LCG rng{ 777u }; std::vector<float> out;
            for (int blk = 0; blk < kBurst + kTail; blk++) {
                for (size_t i = 0; i < host::kBlock; i++) { const float x = (blk < kBurst) ? 0.5f * rng.next() : 0.f; il[i] = x; ir[i] = x; }
                e.process(in, o, host::kBlock);
                for (size_t i = 0; i < host::kBlock; i++) out.push_back(ol[i]);
            }
            return out;
        };
        // 5a. The cap: in DoubleMono, selecting Hall does NOT change the output (it's forced to plate).
        const auto dm_plate = dm_deckA(0); // mode = plate
        const auto dm_hall  = dm_deckA(1); // mode = hall -> capped to plate in DoubleMono
        std::printf("cap: finite=%d  SAD(DoubleMono plate, DoubleMono 'hall')=%.3f\n",
                    int(all_finite(dm_plate) && all_finite(dm_hall)), sad(dm_plate, dm_hall));
        check(all_finite(dm_plate), "doublemono output is finite");
        check(sad(dm_plate, dm_hall) == 0.f, "DoubleMono ignores the Mode switch (plate-only cap)");

        // 5b. ...but a stereo route DOES honor it (hall != plate), proving the cap is route-specific.
        const auto s_plate = response(ctx, 0, 1.0f, ParamId::Count, 0.f); // stereo, mode plate
        const auto s_hall  = response(ctx, 1, 1.0f, ParamId::Count, 0.f); // stereo, mode hall
        check(sad(s_plate, s_hall) > 1.0f, "stereo route honors the Mode switch (hall != plate)");

        // 5c. Channel isolation in DoubleMono: feed deck A only -> deck B (silent input) stays silent.
        ReverbEngine e; e.init(ctx);
        e.set_config(ConfigId::Route, DeckRef::A, 1); // DoubleMono (both plate)
        for (auto d : { DeckRef::A, DeckRef::B }) { e.set_param(ParamId::Mix, d, 1.0f); e.set_param(ParamId::Speed, d, 0.5f); }
        LCG rng2{ 888u }; std::vector<float> a, b;
        for (int blk = 0; blk < kBurst + kTail; blk++) {
            for (size_t i = 0; i < host::kBlock; i++) { il[i] = (blk < kBurst) ? 0.5f * rng2.next() : 0.f; ir[i] = 0.f; }
            e.process(in, o, host::kBlock);
            for (size_t i = 0; i < host::kBlock; i++) { a.push_back(ol[i]); b.push_back(orr[i]); }
        }
        std::printf("doublemono: deckA tail=%.3f  deckB(silent in) tail=%.6f\n", tail_energy(a), tail_energy(b));
        check(tail_energy(a) > 1.0f, "deck A reverberates its own input");
        check(tail_energy(b) < 1e-3f, "deck B stays silent when its input is silent (deck isolation, no crosstalk)");
    }

    if (g_failures == 0) { std::printf("OK: all reverb checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
