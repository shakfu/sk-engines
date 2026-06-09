// Headless test for the reverb engine (Faust-generated Dattorro plate + Zita-rev1 hall; Alt+PITCH
// selects). Exercised through the public IEngine surface only:
//   1. Param round-trip (set_param/param) for the six knob roles + the Aux selector.
//   2. Both algorithms: output is finite, a wet setting rings out (a tail), wet tail > dry tail.
//   3. Every reverb binds all SIX knob roles - sweeping each role 0->1 changes the output. This is the
//      regression guard for the one silent failure mode of the bind-table design: a role bound to no
//      Faust zone would no-op a knob with no compile error.
//   4. The Aux selector: it quantizes to a reverb index (round(v*(N-1))) and actually swaps the kernel
//      - plate and hall produce different output, and Aux values either side of the 0.5 boundary pick
//      the expected algorithm (bit-identical output to the endpoint they round to).

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
std::vector<float> response(EngineContext& ctx, float aux, float mix, ParamId role, float roleval) {
    ReverbEngine e;
    e.init(ctx);
    e.set_param(ParamId::Aux,    DeckRef::A, aux); // pick algorithm first (re-applies cached knobs)
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
        for (size_t i = 0; i < host::kBlock; i++) out.push_back(ol[i]);
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

const char* kAlgo[2] = { "plate", "hall" };
const struct { ParamId id; const char* name; } kRoles[6] = {
    { ParamId::Mix,    "Mix"    }, { ParamId::Pos,   "Decay" }, { ParamId::Env,    "Damp"  },
    { ParamId::Speed,  "Tone"   }, { ParamId::Size,  "SizeA" }, { ParamId::ModAmp, "SizeB" },
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
        e.set_param(ParamId::Aux, DeckRef::A, 1.0f); check(approx(e.param(ParamId::Aux, DeckRef::A), 1.0f), "Aux param round-trips (1.0)");
        e.set_param(ParamId::Aux, DeckRef::A, 0.0f); check(approx(e.param(ParamId::Aux, DeckRef::A), 0.0f), "Aux param round-trips (0.0)");
    }

    // --- 2. Both algorithms: finite, wet rings out, wet tail > dry tail -------------------------
    for (int a = 0; a < ReverbEngine::kReverbCount; a++) {
        const float aux = ReverbEngine::kReverbCount > 1 ? float(a) / (ReverbEngine::kReverbCount - 1) : 0.f;
        const auto wet = response(ctx, aux, 1.0f, ParamId::Count, 0.f);
        const auto dry = response(ctx, aux, 0.0f, ParamId::Count, 0.f);
        const float wt = tail_energy(wet), dt = tail_energy(dry);
        std::printf("[%s] finite=%d  wet_tail=%.3f  dry_tail=%.3f\n", kAlgo[a], (int)(all_finite(wet) && all_finite(dry)), wt, dt);
        check(all_finite(wet) && all_finite(dry), "output is finite");
        check(wt > 1.0f, "wet setting produces a reverb tail");
        check(wt > dt * 2.0f, "wet tail exceeds dry tail (Mix wired + reverberation present)");
    }

    // --- 3. Every reverb binds all six knob roles (sweeping each 0->1 changes the output) -------
    for (int a = 0; a < ReverbEngine::kReverbCount; a++) {
        const float aux = ReverbEngine::kReverbCount > 1 ? float(a) / (ReverbEngine::kReverbCount - 1) : 0.f;
        for (auto r : kRoles) {
            std::vector<float> lo, hi;
            if (r.id == ParamId::Mix) { lo = response(ctx, aux, 0.0f, ParamId::Count, 0.f); hi = response(ctx, aux, 1.0f, ParamId::Count, 0.f); }
            else                      { lo = response(ctx, aux, 1.0f, r.id, 0.0f);          hi = response(ctx, aux, 1.0f, r.id, 1.0f); }
            const float d = sad(lo, hi);
            std::printf("[%s] role %-5s sweep SAD=%.3f\n", kAlgo[a], r.name, d);
            check(d > 1.0f, "knob role audibly drives the output (role is bound to a Faust zone)");
        }
    }

    // --- 4. Aux selector: quantizes to an index and swaps the kernel ----------------------------
    {
        const auto plate    = response(ctx, 0.0f, 1.0f, ParamId::Count, 0.f);
        const auto plate_lo = response(ctx, 0.4f, 1.0f, ParamId::Count, 0.f); // round(0.4)=0 -> plate
        const auto hall     = response(ctx, 1.0f, 1.0f, ParamId::Count, 0.f);
        const auto hall_hi  = response(ctx, 0.6f, 1.0f, ParamId::Count, 0.f); // round(0.6)=1 -> hall
        std::printf("aux: SAD(plate,plate@0.4)=%.3f  SAD(hall,hall@0.6)=%.3f  SAD(plate,hall)=%.3f\n",
                    sad(plate, plate_lo), sad(hall, hall_hi), sad(plate, hall));
        check(sad(plate, plate_lo) == 0.f, "Aux 0.4 selects the same algorithm as 0.0 (plate)");
        check(sad(hall,  hall_hi)  == 0.f, "Aux 0.6 selects the same algorithm as 1.0 (hall)");
        check(sad(plate, hall) > 1.0f,     "plate and hall produce different output (Aux swaps the kernel)");
    }

    if (g_failures == 0) { std::printf("OK: all reverb checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
