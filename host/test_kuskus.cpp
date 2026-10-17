// Host test for the kuskus engine (Mutable Instruments *Beads*, one stereo instance over both decks).
//
// What it pins (docs/dev/kuskus.md, phase 0-2 exit criteria):
//
//   1. Dry/wet 0 passes the input through Beads' dry gain (0.75).
//   2. A latched SEED at the default density produces grains from a live input.
//   3. FREEZE holds the buffer: frozen grains still sound after the input goes silent, unfrozen do not.
//   4. SIZE at the top of travel switches to delay mode and still produces wet output.
//   5. Every media (Digital, Clean Tape, Dirty Tape, Vintage Digital) renders finite, audible output
//      after its Configure handshake, and the handshake does not leave the engine muted.
//   6. The gate-out route writes a clean gate on R and reports it on gate out A.
//   7. Param round-trip over the live ids.
//
// Not asserted: the timbre of any media. Finite, bounded and audible is the contract worth locking.

#include <cmath>
#include <cstdio>
#include <functional>

#include "engine/kuskus/kuskus_engine.h"
#include "engine/itransport.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

struct StubTransport : ITransport {
    std::function<void(const TransportTick&)> cb;
    float               tempo() const override { return 120.f; }
    ClockSource::Source source() const override { return ClockSource::internal; }
    bool                is_external_sync() const override { return false; }
    uint8_t             key_interval() const override { return 4; }
    bool                is_key_sub_quarter() const override { return false; }
    void set_on_tick(std::function<void(const TransportTick&)> f) override { cb = std::move(f); }
};

struct Stats {
    float rms = 0.f, peak = 0.f, in_rms = 0.f, corr = 0.f, last = 0.f;
    bool  finite = true, r_is_gate = true;
};

double g_phase = 0.0;

// Render `seconds` of audio, calling prepare() each block as the main loop does. `amp` is the
// amplitude of a 220 Hz sine on both inputs (0 = silence).
Stats run(KuskusEngine& e, float seconds, float amp) {
    const int blocks = static_cast<int>(seconds * host::kSampleRate / host::kBlock);
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    Stats s; double acc = 0, in_acc = 0, xy = 0; size_t n = 0;
    for (int b = 0; b < blocks; b++) {
        for (size_t i = 0; i < host::kBlock; i++) {
            il[i] = ir[i] = amp * static_cast<float>(std::sin(g_phase));
            g_phase += 2.0 * M_PI * 220.0 / host::kSampleRate;
        }
        e.process(in, out, host::kBlock);
        e.prepare();
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) s.finite = false;
            if (orr[i] != 0.f && orr[i] != 0.8f) s.r_is_gate = false;
            const float a = std::fabs(ol[i]);
            if (a > s.peak) s.peak = a;
            acc += static_cast<double>(ol[i]) * ol[i];
            in_acc += static_cast<double>(il[i]) * il[i];
            xy += static_cast<double>(ol[i]) * il[i];
            s.last = std::fabs(ol[i]) + std::fabs(orr[i]);
            n++;
        }
    }
    s.rms    = n ? static_cast<float>(std::sqrt(acc / n)) : 0.f;
    s.in_rms = n ? static_cast<float>(std::sqrt(in_acc / n)) : 0.f;
    s.corr   = (acc > 0 && in_acc > 0) ? static_cast<float>(xy / std::sqrt(acc * in_acc)) : 0.f;
    return s;
}

// A neutral panel: no randomness, no feedback, no reverb, unity gains.
void neutral(KuskusEngine& e) {
    for (auto id : { ParamId::Speed, ParamId::Pos, ParamId::Size, ParamId::Env })
        e.set_param(id, DeckRef::B, 0.5f);                 // attenuverters at noon
    e.set_param(ParamId::Speed, DeckRef::A, 0.5f);         // pitch 0
    e.set_param(ParamId::Pos,   DeckRef::A, 0.5f);
    e.set_param(ParamId::Size,  DeckRef::A, 0.6f);
    e.set_param(ParamId::Env,   DeckRef::A, 0.5f);
    e.set_param(ParamId::ModAmp, DeckRef::A, 0.f);         // feedback
    e.set_param(ParamId::ModAmp, DeckRef::B, 0.f);         // reverb
    e.set_param(ParamId::Mix,   DeckRef::B, 0.5f);         // output level unity
    e.set_mod_speed(DeckRef::A, 0.3f, false);              // density: regular grains
    e.set_mod_speed(DeckRef::B, 0.5f, false);              // input gain unity
}

void media(KuskusEngine& e, int deck_a, int deck_b) {
    e.set_config(ConfigId::Mode, DeckRef::A, deck_a);
    e.set_config(ConfigId::Mode, DeckRef::B, deck_b);
}

} // namespace

int main() {
    std::printf("=== kuskus (Beads granular processor) ===\n");

    host::TimeSource time; host::HostArena arena; StubTransport transport;
    EngineContext ctx = host::make_context(arena, time);
    ctx.transport = &transport;

    KuskusEngine e;
    e.init(ctx);
    neutral(e);

    {
        const Capabilities c = e.capabilities();
        check((c & CapOwnDisplay) != 0, "kuskus advertises CapOwnDisplay");
        check((c & CapDualDeck)   != 0, "kuskus advertises CapDualDeck");
        check((c & CapTransport)  != 0, "kuskus advertises CapTransport");
        check((c & CapTapeStorage) == 0, "kuskus does not claim the tape store");
    }

    // --- 1. dry path ------------------------------------------------------------------------------
    {
        e.set_param(ParamId::Mix, DeckRef::A, 0.f);
        run(e, 0.5f, 0.5f);
        const Stats s = run(e, 0.5f, 0.5f);
        const float gain = s.rms / s.in_rms;
        std::printf("dry: gain=%.3f corr=%.4f\n", gain, s.corr);
        check(s.finite, "dry path is finite");
        check(gain > 0.65f && gain < 0.85f, "dry/wet 0 passes the input at Beads' 0.75 dry gain");
        check(s.corr > 0.99f, "dry/wet 0 output is the input, not a processed copy");
    }

    // --- 2. grains ----------------------------------------------------------------------------
    {
        e.set_param(ParamId::Mix, DeckRef::A, 1.f);
        run(e, 1.f, 0.5f);
        const Stats s = run(e, 1.f, 0.5f);
        std::printf("grains: rms=%.4f peak=%.4f\n", s.rms, s.peak);
        check(s.finite, "granular output is finite");
        check(s.rms > 0.01f, "a latched SEED at density 0.3 produces grains");
        check(s.peak <= 1.01f, "the output limiter bounds the wet signal");
    }

    // --- 3. freeze ------------------------------------------------------------------------------
    {
        // Unfrozen: 3 s of sine then 3 s of silence. TIME 0.5 reads ~2 s back, i.e. the silence.
        run(e, 3.f, 0.5f);
        run(e, 3.f, 0.f);
        const Stats thawed = run(e, 0.5f, 0.f);

        // Frozen after 3 s of sine: the buffer stops recording, so grains keep reading the sine.
        run(e, 3.f, 0.5f);
        e.on_play_pad(DeckRef::B, false);
        run(e, 3.f, 0.f);
        const Stats frozen = run(e, 0.5f, 0.f);
        e.on_play_pad(DeckRef::B, false);
        std::printf("freeze: frozen rms=%.4f thawed rms=%.5f\n", frozen.rms, thawed.rms);
        check(frozen.rms > 0.01f, "FREEZE holds the buffer after the input goes silent");
        check(thawed.rms < frozen.rms * 0.1f, "without FREEZE the silence overwrites the buffer");
    }

    // --- 4. delay mode --------------------------------------------------------------------------
    {
        e.set_param(ParamId::Size, DeckRef::A, 1.f);
        run(e, 2.f, 0.5f);   // the granular->delay transition takes 1.5 s
        const Stats s = run(e, 1.f, 0.5f);
        std::printf("delay mode: rms=%.4f\n", s.rms);
        check(s.finite, "delay mode is finite");
        check(s.rms > 0.01f, "SIZE at the top of travel produces delay-mode wet output");
        e.set_param(ParamId::Size, DeckRef::A, 0.6f);
        run(e, 2.f, 0.5f);
    }

    // --- 5. every media -------------------------------------------------------------------------
    // Configure resets the delay line to a full-length, zeroed buffer, so read near the write head:
    // at TIME 0.5 a grain would sit 2-5 s back in the silence.
    e.set_param(ParamId::Pos, DeckRef::A, 0.02f);
    {
        struct M { int a, b; const char* name; };
        const M all[] = { { 1, 0, "clean tape" }, { 2, 0, "dirty tape" },
                          { 0, 2, "vintage digital" }, { 0, 0, "digital" } };
        for (const M& m : all) {
            media(e, m.a, m.b);
            const Stats first = run(e, host::kBlock / host::kSampleRate, 0.5f);
            run(e, 1.f, 0.5f);
            const Stats s = run(e, 1.f, 0.5f);
            std::printf("  %-16s rms=%.4f peak=%.4f\n", m.name, s.rms, s.peak);
            char msg[96];
            std::snprintf(msg, sizeof(msg), "%s: the switch fades out before Configure", m.name);
            check(first.last < 1e-6f, msg);
            std::snprintf(msg, sizeof(msg), "%s output is finite", m.name);
            check(s.finite, msg);
            std::snprintf(msg, sizeof(msg), "%s is audible after Configure (not left muted)", m.name);
            check(s.rms > 0.01f, msg);
        }
    }

    // --- mono route -----------------------------------------------------------------------------
    {
        e.set_config(ConfigId::Route, DeckRef::A, 1);
        check(e.route() == Route::DoubleMono, "Route 1 selects mono");
        run(e, 1.f, 0.5f);
        const Stats s = run(e, 1.f, 0.5f);
        check(s.finite && s.rms > 0.01f, "mono processing is audible and finite");
        e.set_config(ConfigId::Route, DeckRef::A, 0);
        run(e, 0.5f, 0.5f);
    }
    e.set_param(ParamId::Pos, DeckRef::A, 0.5f);

    // --- 6. gate-out route ----------------------------------------------------------------------
    {
        e.set_config(ConfigId::Route, DeckRef::A, 2);
        run(e, 0.2f, 0.5f);
        while (e.gate_out_triggered(DeckRef::A)) {}
        const Stats s = run(e, 1.f, 0.5f);
        int edges = 0;
        while (e.gate_out_triggered(DeckRef::A)) edges++;
        std::printf("gate out: edges=%d\n", edges);
        check(s.r_is_gate, "the gate-out route writes only 0 or +0.8 on R");
        check(edges > 0, "grain gates reach gate out A");
        check(!e.gate_out_triggered(DeckRef::B), "gate out B stays idle");
        e.set_config(ConfigId::Route, DeckRef::A, 0);
    }

    // --- transport clock-follow: Rev A, quarter ticks seed grains ----------------------------------
    {
        e.on_record_pad(DeckRef::A, false);            // Alt+Play A: unlatch
        e.set_mod_speed(DeckRef::A, 0.5f, false);      // density at noon: manual taps only
        run(e, 1.f, 0.5f);
        const Stats idle = run(e, 0.5f, 0.5f);

        e.on_play_pad(DeckRef::A, true);               // Rev A: clock-follow on
        TransportTick t; t.quarter = true;
        Stats ticked;
        for (int i = 0; i < 4; i++) { transport.cb(t); ticked = run(e, 0.1f, 0.5f); }
        std::printf("clock follow: idle rms=%.5f ticked rms=%.4f\n", idle.rms, ticked.rms);
        check(idle.rms < 0.005f, "unlatched SEED at the manual-tap density is silent");
        check(ticked.rms > idle.rms * 4.f, "transport quarters seed grains under clock-follow");
        e.on_play_pad(DeckRef::A, true);
        e.on_record_pad(DeckRef::A, false);            // relatch
        e.set_mod_speed(DeckRef::A, 0.3f, false);
    }

    // --- 7. param round-trip --------------------------------------------------------------------
    {
        const ParamId ids[] = { ParamId::Speed, ParamId::Pos, ParamId::Size,
                                ParamId::Env, ParamId::ModAmp, ParamId::Mix };
        for (auto d : { DeckRef::A, DeckRef::B })
            for (ParamId id : ids)
                for (float v : { 0.f, 0.25f, 0.75f, 1.f }) {
                    e.set_param(id, d, v);
                    check(std::fabs(e.param(id, d) - v) < 1e-4f, "param round-trips");
                }
        e.set_param(ParamId::Crossfade, DeckRef::A, 0.3f);
        check(std::fabs(e.param(ParamId::Mix, DeckRef::A) - 0.3f) < 1e-4f,
              "the crossfader writes dry/wet (shared with SOS A)");
        const Stats s = run(e, 1.f, 0.5f);
        check(s.finite, "still finite after sweeping every live param to its extremes");
    }

    // --- display ---------------------------------------------------------------------------------
    {
        DisplayModel m;
        e.render(m);
        check(m.mode_center.brightness > 0.f, "the L/C/R LEDs show the route (Stereo = center)");
    }

    if (g_failures == 0) { std::printf("\nAll kuskus tests passed.\n"); return 0; }
    std::printf("\nFAILED: %d check(s)\n", g_failures);
    return 1;
}
