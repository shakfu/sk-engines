// Headless test for the graincloud engine (polyphonic grain cloud; docs/engine-ideas.md #11), built on
// the de-STL'd GrainflowLib port. Exercised through the public IEngine surface only:
//   1. Param round-trip (set_param/param) across the cloud knob map.
//   2. Empty buffer -> silence (no recording yet, grains have nothing to read).
//   3. Record the live input (Rev pad), then the cloud scatters grains over it: audible + finite.
//   4. Output stays bounded (the window/pan/gain scaling does not blow up).
//   5. Storage port (CapTapeStorage): data ptr / recorded-bytes / capacity / apply_loaded behave.
//   6. clear_buffer empties a deck; decks are independent.
//   7. MIDI note addresses a deck.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "engine/graincloud/graincloud_engine.h"
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

struct StubTransport : ITransport {
    float               tempo() const override { return 120.f; }
    ClockSource::Source source() const override { return ClockSource::internal; }
    bool                is_external_sync() const override { return false; }
    uint8_t             key_interval() const override { return 4; }
    bool                is_key_sub_quarter() const override { return false; }
    void set_on_tick(std::function<void(const TransportTick&)>) override {}
};

// Run `blocks` blocks feeding a sine into both channels (amp `in_amp`); return peak |out L|+|out R|.
float run_peak(GraincloudEngine& e, int blocks, float in_amp, bool& finite, double freq = 220.0) {
    static double phase = 0.0;
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    float peak = 0.f;
    const double inc = 2.0 * 3.14159265358979 * freq / host::kSampleRate;
    for (int b = 0; b < blocks; b++) {
        for (size_t i = 0; i < host::kBlock; i++) {
            const float s = static_cast<float>(std::sin(phase)) * in_amp; phase += inc;
            il[i] = s; ir[i] = s;
        }
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            const float a = std::fabs(ol[i]) + std::fabs(orr[i]);
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

    // --- 1. Param round-trip ---------------------------------------------------------------------
    {
        GraincloudEngine e; e.init(ctx);
        e.set_param(ParamId::Pos,    DeckRef::A, 0.42f);
        e.set_param(ParamId::Size,   DeckRef::A, 0.31f);
        e.set_param(ParamId::Speed,  DeckRef::B, 0.66f);
        e.set_param(ParamId::Env,    DeckRef::A, 0.7f);
        e.set_param(ParamId::ModAmp, DeckRef::B, 0.25f);
        e.set_param(ParamId::Mix,    DeckRef::A, 0.9f);
        e.set_mod_speed(DeckRef::A, 0.5f, false);
        // Grain direction is on the Reel/Slice/Drift mode switch (set_config), reporting a change only on
        // an actual change (so the platform re-applies correctly). Aux is gone (Alt+PITCH = loop slot).
        check(e.set_config(ConfigId::Mode, DeckRef::A, 1) == true,  "Mode->reverse reports a change");
        check(e.set_config(ConfigId::Mode, DeckRef::A, 1) == false, "same Mode reports no change");
        check(e.set_config(ConfigId::Mode, DeckRef::A, 0) == true,  "Mode->forward reports a change");
        check(approx(e.param(ParamId::Pos,    DeckRef::A), 0.42f), "Pos round-trips");
        check(approx(e.param(ParamId::Size,   DeckRef::A), 0.31f), "Size round-trips");
        check(approx(e.param(ParamId::Speed,  DeckRef::B), 0.66f), "Speed round-trips");
        check(approx(e.param(ParamId::Env,    DeckRef::A), 0.7f),  "Env round-trips");
        check(approx(e.param(ParamId::ModAmp, DeckRef::B), 0.25f), "ModAmp round-trips");
        check(approx(e.param(ParamId::Mix,    DeckRef::A), 0.9f),  "Mix round-trips");
        check(approx(e.param(ParamId::ModSpeed, DeckRef::A), 0.5f), "ModSpeed round-trips");
    }

    // --- 2. Empty buffer -> wet path is silent (grains have nothing to read) ----------------------
    {
        GraincloudEngine e; e.init(ctx);
        e.set_param(ParamId::Mix, DeckRef::A, 1.0f); // full wet
        e.set_param(ParamId::Mix, DeckRef::B, 1.0f);
        e.set_mod_speed(DeckRef::A, 0.8f, false);
        bool finite = true;
        const float peak = run_peak(e, 60, 0.f, finite); // zero input, nothing recorded
        check(finite, "empty-buffer output is finite");
        check(peak < 1e-3f, "empty buffer + full wet -> silence");
    }

    // --- 2b. Poisoned (non-zeroed) arena -> still silent at boot --------------------------------
    // The target SDRAM arena is NOT zero-initialized (unlike the host's). GrainflowLib skips writing
    // grain_output when the source buffer is empty, so the engine MUST zero its io scratch or the
    // mixdown sums uninitialized memory -> full-scale/NaN noise at boot. Reproduce by poisoning the
    // arena with garbage before init; output must be finite and silent (no recording made).
    {
        host::HostArena poison; host::TimeSource t2;
        EngineContext pctx = host::make_context(poison, t2);
        // Poison with a byte pattern that reads as a FINITE float (0x40404040 ~= 3.0), not 0xFF (which
        // would be NaN and get masked by the non-finite output guard). This isolates the io-zeroing fix:
        // finite garbage in grain_output would sum to audible noise unless the scratch is zeroed.
        std::memset(poison.mem.data(), 0x40, poison.mem.size());
        pctx.transport = &transport;
        GraincloudEngine e; e.init(pctx);
        e.set_param(ParamId::Mix, DeckRef::A, 1.0f);
        e.set_param(ParamId::Mix, DeckRef::B, 1.0f);
        e.set_mod_speed(DeckRef::A, 0.8f, false);
        bool finite = true;
        const float peak = run_peak(e, 60, 0.f, finite); // empty buffers, full wet
        check(finite, "poisoned-arena boot output is finite (io scratch zeroed)");
        check(peak < 1e-3f, "poisoned-arena empty buffers -> silence (no uninitialized-memory noise)");
    }

    // --- 3. Record the input, then the cloud sounds ----------------------------------------------
    {
        GraincloudEngine e; e.init(ctx);
        e.set_param(ParamId::Mix, DeckRef::A, 1.0f);     // full wet so we hear the cloud, not the dry
        e.set_param(ParamId::Mix, DeckRef::B, 1.0f);
        e.set_param(ParamId::Crossfade, DeckRef::A, 0.0f); // hard to deck A
        e.set_mod_speed(DeckRef::A, 0.8f, false);        // high density
        bool finite = true;
        // Record ~0.5 s of a tone into deck A.
        e.on_record_pad(DeckRef::A, /*reverse=*/false);
        run_peak(e, 260, 0.5f, finite);                  // ~0.52 s of input while recording
        e.on_record_pad(DeckRef::A, false);              // stop
        check(!e.audio_is_empty(DeckRef::A), "recording filled deck A buffer");
        // Now play: zero input, full wet -> output is the cloud reading the recording.
        const float cloud = run_peak(e, 200, 0.f, finite);
        check(finite, "cloud output is finite");
        check(cloud > 1e-3f, "cloud scatters grains over the recording (audible)");
        check(cloud < 8.f,   "cloud output is bounded");
    }

    // --- 4. Storage port ------------------------------------------------------------------------
    {
        GraincloudEngine e; e.init(ctx);
        check(e.audio_is_empty(DeckRef::A), "fresh deck is empty");
        check(e.audio_capacity_bytes(DeckRef::A) > 0, "capacity is reported");
        bool finite = true;
        e.on_record_pad(DeckRef::A, false);
        run_peak(e, 100, 0.4f, finite);
        e.on_record_pad(DeckRef::A, false);
        check(e.audio_data(DeckRef::A) != nullptr, "audio_data is non-null after record");
        check(e.audio_recorded_bytes(DeckRef::A) > 0, "recorded bytes > 0 after record");
        check(e.audio_recorded_bytes(DeckRef::A) <= e.audio_capacity_bytes(DeckRef::A), "recorded <= capacity");
        // Simulate an SD load of a shorter clip.
        e.audio_apply_loaded(DeckRef::A, 1000);
        check(e.audio_recorded_bytes(DeckRef::A) == 1000 * 2 * sizeof(float), "apply_loaded sets recorded length");
    }

    // --- 5. clear_buffer empties; decks independent ----------------------------------------------
    {
        GraincloudEngine e; e.init(ctx);
        bool finite = true;
        e.on_record_pad(DeckRef::A, false); run_peak(e, 80, 0.4f, finite); e.on_record_pad(DeckRef::A, false);
        check(!e.audio_is_empty(DeckRef::A), "deck A recorded");
        check(e.audio_is_empty(DeckRef::B),  "deck B still empty (decks independent)");
        e.clear_buffer(DeckRef::A);
        check(e.audio_is_empty(DeckRef::A),  "clear_buffer empties deck A");
    }

    // --- 5b. Duration (ENV) and density (MODFREQ) extremes stay finite/audible/bounded -----------
    // Guards the decoupled mapping: ENV -> grain-clock period (duration), MODFREQ -> onset rate ->
    // derived overlap (set_active_grains). Both ends must produce a healthy cloud over a recording.
    {
        GraincloudEngine e; e.init(ctx);
        e.set_param(ParamId::Mix, DeckRef::A, 1.0f);
        e.set_param(ParamId::Crossfade, DeckRef::A, 0.0f);
        bool finite = true;
        e.on_record_pad(DeckRef::A, false); run_peak(e, 260, 0.5f, finite); e.on_record_pad(DeckRef::A, false);

        auto sweep = [&](float env, float dens) {
            e.set_param(ParamId::Env, DeckRef::A, env);
            e.set_mod_speed(DeckRef::A, dens, false);
            bool fin = true; const float p = run_peak(e, 200, 0.f, fin);
            return std::make_pair(p, fin);
        };
        auto shortDense = sweep(0.0f, 1.0f);  // ~8 ms grains, max density (overlap saturates)
        auto longSparse = sweep(1.0f, 0.0f);  // ~1.5 s grains, min density (few onsets)
        check(shortDense.second && longSparse.second, "duration/density extremes are finite");
        check(shortDense.first > 1e-3f, "short+dense cloud is audible");
        check(longSparse.first > 1e-3f, "long+sparse cloud is audible");
        check(shortDense.first < 8.f && longSparse.first < 8.f, "duration/density extremes stay bounded");
    }

    // --- 6. MIDI note addresses a deck ----------------------------------------------------------
    {
        GraincloudEngine e; e.init(ctx);
        check(e.handle_midi_note(0, 60) == DeckRef::A, "MIDI ch0 -> deck A");
        check(e.handle_midi_note(1, 60) == DeckRef::B, "MIDI ch1 -> deck B");
    }

    if (g_failures == 0) { std::printf("OK: all graincloud checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
