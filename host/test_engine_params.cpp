// Headless test for the GranularEngine parameter API (Phase 3a).
//
// The host can't drive the real pot/pad/LED path, but it CAN exercise the engine's
// parameter surface directly. This pins the mode-dependent dispatch (set_param) and the
// readback (param) before the live UI rewire switches onto them. Checks:
//   1. param() round-trips the last set_param value for every ParamId, across all modes.
//   2. set_param drives the real graph without producing NaN/Inf (finite-output smoke).
//   3. capabilities() reports the granular set.
//
// Drives the engine through the public IEngine surface only (transport_*/set_config/param/...);
// item 3a-4 removed the engine.core() escape hatch, so internal leaf state is no longer
// observable from a test - the cache round-trip + finite-output smoke cover the param API.

#include <cmath>
#include <cstdio>

#include "core/core.h"
#include "core/mode.h"
#include "engine/granular_engine.h"
#include "engine/passthrough/passthrough_engine.h"
#include "engine/display_model.h"
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

// Mode -> the set_config(ConfigId::Mode, ...) selector the engine maps (Slice=0, Reel=1, Drift=2).
int mode_to_config(Mode m) {
    switch (m) {
        case Mode::Drift: return 2;
        case Mode::Reel:  return 1;
        default:          return 0; // Slice
    }
}

} // namespace

int main() {
    host::TimeSource time;
    host::Buffers buffers;
    auto ctx = host::make_context(buffers, time);

    GranularEngine engine;
    engine.init(ctx);
    engine.transport_set_on_quarter([](bool) {});
    engine.transport_set_on_clock_out([]() {});
    engine.set_config(ConfigId::Route, Deck::A, 0); // Stereo
    engine.set_config(ConfigId::Route, Deck::A, 1); // DoubleMono

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
        for (auto ref : {Deck::A, Deck::B}) engine.set_config(ConfigId::Mode, ref, mode_to_config(mode));

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

        // (3) leaf spot-check removed: it read engine.core().deck().fx().flux_mix() to prove
        // set_param reached the DSP leaf. item 3a-4 removed core(), so leaf state isn't observable
        // from the public IEngine API; the cache round-trip (1) + finite-output smoke (2) cover it.

        // (4) CV + gate + mod-speed: the host CAN drive these directly (unlike pads/LEDs).
        // Exercise every handler across both decks; just assert no crash + cache where applicable.
        for (auto ref : {Deck::A, Deck::B}) {
            engine.cv_mix(ref, 0.2f);
            engine.cv_size_pos(ref, 0.3f);
            engine.cv_voct(ref, 0.0f);    // 0 semitones -> speed 1.0; caches _voct_speed for the gate
            engine.set_mod_speed(ref, 0.5f, true);
            check(approx(engine.param(ParamId::ModSpeed, ref), 0.5f), "set_mod_speed caches ModSpeed");
            engine.on_gate_trigger(ref);          // fire the deck at the cached V/Oct speed
            (void)engine.gate_out_triggered(ref); // bool query, must not crash

            // (5) storage audio port: byte view + apply-loaded round-trip.
            check(engine.audio_data(ref) != nullptr, "audio_data is non-null");
            check(engine.audio_capacity_bytes(ref) ==
                      host::kSourceFrames * sizeof(Buffer::Frame), "audio_capacity_bytes matches buffer");
            engine.audio_apply_loaded(ref, 100);
            check(!engine.audio_is_empty(ref), "audio not empty after apply_loaded(100)");
            check(engine.audio_recorded_bytes(ref) == 100 * sizeof(Buffer::Frame),
                  "audio_recorded_bytes reflects apply_loaded");
            engine.audio_apply_loaded(ref, 0);
            check(engine.audio_is_empty(ref), "audio empty after apply_loaded(0)");
        }
        engine.cv_crossfade(0.5f);

        // (2) finite-output smoke: feed a tone through a few blocks after the sweep + CV/gate.
        bool finite = true;
        for (int b = 0; b < 200; b++) {
            for (size_t i = 0; i < host::kBlock; i++) {
                float s = 0.4f * std::sin(2.f * 3.14159265f * 220.f * (b * host::kBlock + i) / host::kSampleRate);
                in_l[i] = in_r[i] = s;
            }
            engine.transport_tick(false);
            engine.process(in_ptrs, out_ptrs, host::kBlock);
            for (size_t i = 0; i < host::kBlock; i++) {
                if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
            }
        }
        check(finite, "process() output stays finite after a full param + CV/gate sweep");
    }

    // (6) Second-engine grounding: a non-granular PassthroughEngine through the same IEngine
    // seam + the DisplayModel render contract. Proves the contract fits an engine that opts out
    // of Recording/Tape/Sequencer and isn't a looper. (Host-only sketch; not in firmware.)
    {
        std::printf("passthrough engine\n");
        PassthroughEngine pe;
        pe.init(ctx);
        pe.prepare();

        // capabilities: own-display only - no Recording/Tape/Sequencer/Transport.
        check(pe.capabilities() == CapOwnDisplay, "passthrough capabilities() == CapOwnDisplay");

        // process: out == in, sample for sample.
        bool passthrough = true;
        for (size_t i = 0; i < host::kBlock; i++) {
            float s = 0.4f * std::sin(2.f * 3.14159265f * 330.f * i / host::kSampleRate);
            in_l[i] = in_r[i] = s;
            out_l[i] = out_r[i] = -1.f; // poison: render must overwrite
        }
        pe.process(in_ptrs, out_ptrs, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!approx(out_l[i], in_l[i]) || !approx(out_r[i], in_r[i])) passthrough = false;
        }
        check(passthrough, "passthrough process() copies in -> out");

        // Count lit pixels across both rings THROUGH the real LEDRing::apply(sink) blit path
        // (pixels are private; this also exercises the hardware-free blit the platform uses).
        auto count_lit = [](DisplayModel& m) {
            int lit = 0;
            for (int r = 0; r < 2; r++)
                m.ring[r].apply([&](uint8_t, uint32_t, float b){ if (b > 0.f) lit++; });
            return lit;
        };

        // render: a non-zero block peak must light some ring pixels + the play indicators.
        DisplayModel model;
        pe.render(model);
        check(count_lit(model) > 0, "render() lights ring pixels for a non-zero level");
        check(model.play[0].brightness > 0.f && model.play[1].brightness > 0.f,
              "render() lights both play indicators");

        // silence -> meter collapses; render() must clear() stale pixels (pre-dirtied here).
        for (size_t i = 0; i < host::kBlock; i++) { in_l[i] = in_r[i] = 0.f; }
        pe.process(in_ptrs, out_ptrs, host::kBlock);
        DisplayModel silent;
        silent.ring[0].set_hex_color(0xdeadbe);
        silent.ring[0].set_segment(0.f, 0.999f);   // pre-dirty: render() must clear() it
        silent.ring[0].set_updated();
        pe.render(silent);
        check(count_lit(silent) == 0, "render() shows no meter for silence");
    }

    if (g_failures == 0) {
        std::printf("OK: all engine param checks passed\n");
        return 0;
    }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
