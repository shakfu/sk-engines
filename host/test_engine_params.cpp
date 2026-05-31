// Headless test for the GranularEngine parameter API (Phase 3a).
//
// The host can't drive the real pot/pad/LED path, but it CAN exercise the engine's
// parameter surface directly. This pins the mode-dependent dispatch (set_param) and the
// readback (param) before the live UI rewire switches onto them. Checks:
//   1. param() round-trips the last set_param value for every ParamId, across all modes.
//   2. set_param drives the real graph without producing NaN/Inf (finite-output smoke).
//   3. a getter-backed leaf effect (FluxMix -> fx().flux_mix()) actually lands.
//   4. capabilities() reports the granular set.

#include <cmath>
#include <cstdio>

#include "core/core.h"
#include "core/mode.h"
#include "engine/granular_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::printf("  FAIL: %s\n", msg);
        g_failures++;
    }
}

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Every per-deck ParamId; globals are tested separately with Deck::A.
constexpr ParamId kPerDeck[] = {
    ParamId::Pos, ParamId::FluxFb, ParamId::Env, ParamId::EnvSize, ParamId::Size,
    ParamId::Win, ParamId::PolySlice, ParamId::Speed, ParamId::FluxIntensity,
    ParamId::GritIntensity, ParamId::FluxMix, ParamId::GritMix, ParamId::Feedback,
    ParamId::Mix, ParamId::ModSpeed, ParamId::ModAmp,
};
constexpr ParamId kGlobal[] = {
    ParamId::Tempo, ParamId::ClickMix, ParamId::PanSpeed, ParamId::PanRange,
    ParamId::KeyInterval, ParamId::Crossfade,
};

const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Reel: return "Reel";
        case Mode::Slice: return "Slice";
        case Mode::Drift: return "Drift";
        default: return "None";
    }
}

} // namespace

int main() {
    host::TimeSource time;
    host::Buffers buffers;
    auto ctx = host::make_context(buffers, time);

    GranularEngine engine;
    engine.init(ctx);
    engine.core().driver().set_on_quarter([](bool) {});
    engine.core().driver().set_on_clock_out([]() {});
    engine.core().set_route(Route::Stereo);
    engine.core().set_route(Route::DoubleMono);

    // (4) capabilities
    auto caps = engine.capabilities();
    Capabilities expected = CapRecording | CapTapeStorage | CapStepSequencer
                          | CapLaunchQuant | CapTransport | CapDualDeck;
    check(caps == expected, "capabilities() reports the granular set");

    float in_l[host::kBlock], in_r[host::kBlock], out_l[host::kBlock], out_r[host::kBlock];
    const float* in_ptrs[2] = {in_l, in_r};
    float* out_ptrs[2] = {out_l, out_r};

    for (Mode mode : {Mode::Reel, Mode::Slice, Mode::Drift}) {
        std::printf("mode %s\n", mode_name(mode));
        for (auto ref : {Deck::A, Deck::B}) engine.core().deck(ref).set_mode(mode);

        // (1) per-deck param() round-trips set_param across a value sweep.
        int k = 0;
        for (ParamId id : kPerDeck) {
            for (auto ref : {Deck::A, Deck::B}) {
                float v = 0.1f + 0.05f * static_cast<float>((k++) % 17); // varied [0.1, 0.9]
                engine.set_param(id, ref, v);
                check(approx(engine.param(id, ref), v), "per-deck param() round-trips set_param");
            }
        }
        // globals
        for (ParamId id : kGlobal) {
            float v = 0.42f;
            engine.set_param(id, Deck::A, v);
            check(approx(engine.param(id, Deck::A), v), "global param() round-trips set_param");
        }

        // (3) leaf spot-check: FluxMix clamps-and-stores, so the getter must match.
        engine.set_param(ParamId::FluxMix, Deck::A, 0.3f);
        check(approx(engine.core().deck(Deck::A).fx().flux_mix(), 0.3f), "FluxMix lands on fx().flux_mix()");

        // (2) finite-output smoke: feed a tone through a few blocks after the param sweep.
        bool finite = true;
        for (int b = 0; b < 200; b++) {
            for (size_t i = 0; i < host::kBlock; i++) {
                float s = 0.4f * std::sin(2.f * 3.14159265f * 220.f * (b * host::kBlock + i) / host::kSampleRate);
                in_l[i] = in_r[i] = s;
            }
            engine.core().driver().tick(false);
            engine.process(in_ptrs, out_ptrs, host::kBlock);
            for (size_t i = 0; i < host::kBlock; i++) {
                if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
            }
        }
        check(finite, "process() output stays finite after a full param sweep");
    }

    if (g_failures == 0) {
        std::printf("OK: all engine param checks passed\n");
        return 0;
    }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
