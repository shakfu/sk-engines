// Headless test for the karp engine (dual Karplus-Strong string voice; docs/engine-ideas.md #1).
//
// Exercised through the public IEngine surface only:
//   1. Param round-trip (set_param/param) and the reel/slice/drift switch (set_config Mode) reporting
//      a change exactly when the mode actually changes.
//   2. Slice: a pluck makes sound that then DECAYS (the loop rings down) - the defining KS behaviour.
//   3. Reel: with no input the internal-noise exciter still drives the resonator to a finite,
//      non-silent output (a sympathetic body you "play" by feeding it energy).
//   4. Drift: the free-running scatter scheduler fires grains and stays finite.
//   5. Every mode's process() output is finite (no NaN/Inf) under a zero input block.

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "engine/karp/karp_engine.h"
#include "engine/display_model.h"
#include "engine/itransport.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}
bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Capture the left output for `blocks` blocks after skipping `skip` (the attack), zero input.
std::vector<float> capture(KarpEngine& e, int skip, int blocks) {
    float il[host::kBlock] = {0}, ir[host::kBlock] = {0}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    for (int b = 0; b < skip; b++) e.process(in, out, host::kBlock);
    std::vector<float> v;
    for (int b = 0; b < blocks; b++) { e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) v.push_back(ol[i]); }
    return v;
}
// Autocorrelation fundamental estimate (Hz) over a plausible string range.
float est_pitch(const std::vector<float>& v) {
    int bl = 0; double best = 0;
    for (int lag = 30; lag < 1600; lag++) {
        double s = 0; for (size_t i = lag; i < v.size(); i++) s += v[i] * v[i - lag];
        if (s > best) { best = s; bl = lag; }
    }
    return bl ? host::kSampleRate / bl : 0.f;
}
// Zero-crossing rate (per 1000 samples) - a brightness proxy.
float zcr(const std::vector<float>& v) {
    int c = 0; for (size_t i = 1; i < v.size(); i++) if ((v[i-1] <= 0 && v[i] > 0) || (v[i-1] >= 0 && v[i] < 0)) c++;
    return v.empty() ? 0.f : 1000.f * c / v.size();
}

// Minimal transport: karp only reads tempo() (for the Slice arp rate); the tick subscription is unused.
struct StubTransport : ITransport {
    float               tempo() const override { return 120.f; }
    ClockSource::Source source() const override { return ClockSource::internal; }
    bool                is_external_sync() const override { return false; }
    uint8_t             key_interval() const override { return 4; }
    bool                is_key_sub_quarter() const override { return false; }
    void set_on_tick(std::function<void(const TransportTick&)>) override {}
};

// Run `blocks` audio blocks with a zero input and return the peak |output| on the left channel.
// `finite` is set false if any sample is non-finite.
float run_peak(KarpEngine& e, int blocks, bool& finite) {
    float in_l[host::kBlock] = {0}, in_r[host::kBlock] = {0};
    float out_l[host::kBlock], out_r[host::kBlock];
    const float* in_ptrs[2]  = { in_l, in_r };
    float*       out_ptrs[2] = { out_l, out_r };
    float peak = 0.f;
    for (int b = 0; b < blocks; b++) {
        e.process(in_ptrs, out_ptrs, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
            const float a = std::fabs(out_l[i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

} // namespace

int main() {
    host::TimeSource time;
    host::HostArena  arena;
    StubTransport    transport;

    EngineContext ctx = host::make_context(arena, time);
    ctx.transport = &transport;

    KarpEngine e;
    e.init(ctx);

    // --- 1. Param round-trip + mode switch reporting -------------------------------------------
    e.set_param(ParamId::Size, DeckRef::A, 0.42f);
    check(approx(e.param(ParamId::Size, DeckRef::A), 0.42f), "Size param round-trips");
    e.set_param(ParamId::Aux, DeckRef::B, 0.5f);
    check(approx(e.param(ParamId::Aux, DeckRef::B), 0.5f), "Aux param round-trips");

    // Default mode is Slice (value 0). Switching to Reel (1) / Drift (2) reports a change; re-asserting
    // the same mode reports no change (so the platform only re-applies SIZE on a real switch).
    check(e.set_config(ConfigId::Mode, DeckRef::A, 1) == true,  "Slice->Reel reports a change");
    check(e.set_config(ConfigId::Mode, DeckRef::A, 1) == false, "Reel->Reel reports no change");
    check(e.set_config(ConfigId::Mode, DeckRef::A, 0) == true,  "Reel->Slice reports a change");

    // --- 2. Slice: a pluck sounds, then decays -------------------------------------------------
    // Short decay + full wet + mid pitch so the ring-down is quick and loud.
    e.set_config(ConfigId::Mode, DeckRef::A, 0);     // Slice
    e.set_param(ParamId::Mix,  DeckRef::A, 1.0f);    // full wet
    e.set_param(ParamId::Size, DeckRef::A, 0.10f);   // short decay
    e.set_param(ParamId::Speed, DeckRef::A, 0.5f);   // mid pitch
    e.on_gate_trigger(DeckRef::A);                   // strike

    bool finite = true;
    const float early = run_peak(e, 30, finite);     // ~60 ms just after the pluck
    const float late  = run_peak(e, 120, finite);    // ~240 ms later
    check(finite, "Slice output is finite");
    check(early > 1e-3f, "Slice pluck produces audible output");
    check(late < early * 0.6f, "Slice voice decays (late peak well below early peak)");

    // --- 3. Reel: quiet when idle (no constant drone), sounds when triggered -------------------
    KarpEngine reel;
    reel.init(ctx);
    reel.set_config(ConfigId::Mode, DeckRef::A, 1);  // Reel
    reel.set_param(ParamId::Mix,  DeckRef::A, 1.0f); // full wet
    reel.set_param(ParamId::Size, DeckRef::A, 0.7f); // long-ish sustain
    reel.set_param(ParamId::Speed, DeckRef::A, 0.4f);
    finite = true;
    const float reel_idle = run_peak(reel, 30, finite);  // no trigger, no input -> silent
    reel.on_gate_trigger(DeckRef::A);                     // a trigger should excite the body
    const float reel_hit  = run_peak(reel, 30, finite);
    check(finite, "Reel output is finite");
    check(reel_hit > 1e-3f, "Reel trigger produces output");
    check(reel_hit > reel_idle * 4.f, "Reel is quiet until triggered (no constant drone)");

    // --- 4. Drift: scatter scheduler fires grains, stays finite --------------------------------
    KarpEngine drift;
    drift.init(ctx);
    drift.set_config(ConfigId::Mode, DeckRef::A, 2); // Drift
    drift.set_param(ParamId::Mix, DeckRef::A, 1.0f);
    drift.set_mod_speed(DeckRef::A, 0.8f, false);    // high density -> grains fire within the window
    finite = true;
    const float drift_peak = run_peak(drift, 300, finite);
    check(finite, "Drift output is finite");
    check(drift_peak > 1e-3f, "Drift scatter produces output");

    // --- 5. handle_midi_note returns a real deck and plucks ------------------------------------
    KarpEngine midi;
    midi.init(ctx);
    const DeckRef::Ref hit = midi.handle_midi_note(/*channel=*/0, /*note=*/60);
    check(hit == DeckRef::A, "MIDI note on channel 0 addresses deck A");
    finite = true;
    const float midi_peak = run_peak(midi, 30, finite);
    check(finite, "post-MIDI-note output is finite");
    check(midi_peak > 1e-3f, "MIDI note plucks an audible voice");

    // --- 6. PITCH tracks the Speed param (the on-device "pitch knob does nothing" regression) ------
    // A clean, pitched tone whose fundamental follows Speed - guards both the routing and a tone that
    // is actually pitched (not noise that masks the pitch).
    {
        KarpEngine p; p.init(ctx);
        p.set_param(ParamId::Mix, DeckRef::A, 1.0f);
        p.set_param(ParamId::Size, DeckRef::A, 0.7f);
        p.set_param(ParamId::Speed, DeckRef::A, 0.3f); p.on_gate_trigger(DeckRef::A);
        const float lo = est_pitch(capture(p, 8, 60));
        p.set_param(ParamId::Speed, DeckRef::A, 0.7f); p.on_gate_trigger(DeckRef::A);
        const float hi = est_pitch(capture(p, 8, 60));
        // Expected ~92 Hz and ~370 Hz; require the measured fundamental to roughly double-plus.
        check(lo > 60.f && lo < 130.f,  "low Speed gives a low fundamental (~92 Hz)");
        check(hi > 250.f && hi < 480.f, "high Speed gives a high fundamental (~370 Hz)");
        check(hi > lo * 2.5f,           "pitch clearly rises with Speed");
    }

    // --- 6b. V/Oct CV is ADDITIVE, not a pitch override (the cv_voct-clobbers-the-knob regression) --
    // read_cv() calls cv_voct on the engine every block; with nothing patched it passes 0 semitones.
    // The bug: cv_voct overwrote pitch_n from that value every block, pinning pitch to the unpatched
    // reading and making the PITCH knob dead. Guard both halves: the knob must still control pitch
    // while cv_voct(0) is hammered in, and a nonzero CV must TRANSPOSE the knob pitch, not replace it.
    {
        KarpEngine p; p.init(ctx);
        p.set_param(ParamId::Mix,  DeckRef::A, 1.0f);
        p.set_param(ParamId::Size, DeckRef::A, 0.7f);
        // Emulate read_cv() hammering cv_voct(0) (nothing patched) around each knob change + pluck.
        p.set_param(ParamId::Speed, DeckRef::A, 0.3f); p.cv_voct(DeckRef::A, 0.f); p.on_gate_trigger(DeckRef::A);
        const float lo = est_pitch(capture(p, 8, 60));
        p.set_param(ParamId::Speed, DeckRef::A, 0.7f); p.cv_voct(DeckRef::A, 0.f); p.on_gate_trigger(DeckRef::A);
        const float hi = est_pitch(capture(p, 8, 60));
        check(hi > lo * 2.5f, "PITCH knob still controls pitch while V/Oct CV reads 0 (no clobber)");

        // +12 semitones of V/Oct CV transposes the same knob setting up ~1 octave.
        KarpEngine q; q.init(ctx);
        q.set_param(ParamId::Mix,   DeckRef::A, 1.0f);
        q.set_param(ParamId::Size,  DeckRef::A, 0.7f);
        q.set_param(ParamId::Speed, DeckRef::A, 0.4f);
        q.cv_voct(DeckRef::A, 0.f);  q.on_gate_trigger(DeckRef::A);
        const float base = est_pitch(capture(q, 8, 60));
        q.cv_voct(DeckRef::A, 12.f); q.on_gate_trigger(DeckRef::A);
        const float up   = est_pitch(capture(q, 8, 60));
        check(up > base * 1.7f && up < base * 2.3f, "V/Oct CV of +12 semitones transposes up ~1 octave");
    }

    // --- 7. The brightness knob (ENV -> Rings patch.brightness) changes the timbre ----------------
    {
        auto bright_zcr = [&](float env) {
            KarpEngine m; m.init(ctx);
            m.set_param(ParamId::Mix,  DeckRef::A, 1.0f);
            m.set_param(ParamId::Size, DeckRef::A, 0.7f);
            m.set_param(ParamId::Speed, DeckRef::A, 0.4f);
            m.set_param(ParamId::Env,  DeckRef::A, env);   // -> Rings patch.brightness
            m.on_gate_trigger(DeckRef::A);
            return zcr(capture(m, 6, 40));
        };
        const float dark   = bright_zcr(0.05f);
        const float bright = bright_zcr(0.95f);
        check(bright > dark * 1.3f, "brightness knob raises spectral content (timbre is controllable)");
    }

    // --- 8. Default ENV plucks audibly; ENV fully CCW is silent-on-trigger ------------------------
    // ENV drives Rings brightness; at 0 the excitation is filtered to ~nothing, so a 0 default booted
    // the voice silent when the trig pad was pressed. The platform now engine-seeds ENV, so karp's
    // default is its own 0.5. Guard that default ENV is audible and that ENV=0 is dramatically quieter.
    // NOTE: the host arena is shared, so each KarpEngine's Impl overlaps the previous one - read a
    // deck's state before constructing the next engine. Capture `d`'s default + audio fully before `z`.
    {
        KarpEngine d; d.init(ctx);
        const float env_default = d.param(ParamId::Env, DeckRef::A);   // read before `z` reuses the arena
        d.set_param(ParamId::Mix, DeckRef::A, 1.0f);
        d.on_gate_trigger(DeckRef::A);
        bool fin1 = true; const float def_peak = run_peak(d, 30, fin1);

        KarpEngine z; z.init(ctx);
        z.set_param(ParamId::Mix, DeckRef::A, 1.0f);
        z.set_param(ParamId::Env, DeckRef::A, 0.0f);     // fully CCW
        z.on_gate_trigger(DeckRef::A);
        bool fin2 = true; const float ccw_peak = run_peak(z, 30, fin2);

        check(approx(env_default, 0.5f),    "ENV default is 0.5 (engaged, not silent)");
        check(def_peak > 0.02f,             "default ENV plucks an audible voice");
        check(ccw_peak < def_peak * 0.25f,  "ENV fully CCW is far quieter (brightness 0 -> ~silent)");
    }

    // --- 9. Alt+PITCH model change shows a multi-point model selector on the ring (then reverts) ----
    // The model selector previously had no visual. render() now draws all 5 model options around the
    // ring (selected bright, rest dim) for ~0.7 s after a change, replacing the lone pitch dot. With a
    // fresh engine and no trigger the only lit ring points are the pitch dot (1) or that selector (5),
    // so counting lit points cleanly tells them apart.
    {
        KarpEngine v; v.init(ctx);
        DisplayModel disp;
        auto count_lit = [&] {
            v.render(disp);
            int n = 0;
            disp.ring[DeckRef::A].apply([&](uint8_t, uint32_t, float b) { if (b > 0.03f) ++n; });
            return n;
        };
        const int baseline = count_lit();              // model_show == 0 -> just the pitch dot
        v.set_param(ParamId::Aux, DeckRef::A, 1.0f);   // string (2) -> string+reverb (4): a real change
        const int during = count_lit();                // selector window active
        for (int f = 0; f < 60; f++) v.render(disp);   // exhaust the ~45-frame show window
        const int after = count_lit();
        check(during > baseline, "Alt+PITCH model change lights the model selector (more than the lone pitch dot)");
        check(during >= 5,       "selector shows all 5 model options");
        check(after  <= baseline, "selector reverts to the pitch dot after the show window");
    }

    if (g_failures == 0) { std::printf("OK: all karp checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
