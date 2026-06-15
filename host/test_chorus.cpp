// Headless test for the generated `chorus` Faust engine - proves the shared FaustEngine<Traits> runtime
// (zone capture + range mapping + compute marshalling) works through the public IEngine surface:
//   1. param round-trip incl. the set_mod_speed -> ModSpeed routing (MODFREQ knob).
//   2. a fresh engine reports the kernel's own slider defaults (no boot jump).
//   3. output is finite + bounded, and the wet path (mix=1) audibly differs from dry (mix=0).

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

#include "engine/chorus/chorus_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

std::vector<float> run(ChorusEngine& e, int blocks, bool& finite) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    static double ph = 0.0;
    const double inc = 2.0 * M_PI * 220.0 / host::kSampleRate;
    std::vector<float> v;
    for (int b = 0; b < blocks; b++) {
        for (size_t i = 0; i < host::kBlock; i++) { float s = 0.3f * (float)std::sin(ph); ph += inc; il[i] = s; ir[i] = s; }
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) { if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false; v.push_back(ol[i]); }
    }
    return v;
}
} // namespace

int main() {
    host::TimeSource time;
    host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    ChorusEngine e; e.init(ctx);

    // 1. param round-trip (Size -> "delay" slider; ModSpeed via set_mod_speed -> "rate")
    e.set_param(ParamId::Size, DeckRef::A, 0.7f);
    check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.7f) < 1e-4f, "Size round-trips");
    e.set_param(ParamId::Mix, DeckRef::A, 0.25f);
    check(std::fabs(e.param(ParamId::Mix, DeckRef::A) - 0.25f) < 1e-4f, "Mix round-trips");
    e.set_mod_speed(DeckRef::A, 0.5f, false);
    check(std::fabs(e.param(ParamId::ModSpeed, DeckRef::A) - 0.5f) < 1e-4f, "MODFREQ -> ModSpeed round-trips");

    // 2. a fresh engine reports the kernel's own slider default (mix default 0.5 in chorus.dsp)
    ChorusEngine fresh; fresh.init(ctx);
    check(std::fabs(fresh.param(ParamId::Mix, DeckRef::A) - 0.5f) < 0.02f, "boot param = kernel slider default");

    // 3. finite + bounded, and wet (mix=1) differs from dry (mix=0)
    bool f1 = true, f2 = true;
    e.set_param(ParamId::Mix, DeckRef::A, 0.0f); const auto dry = run(e, 40, f1);
    e.set_param(ParamId::Mix, DeckRef::A, 1.0f); const auto wet = run(e, 40, f2);
    check(f1 && f2, "output is finite");
    float pk = 0.f; for (float x : wet) pk = std::fmax(pk, std::fabs(x));
    check(pk < 2.0f, "output is bounded");
    float sad = 0.f; const size_t n = std::min(dry.size(), wet.size());
    for (size_t i = 0; i < n; i++) sad += std::fabs(dry[i] - wet[i]);
    check(sad > 1.0f, "chorus wet (mix=1) differs from dry (mix=0) - the effect is wired");

    if (g_failures == 0) { std::printf("OK: all chorus checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
