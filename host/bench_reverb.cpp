// Host micro-benchmark for reverb process() cost, route-aware. One binary measures both topologies by
// switching ConfigId::Route at runtime:
//   STEREO     - one stereo voice (one Zita hall)
//   DOUBLEMONO - two mono voices (two Zita halls, one per deck)
// It prints us/block for each. The RATIO is the architecture-stable signal (~2x); the ABSOLUTE us is
// HOST time, NOT a device load % (a desktop core is ~15-25x faster than the Daisy's Cortex-M7 @ 480 MHz).
// For a real device load %, flash the METER firmware (make ENGINE=reverb METER=1, set DoubleMono) and
// read the serial log.
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "engine/reverb/reverb_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
struct LCG { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (int32_t(s) >> 8) * (1.f / 8388608.f); } };

double time_route(ReverbEngine& e, int route_wire) {
    e.set_config(ConfigId::Route, DeckRef::A, route_wire);
    LCG rng{ 12345u };
    // Pre-fill one input block and reuse it every iteration, so the timed loop is process() ONLY (the
    // device's audio ISR fills the buffer separately; including it here would dilute the ratio). The
    // reverb's internal delay lines still evolve, so it's real work; compute cost is input-independent.
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    for (size_t i = 0; i < host::kBlock; i++) { il[i] = ir[i] = 0.3f * rng.next(); }
    const float* in[2] = { il, ir }; float* o[2] = { ol, orr };
    for (int b = 0; b < 300; b++) e.process(in, o, host::kBlock); // warm up the delay lines
    const int N = 50000;
    volatile double checksum = 0; // keep the optimizer from eliding process()
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < N; b++) {
        e.process(in, o, host::kBlock);
        checksum = checksum + ol[0] + orr[0];
    }
    const auto t1 = std::chrono::steady_clock::now();
    (void)checksum;
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
}
}

int main() {
    host::TimeSource time; host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);
    ReverbEngine e; e.init(ctx);

    // Worst case: hall, fully wet, on both decks.
    e.set_config(ConfigId::Mode, DeckRef::A, 1); // 1 = hall
    e.set_config(ConfigId::Mode, DeckRef::B, 1);
    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        e.set_param(ParamId::Mix,    d, 1.0f);
        e.set_param(ParamId::Speed,  d, 0.7f); // decay
        e.set_param(ParamId::Env,    d, 0.5f);
        e.set_param(ParamId::Pos,    d, 0.5f);
        e.set_param(ParamId::Size,   d, 0.5f);
        e.set_param(ParamId::ModAmp, d, 0.5f);
    }

    const double period_us = 1e6 * host::kBlock / host::kSampleRate; // 2000 us @ 96 smp / 48 kHz
    const double us_stereo = time_route(e, 0); // Stereo     -> one stereo hall
    const double us_dual   = time_route(e, 1); // DoubleMono -> two mono halls

    std::printf("STEREO     (one stereo hall)   us/block=%.3f  host-load=%.2f%%\n", us_stereo, 100.0 * us_stereo / period_us);
    std::printf("DOUBLEMONO (two mono halls)    us/block=%.3f  host-load=%.2f%%\n", us_dual,   100.0 * us_dual   / period_us);
    std::printf("ratio DOUBLEMONO/STEREO = %.2fx   (block period=%.0f us; host us is NOT a device %%)\n",
                us_dual / us_stereo, period_us);
    return 0;
}
