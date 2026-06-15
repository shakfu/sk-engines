// Headless test for the generated `voice` engine - the SERIES (chain) dual-deck mode of the Faust
// generator: a drone oscillator (stage A, deck A) into a resonant filter (stage B, deck B), on
// FaustChainEngine<Traits>. Proves:
//   1. the instrument stage generates with NO audio input (the 0-input chain path).
//   2. the two stages keep independent per-deck params (deck A's Size != deck B's Size).
//   3. a fresh engine reports each stage's own slider defaults.
//   4. deck A's level (stage A) controls amplitude; deck B's cutoff (stage B) shapes the tone -
//      i.e. the signal really flows osc -> filter.

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

#include "engine/voice/voice_engine.h"
#include "host_setup.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

// The chain generates from its knobs; audio input is ignored (stage A is a 0-input instrument). Feed
// silence and return the output RMS.
float run(VoiceEngine& e, int blocks, bool& finite) {
    float il[host::kBlock] = {}, ir[host::kBlock] = {}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    double s = 0.0; long n = 0;
    for (int b = 0; b < blocks; b++) {
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            s += (double)ol[i] * ol[i]; n++;
        }
    }
    return (float)std::sqrt(s / (double)n);
}
} // namespace

int main() {
    host::TimeSource time;
    host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    VoiceEngine e; e.init(ctx);
    // Sensible drone: deck A osc audible (Pitch=freq, Mix=level), deck B filter open + full wet.
    // Deck B map: Pitch=cutoff, Position=reso, Size=drive, Mix=mix.
    e.set_param(ParamId::Speed, DeckRef::A, 0.35f);  // osc freq (deck A Pitch)
    e.set_param(ParamId::Mix,   DeckRef::A, 0.8f);   // osc level (deck A Mix)
    e.set_param(ParamId::Mix,   DeckRef::B, 1.0f);   // filter full wet (deck B Mix)
    e.set_param(ParamId::Speed, DeckRef::B, 0.9f);   // filter cutoff open (deck B Pitch -> cutoff)

    // 1. the instrument generates with no input.
    bool finite = true;
    float rms_loud = run(e, 60, finite);
    check(finite, "output is finite");
    check(rms_loud > 0.02f, "instrument stage generates sound with no audio input (0-in chain path)");
    check(rms_loud < 2.0f, "output is bounded");

    // 2. per-deck params are independent though both use ParamId::Size (deck A=shape, deck B=drive).
    e.set_param(ParamId::Size, DeckRef::A, 0.2f);
    e.set_param(ParamId::Size, DeckRef::B, 0.7f);
    check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.2f) < 1e-4f, "deck A Size round-trips");
    check(std::fabs(e.param(ParamId::Size, DeckRef::B) - 0.7f) < 1e-4f,
          "deck B Size independent of deck A (separate stage state)");

    // 3. fresh engine: each stage's own slider defaults (osc shape=0.0 on deck A Size; filter cutoff=0.55
    //    on deck B Pitch).
    VoiceEngine fresh; fresh.init(ctx);
    check(std::fabs(fresh.param(ParamId::Size,  DeckRef::A) - 0.0f)  < 0.02f, "deck A boot = osc shape default");
    check(std::fabs(fresh.param(ParamId::Speed, DeckRef::B) - 0.55f) < 0.02f, "deck B boot = filter cutoff default");

    // 4a. deck A level (stage A) drives amplitude: level 0 -> near silence.
    VoiceEngine eq; eq.init(ctx);
    eq.set_param(ParamId::Speed, DeckRef::A, 0.35f);
    eq.set_param(ParamId::Mix,   DeckRef::B, 1.0f);
    eq.set_param(ParamId::Speed, DeckRef::B, 0.9f);   // cutoff open (deck B Pitch)
    eq.set_param(ParamId::Mix,   DeckRef::A, 0.0f);   // level 0
    bool f2 = true; float rms_silent = run(eq, 60, f2);
    check(rms_silent < 0.2f * rms_loud, "deck A level=0 silences the chain (stage A feeds stage B)");

    // 4b. deck B cutoff (deck B Pitch) shapes the tone: a low cutoff passes less of the bright osc than open.
    VoiceEngine lo; lo.init(ctx);
    lo.set_param(ParamId::Speed, DeckRef::A, 0.35f); lo.set_param(ParamId::Mix, DeckRef::A, 0.8f);
    lo.set_param(ParamId::Mix,   DeckRef::B, 1.0f);  lo.set_param(ParamId::Speed, DeckRef::B, 0.1f); // cutoff low
    bool f3 = true; float rms_lo = run(lo, 60, f3);
    check(rms_lo < rms_loud, "deck B low cutoff attenuates vs open cutoff - the filter stage acts on the osc");

    if (g_failures == 0) { std::printf("OK: all voice checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
