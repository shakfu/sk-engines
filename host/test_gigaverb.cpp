// Headless test for the gen~-authored `gigaverb` engine - the only engine whose DSP comes through the
// gen~ export path, so this is also the only off-target proof that path produces working DSP.
//   1. the ParamId -> gen-parameter map round-trips over each slot's own [min..max], and the ids the
//      export does not implement are inert.
//   2. live_params() is exactly the set index_of() maps (GenEngine derives one from the other).
//   3. deck B is ignored - one stereo effect on a dual-deck panel.
//   4. roomsize is SLEWED, not written: it retunes the tank's delay lengths, so a raw write crackles.
//   5. the reverb rings - a burst then silence leaves a decaying tail, and output stays finite/bounded.

#include <cmath>
#include <cstdio>

#include "engine/gigaverb/gigaverb_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

// One block of input at `amp` (220 Hz), returning the peak of the left output.
float pump(GigaverbEngine& e, float amp, bool& finite) {
    static double ph = 0.0;
    const double inc = 2.0 * M_PI * 220.0 / host::kSampleRate;
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    for (size_t i = 0; i < host::kBlock; i++) {
        const float s = amp * (float)std::sin(ph); ph += inc; il[i] = s; ir[i] = s;
    }
    e.process(in, out, host::kBlock);
    float pk = 0.f;
    for (size_t i = 0; i < host::kBlock; i++) {
        if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
        pk = std::fmax(pk, std::fmax(std::fabs(ol[i]), std::fabs(orr[i])));
    }
    return pk;
}

// The eight ids the wrap maps, in gen-parameter order.
const ParamId kMapped[] = { ParamId::Speed, ParamId::Env, ParamId::Mix, ParamId::EnvSize,
                            ParamId::Pos, ParamId::Size, ParamId::Feedback, ParamId::ModAmp };
} // namespace

int main() {
    host::TimeSource time;
    host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    GigaverbEngine e; e.init(ctx);

    // 1. normalized round-trip on every mapped slot. Size is excluded: it is slewed (see 4), so
    //    reading it back straight after a write is only expected to match on the first, priming write.
    for (ParamId id : kMapped) {
        if (id == ParamId::Size) continue;
        for (float v : { 0.f, 0.25f, 1.f }) {
            e.set_param(id, DeckRef::A, v);
            const float got = e.param(id, DeckRef::A);
            check(std::fabs(got - v) < 1e-3f, "mapped param round-trips over its own range");
        }
    }
    // ...and an unmapped id neither stores nor disturbs anything.
    e.set_param(ParamId::Aux, DeckRef::A, 0.8f);
    check(e.param(ParamId::Aux, DeckRef::A) == 0.f, "unmapped ParamId is inert");

#if SPK_TERMINAL
    // 2. the describe mask is derived from index_of(), so it must name exactly the mapped set.
    IEngine::ParamMask want = 0;
    for (ParamId id : kMapped) want |= (IEngine::ParamMask{1} << static_cast<uint32_t>(id));
    check(e.live_params() == want, "live_params() == the index_of() map");
    check(e.live_configs() == 0, "no config reaches the gen~ export");
#endif

    // 3. deck B is ignored (single stereo effect).
    e.set_param(ParamId::Pos, DeckRef::A, 0.2f);
    e.set_param(ParamId::Pos, DeckRef::B, 0.9f);
    check(std::fabs(e.param(ParamId::Pos, DeckRef::A) - 0.2f) < 1e-3f, "deck B write does not reach the export");

    // 4. roomsize slew. The first write primes and snaps; later writes glide, one step per process()
    //    block, and converge. A raw write would land in a single block.
    bool fin = true;
    e.set_param(ParamId::Size, DeckRef::A, 0.1f);           // prime
    check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.1f) < 1e-3f, "first Size write snaps (boot/pickup seed)");
    e.set_param(ParamId::Size, DeckRef::A, 0.9f);
    check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.1f) < 1e-3f, "Size write alone does not move roomsize");
    pump(e, 0.f, fin);
    const float after_one = e.param(ParamId::Size, DeckRef::A);
    check(after_one > 0.1f && after_one < 0.5f, "one block advances roomsize partway, not all the way");
    for (int b = 0; b < 400; b++) pump(e, 0.f, fin);
    check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.9f) < 5e-3f, "roomsize converges on its target");

    // 5. the reverb rings. dry=0 (Mix is the export's "dry" slot) so the tail is the wet path alone.
    e.set_param(ParamId::Mix, DeckRef::A, 0.f);
    e.set_param(ParamId::Pos, DeckRef::A, 1.f);             // revtime up
    for (int b = 0; b < 60; b++) pump(e, 0.3f, fin);        // excite
    const float wet = pump(e, 0.3f, fin);
    check(wet > 1e-3f, "wet path produces output");
    // Windowed peaks, not consecutive blocks: the tank's energy BUILDS for ~0.5 s as the diffusion
    // wash fills before it decays, so any two adjacent readings can go either way.
    float early = 0.f, late = 0.f;
    for (int b = 0; b < 2000; b++) {                        // input silent from here (~4 s)
        const float pk = pump(e, 0.f, fin);
        if (b < 200) early = std::fmax(early, pk);
        if (b >= 1800) late = std::fmax(late, pk);
    }
    check(early > 1e-3f, "silence after a burst still rings - the tank holds a tail");
    check(late < early * 0.1f, "the tail decays");
    check(fin, "output is finite throughout");

    float pk = 0.f;
    for (int b = 0; b < 40; b++) pk = std::fmax(pk, pump(e, 0.9f, fin));
    check(pk < 4.f, "output stays bounded under a hot input");

    if (g_failures == 0) { std::printf("OK: all gigaverb checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
