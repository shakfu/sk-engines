// Smoke test for the SPK_REVERB_GIGAVERB build: the gen~ "gigaverb" folded into ReverbEngine as a
// third voice. Validates compile/link of the GigaverbVoice path plus basic runtime behaviour through
// the public IEngine surface: selecting the third algorithm via the Mode switch yields finite output, a wet tail,
// and distinct output from the two Faust voices. NOT part of `make test` (needs the gen~ sources);
// built ad hoc to verify the integration.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/reverb/reverb_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool cond, const char* msg) { if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; } }

constexpr int kBurst = 10, kTail = 200;
struct LCG { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (int32_t(s) >> 8) * (1.f / 8388608.f); } };

std::vector<float> response(EngineContext& ctx, int voice, float mix) {
    ReverbEngine e; e.init(ctx);
    e.set_config(ConfigId::Mode, DeckRef::A, voice);
    e.set_param(ParamId::Mix,    DeckRef::A, mix);
    e.set_param(ParamId::Pos,    DeckRef::A, 0.5f);
    e.set_param(ParamId::Env,    DeckRef::A, 0.5f);
    e.set_param(ParamId::Speed,  DeckRef::A, 0.5f);
    e.set_param(ParamId::Size,   DeckRef::A, 0.5f);
    e.set_param(ParamId::ModAmp, DeckRef::A, 0.5f);
    LCG rng{ 12345u };
    std::vector<float> out;
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* o[2] = { ol, orr };
    for (int b = 0; b < kBurst + kTail; b++) {
        for (size_t i = 0; i < host::kBlock; i++) { const float x = (b < kBurst) ? 0.5f * rng.next() : 0.f; il[i] = x; ir[i] = x; }
        e.process(in, o, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) out.push_back(ol[i]);
    }
    return out;
}
bool all_finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
float sad(const std::vector<float>& a, const std::vector<float>& b) { float s = 0.f; const size_t n = std::min(a.size(), b.size()); for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]); return s; }
float tail_energy(const std::vector<float>& v) { float s = 0.f; for (size_t i = size_t(kBurst) * host::kBlock; i < v.size(); i++) s += std::fabs(v[i]); return s; }
} // namespace

int main() {
    host::TimeSource time; host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    static_assert(ReverbEngine::kReverbCount == 3, "expected 3 voices under SPK_REVERB_GIGAVERB");
    const int kPlate = 0, kHall = 1, kGiga = 2; // Mode-switch positions -> voice index

    const auto wet = response(ctx, kGiga, 1.0f);
    const auto dry = response(ctx, kGiga, 0.0f);
    const float wt = tail_energy(wet), dt = tail_energy(dry);
    std::printf("[gigaverb] finite=%d  wet_tail=%.3f  dry_tail=%.3f\n", int(all_finite(wet) && all_finite(dry)), wt, dt);
    check(all_finite(wet) && all_finite(dry), "gigaverb output is finite");
    check(wt > 1.0f, "gigaverb wet setting produces a tail");
    check(wt > dt * 2.0f, "gigaverb wet tail exceeds dry tail (Mix wired)");

    const auto plate = response(ctx, kPlate, 1.0f);
    const auto hall  = response(ctx, kHall,  1.0f);
    std::printf("[gigaverb] SAD(giga,plate)=%.3f  SAD(giga,hall)=%.3f\n", sad(wet, plate), sad(wet, hall));
    check(sad(wet, plate) > 1.0f, "gigaverb differs from plate (Mode switch selects the gen~ voice)");
    check(sad(wet, hall)  > 1.0f, "gigaverb differs from hall");

    if (g_failures == 0) { std::printf("OK: gigaverb integration checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
