// Headless test for the softcut engine (dual-deck crossfaded OVERDUB looper, 2 decks x 2 voices on
// monome softcut). softcut's playback is not bit-exact (hermite resampling + subhead crossfades), so
// these assertions are on energy, flags, and the pure loop-window/rate math rather than sample equality.
// Covers:
//   1. Bipolar rate map: noon -> 0, deadzone -> 0, +/-2x extremes, monotonic, unity snap.
//   2. Loop window: POS/SIZE -> loop start/length seconds, SIZE floor, POS slide.
//   3. Overdub: Alt+Play auto-rolls + snaps unity + arms one reseed; fed input is captured and replays
//      on silent-input playback (sound-on-sound); disarm leaves the loop rolling.
//   4. Play pad: toggles rolling, snaps unity, arms one reseed; stopping disarms overdub.
//   5. Rev-pad focus swap repoints the knobs; voices stay independent.
//   6. Filters: flux-pad cutoff/resonance boot defaults + round-trip; MOD fade/slew round-trip.
//   7. SD load through a fake IStreamDeck: a loaded clip sets the extent and plays back non-silent.
//   8. Boot preload fills voices from their slots.
//   9. Seq-pad realign (sync) stays finite.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/softcut/softcut_engine.h"
#include "engine/itimesource.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}
bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

struct FakeClock : ITimeSource {
    uint32_t ms = 1000;
    uint32_t now_ms() const override { return ms; }
    uint32_t now_us() const override { return ms * 1000u; }
};

// Memory-backed IStreamDeck (same shape as the shuttle test): start_play serves a deck's `file` through
// play_consume; slots_a/_b model which slot files exist for the boot-preload probe.
struct FakeStream : IStreamDeck {
    std::vector<float> file[2];
    bool     playing[2] = { false, false };
    uint32_t pos[2]     = { 0, 0 };
    uint8_t  slots_a = 0, slots_b = 0;
    static int di(DeckRef::Ref d) { return (d == DeckRef::A) ? 0 : 1; }
    uint32_t play_consume(DeckRef::Ref d, uint8_t* dst, uint32_t n) override {
        const int i = di(d);
        const uint32_t frames = n / sizeof(float);
        const uint32_t avail  = static_cast<uint32_t>(file[i].size()) - pos[i];
        const uint32_t k      = frames < avail ? frames : avail;
        std::memcpy(dst, file[i].data() + pos[i], k * sizeof(float));
        pos[i] += k;
        return k * sizeof(float);
    }
    uint32_t record_produce(DeckRef::Ref, const uint8_t*, uint32_t n) override { return n; }
    bool is_playing(DeckRef::Ref d)   const override { return playing[di(d)]; }
    bool is_recording(DeckRef::Ref) const override { return false; }
    bool start_play(DeckRef::Ref d, const char*)   override { const int i = di(d); playing[i] = true; pos[i] = 0; return true; }
    bool start_record(DeckRef::Ref, const char*) override { return false; }
    void stop(DeckRef::Ref d)         override { playing[di(d)] = false; }
    void set_loop(DeckRef::Ref, bool) override {}
    uint32_t loop_frames(DeckRef::Ref d) const override { const int i = di(d); return playing[i] ? static_cast<uint32_t>(file[i].size()) : 0; }
    bool exists(const char* p) const override {            // path is "softcut/loop_<a|b>_<N>.wav", N = slot+1
        const char* a = std::strstr(p, "_a_");
        const char* b = std::strstr(p, "_b_");
        const char* m = a ? a : b;
        if (!m) return false;
        const int slot = m[3] - '1';
        if (slot < 0 || slot >= 8) return false;
        return ((a ? slots_a : slots_b) >> slot) & 1u;
    }
};

float sig(size_t i) { return 0.5f * std::sin(static_cast<float>(i) * 0.05f); }

// Run `frames` of the test signal into both deck inputs (drives whatever voice is overdubbing).
void feed(SoftcutEngine& e, size_t frames) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    for (size_t done = 0; done < frames; done += host::kBlock) {
        for (size_t i = 0; i < host::kBlock; i++) { il[i] = ir[i] = sig(done + i); }
        e.process(in, out, host::kBlock);
    }
}

// Collect deck A left-channel output over `frames` of SILENT input (pure playback), returning RMS and
// whether all samples are finite.
struct Stat { float rms; bool finite; };
Stat collect(SoftcutEngine& e, size_t frames) {
    float il[host::kBlock] = {0}, ir[host::kBlock] = {0}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    double sum = 0; size_t n = 0; bool fin = true;
    for (size_t done = 0; done < frames; done += host::kBlock) {
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) { sum += double(ol[i]) * ol[i]; n++; if (!std::isfinite(ol[i])) fin = false; }
    }
    return { static_cast<float>(std::sqrt(sum / (n ? n : 1))), fin };
}

} // namespace

int main() {
    std::printf("test_softcut\n");
    host::HostArena arena; host::TimeSource htime;
    EngineContext base = host::make_context(arena, htime);
    FakeClock clock;

    // ---- 1. Bipolar rate map ----------------------------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        e.set_param(ParamId::Speed, DeckRef::A, 0.5f);
        check(approx(e.track_rate(DeckRef::A, 0), 0.f), "noon -> rate 0 (stopped)");
        e.set_param(ParamId::Speed, DeckRef::A, 0.51f);
        check(approx(e.track_rate(DeckRef::A, 0), 0.f), "deadzone about noon -> still 0");
        e.set_param(ParamId::Speed, DeckRef::A, 1.0f);
        check(approx(e.track_rate(DeckRef::A, 0), 2.f), "full CW -> +2x");
        e.set_param(ParamId::Speed, DeckRef::A, 0.0f);
        check(approx(e.track_rate(DeckRef::A, 0), -2.f), "full CCW -> -2x (reverse)");
        float prev = -3.f; bool mono = true;
        for (int k = 0; k <= 100; k++) { e.set_param(ParamId::Speed, DeckRef::A, k / 100.f);
            const float s = e.track_rate(DeckRef::A, 0); if (s < prev - 1e-4f) mono = false; prev = s; }
        check(mono, "rate is monotonic non-decreasing across the knob sweep");
    }

    // ---- 2. Loop window math ----------------------------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        const float ext = static_cast<float>(1u << 19) / host::kSampleRate;   // full-buffer extent (s)
        check(approx(e.loop_len_sec(DeckRef::A, 0), ext * 0.5f, 1e-2f), "default SIZE=0.5 -> half-buffer loop");
        check(approx(e.loop_start_sec(DeckRef::A, 0), 0.f),      "default POS=0 -> start at 0");
        e.set_param(ParamId::Size, DeckRef::A, 1.0f);
        check(approx(e.loop_len_sec(DeckRef::A, 0), ext, 1e-2f), "SIZE=1 -> whole-buffer loop");
        e.set_param(ParamId::Size, DeckRef::A, 0.5f);
        check(approx(e.loop_len_sec(DeckRef::A, 0), ext * 0.5f, 1e-2f), "SIZE=0.5 -> half-length loop");
        e.set_param(ParamId::Pos, DeckRef::A, 1.0f);
        check(approx(e.loop_start_sec(DeckRef::A, 0), ext * 0.5f, 1e-2f), "POS=1 slides start across the tail");
        e.set_param(ParamId::Size, DeckRef::A, 0.f);
        check(e.loop_len_sec(DeckRef::A, 0) > 0.f && e.loop_len_sec(DeckRef::A, 0) < 0.1f,
              "SIZE=0 floors at a small non-degenerate loop");
    }

    // ---- 3. Overdub: arm auto-rolls + captures + replays ------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        e.set_param(ParamId::Size, DeckRef::A, 0.1f);            // ~0.55 s loop so a feed fills it
        check(!e.is_rolling(DeckRef::A, 0) && !e.is_overdubbing(DeckRef::A, 0), "boots stopped + not overdubbing");

        clock.ms += 500; e.on_record_pad(DeckRef::A, false);    // Alt+Play: arm overdub
        check(e.is_overdubbing(DeckRef::A, 0), "Alt+Play arms overdub");
        check(e.is_rolling(DeckRef::A, 0), "arming overdub auto-starts the loop rolling");
        check(approx(e.track_rate(DeckRef::A, 0), 1.f), "overdub auto-start snaps rate to unity");
        check(e.take_param_reseed(DeckRef::A), "overdub auto-start arms a reseed");

        feed(e, 30000);                                          // > 1 loop: lay sound across the window
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);    // disarm overdub
        check(!e.is_overdubbing(DeckRef::A, 0), "Alt+Play again disarms overdub");
        check(e.is_rolling(DeckRef::A, 0), "the loop keeps rolling after overdub disarm");

        const Stat s = collect(e, 6000);                        // silent input -> the loop should sound
        std::printf("overdub playback: rms=%.4f finite=%d\n", s.rms, (int)s.finite);
        check(s.finite, "overdub playback output is finite");
        check(s.rms > 0.03f, "the overdubbed loop replays (non-silent on silent input)");
    }

    // ---- 4. Fresh-take record-defines-loop; Play toggle + snap + reseed; stop disarms overdub ----
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);

        // A fresh take: first Alt+Play defines (records), second closes the loop at the recorded length.
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);   // start fresh take (defining)
        check(e.is_overdubbing(DeckRef::A, 0), "fresh take arms recording");
        check(e.is_rolling(DeckRef::A, 0), "fresh take rolls");
        feed(e, 5000);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);   // close the loop
        check(!e.is_overdubbing(DeckRef::A, 0), "closing a fresh take stops recording");
        check(e.is_rolling(DeckRef::A, 0), "closing a fresh take keeps it rolling");
        check(e.loop_len_sec(DeckRef::A, 0) > 0.05f, "the closed loop has the recorded length");
        (void)e.take_param_reseed(DeckRef::A);                 // consume the close's reseed

        // Play toggles rolling; re-engage snaps to unity and arms exactly one reseed.
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);     // stop
        check(!e.is_rolling(DeckRef::A, 0), "Play toggles rolling off");
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);     // roll + snap unity
        check(e.is_rolling(DeckRef::A, 0), "Play re-engages rolling");
        check(approx(e.track_rate(DeckRef::A, 0), 1.f), "Play snaps to +1x");
        check(e.take_param_reseed(DeckRef::A), "Play snap arms a reseed");
        check(!e.take_param_reseed(DeckRef::A), "reseed fires exactly once");

        // Now the voice has content: Alt+Play overdubs onto it; stopping disarms the overdub.
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);   // overdub onto content
        check(e.is_overdubbing(DeckRef::A, 0), "overdub arms onto existing content");
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);     // stop
        check(!e.is_rolling(DeckRef::A, 0), "Play toggles rolling off");
        check(!e.is_overdubbing(DeckRef::A, 0), "stopping the loop also disarms overdub");
    }

    // ---- 5. Rev-pad focus swap repoints knobs; voices independent ---------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        check(e.active_track(DeckRef::A) == 0, "deck A starts focused on voice 0");
        e.set_param(ParamId::Speed, DeckRef::A, 1.0f);          // voice 0 -> +2x
        clock.ms += 500; e.on_play_pad(DeckRef::A, true);       // Rev: swap focus to voice 1
        check(e.active_track(DeckRef::A) == 1, "Rev pad swaps the focused voice");
        check(e.take_param_reseed(DeckRef::A), "Rev swap arms a reseed");
        e.set_param(ParamId::Speed, DeckRef::A, 0.0f);          // voice 1 -> -2x
        check(approx(e.track_rate(DeckRef::A, 1), -2.f), "PITCH drives the focused voice (1)");
        check(approx(e.track_rate(DeckRef::A, 0),  2.f), "the unfocused voice's rate is untouched");
        clock.ms += 500; e.on_play_pad(DeckRef::A, true);       // swap back
        check(e.active_track(DeckRef::A) == 0, "Rev pad swaps back");
        check(approx(e.param(ParamId::Speed, DeckRef::A), 1.0f), "param(Speed) follows focus back to voice 0");
    }

    // ---- 6. Filter + mod param round-trip ---------------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        check(approx(e.param(ParamId::FluxIntensity, DeckRef::A), 1.f), "flux cutoff boots OPEN (1.0)");
        check(approx(e.param(ParamId::FluxMix,       DeckRef::A), 0.f), "flux resonance boots 0");
        e.set_param(ParamId::FluxIntensity, DeckRef::A, 0.33f);
        e.set_param(ParamId::FluxMix,       DeckRef::A, 0.77f);
        e.set_param(ParamId::ModAmp,        DeckRef::A, 0.6f);
        e.set_mod_speed(DeckRef::A, 0.4f, false);
        e.set_param(ParamId::Env,           DeckRef::A, 0.8f);
        check(approx(e.param(ParamId::FluxIntensity, DeckRef::A), 0.33f), "cutoff round-trips");
        check(approx(e.param(ParamId::FluxMix,       DeckRef::A), 0.77f), "resonance round-trips");
        check(approx(e.param(ParamId::ModAmp,        DeckRef::A), 0.6f),  "fade time (MOD_AMT) round-trips");
        check(approx(e.param(ParamId::ModSpeed,      DeckRef::A), 0.4f),  "rate slew (MODFREQ) round-trips");
        check(approx(e.param(ParamId::Env,           DeckRef::A), 0.8f),  "overdub feedback (ENV) round-trips");
    }

    // ---- 7. SD load through a fake stream ---------------------------------------------------
    {
        FakeStream fs;
        fs.file[0].resize(20000);
        for (size_t i = 0; i < fs.file[0].size(); i++) fs.file[0][i] = sig(i);
        EngineContext ctx = base; ctx.time = &clock; ctx.stream = &fs; SoftcutEngine e; e.init(ctx);

        e.set_param(ParamId::Aux, DeckRef::A, 0.20f);           // pick slot 1 -> request load
        for (int p = 0; p < 5; p++) e.prepare();                // drain the stream into RAM
        const float loaded = static_cast<float>(20000) / host::kSampleRate;
        e.set_param(ParamId::Size, DeckRef::A, 1.0f);           // full loop so loop_len reads back the extent
        check(approx(e.loop_len_sec(DeckRef::A, 0), loaded, 1e-2f),
              "after load a full-SIZE loop spans the loaded clip length");

        clock.ms += 500; e.on_play_pad(DeckRef::A, false);      // play the loaded loop
        const Stat s = collect(e, 6000);
        std::printf("sd-load playback: rms=%.4f finite=%d\n", s.rms, (int)s.finite);
        check(s.finite, "loaded-clip playback is finite");
        check(s.rms > 0.03f, "loaded audio plays back non-silent");
    }

    // ---- 8. Boot preload fills voices from their slots --------------------------------------
    {
        FakeStream fs;
        fs.slots_a = 0b11; fs.slots_b = 0b01;
        fs.file[0].resize(12000); for (size_t i = 0; i < 12000; i++) fs.file[0][i] = sig(i);
        fs.file[1].resize(8000);  for (size_t i = 0; i < 8000;  i++) fs.file[1][i] = sig(i + 40);
        EngineContext ctx = base; ctx.time = &clock; ctx.stream = &fs; SoftcutEngine e; e.init(ctx);
        for (int p = 0; p < 8; p++) e.prepare();
        // loop_len = default SIZE(0.5) x extent, so it is half the loaded clip (or half the full buffer
        // for the unloaded voice) - a clean proxy for which voice got content.
        const float la = 0.5f * 12000.f / host::kSampleRate, lb = 0.5f * 8000.f / host::kSampleRate;
        const float full = 0.5f * static_cast<float>(1u << 19) / host::kSampleRate;
        check(approx(e.loop_len_sec(DeckRef::A, 0), la, 1e-2f), "preload loads deck A voice 0 (slot 0)");
        check(approx(e.loop_len_sec(DeckRef::A, 1), la, 1e-2f), "preload loads deck A voice 1 (slot 1, serialized)");
        check(approx(e.loop_len_sec(DeckRef::B, 0), lb, 1e-2f), "preload loads deck B voice 0 (slot 0)");
        check(approx(e.loop_len_sec(DeckRef::B, 1), full, 1e-2f), "preload skips deck B voice 1 (slot absent -> full-buffer extent)");
    }

    // ---- 9. Seq-pad realign (sync) stays finite ---------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock; SoftcutEngine e; e.init(ctx);
        e.set_param(ParamId::Size, DeckRef::A, 0.1f);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false); feed(e, 20000);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        e.on_seq_trigger(DeckRef::A);                           // realign all voices (crossfaded cut)
        const Stat s = collect(e, 6000);
        check(s.finite, "output stays finite after a Seq realign");
    }

    if (g_failures == 0) std::printf("  OK\n");
    else                 std::printf("  %d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
