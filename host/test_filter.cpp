// Headless test for the generated `filter` engine - the PARALLEL (DoubleMono) dual-deck mode of the
// Faust generator. Proves FaustEngine<Traits> with decks=2 keeps two INDEPENDENT control banks and
// processes deck A on the left channel, deck B on the right - the thing a single-deck engine (chorus)
// structurally cannot do:
//   1. per-deck params are independent (deck A's Size does not touch deck B's).
//   2. a fresh engine reports the kernel's own slider defaults, per deck.
//   3. with deck A's cutoff shut and deck B's open, a high tone is killed on the left, passed on the right.

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

#include "engine/filter/filter_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

// Drive an 8 kHz tone into both channels; return per-channel RMS over `blocks`.
void run(FilterEngine& e, int blocks, float& rms_l, float& rms_r, bool& finite) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    static double ph = 0.0;
    const double inc = 2.0 * M_PI * 8000.0 / host::kSampleRate;
    double sl = 0.0, sr = 0.0; long n = 0;
    for (int b = 0; b < blocks; b++) {
        for (size_t i = 0; i < host::kBlock; i++) { float s = 0.3f * (float)std::sin(ph); ph += inc; il[i] = s; ir[i] = s; }
        e.process(in, out, host::kBlock);
        const bool settled = b >= blocks / 2;   // skip the first half so the si.smoo cutoff ramp settles
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            if (settled) { sl += (double)ol[i] * ol[i]; sr += (double)orr[i] * orr[i]; n++; }
        }
    }
    rms_l = (float)std::sqrt(sl / (double)n);
    rms_r = (float)std::sqrt(sr / (double)n);
}
} // namespace

int main() {
    host::TimeSource time;
    host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    FilterEngine e; e.init(ctx);

    // 1. per-deck params are independent (Pitch -> "cutoff" on each deck's own instance).
    e.set_param(ParamId::Speed, DeckRef::A, 0.1f);
    e.set_param(ParamId::Speed, DeckRef::B, 0.9f);
    check(std::fabs(e.param(ParamId::Speed, DeckRef::A) - 0.1f) < 1e-4f, "deck A cutoff round-trips");
    check(std::fabs(e.param(ParamId::Speed, DeckRef::B) - 0.9f) < 1e-4f, "deck B cutoff round-trips");
    check(std::fabs(e.param(ParamId::Speed, DeckRef::A) - 0.1f) < 1e-4f,
          "deck B's cutoff did NOT clobber deck A's (independent state)");

    // 2. fresh engine reports the kernel slider defaults, per deck (cutoff default 1.0 in filter.dsp -> Pitch).
    FilterEngine fresh; fresh.init(ctx);
    check(std::fabs(fresh.param(ParamId::Speed, DeckRef::A) - 1.0f) < 0.02f, "deck A boot = kernel default");
    check(std::fabs(fresh.param(ParamId::Speed, DeckRef::B) - 1.0f) < 0.02f, "deck B boot = kernel default");

    // 3. DoubleMono channel independence: deck A cutoff shut (kills 8 kHz on L), deck B open (passes on R).
    e.set_param(ParamId::Mix,   DeckRef::A, 1.0f); e.set_param(ParamId::Mix,   DeckRef::B, 1.0f); // full wet both
    e.set_param(ParamId::Speed, DeckRef::A, 0.0f);  // deck A cutoff ~40 Hz -> kills the 8 kHz tone on the left
    e.set_param(ParamId::Speed, DeckRef::B, 1.0f);  // deck B cutoff ~20 kHz -> passes it on the right
    float rl = 0, rr = 0; bool finite = true;
    run(e, 120, rl, rr, finite);
    check(finite, "output is finite");
    check(rr < 2.0f, "output is bounded");
    check(rr > 0.05f, "deck B (open) passes the 8 kHz tone on the right");
    check(rl < 0.2f * rr, "deck A (cutoff shut) attenuates the left far below the right - channels are independent");

    if (g_failures == 0) { std::printf("OK: all filter checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
