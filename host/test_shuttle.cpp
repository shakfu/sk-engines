// Headless test for the shuttle engine (buffer-based bipolar/reverse varispeed tape; 2 decks x 2
// tracks). Exercised through the public IEngine surface plus the engine's host seams. Covers:
//   1. The bipolar speed map: noon -> 0 (stop), a deadzone about centre -> 0, +/-2x at the extremes,
//      monotonic, and the off-centre unity knob -> exactly +1x.
//   2. Record-to-RAM: input fed through process() is captured, and forward playback at unity replays it.
//   3. Reverse playback wraps to the buffer end; silence at noon while rolling; the read pointer loops.
//   4. Play->unity snap: on_play_pad sets +1x and arms exactly one take_param_reseed (the soft-takeover).
//   5. Rev-pad track swap (edrums mechanism): toggles the focused track, arms a reseed, and repoints the
//      knobs - set_param/param then address the focused track while the other track is untouched.
//   6. SD-into-RAM load through a fake IStreamDeck: selecting a slot drains the stream into the buffer.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/shuttle/shuttle_engine.h"
#include "engine/itimesource.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}
bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

constexpr float kPanCenter = 0.70710678f; // both decks centre-panned by default -> per-channel gain

// A clock we control, so the engine's 300 ms pad debounce can be stepped over deterministically.
struct FakeClock : ITimeSource {
    uint32_t ms = 1000;
    uint32_t now_ms() const override { return ms; }
    uint32_t now_us() const override { return ms * 1000u; }
};

// A memory-backed IStreamDeck: start_play serves `file` (a frame ramp) through play_consume, exactly
// like the real SD stream feeding the engine's load drain. Only the load path is modelled.
struct FakeStream : IStreamDeck {
    std::vector<float> file;
    bool playing = false;
    uint32_t pos = 0;
    uint32_t play_consume(DeckRef::Ref, uint8_t* dst, uint32_t n) override {
        const uint32_t frames = n / sizeof(float);
        const uint32_t avail  = static_cast<uint32_t>(file.size()) - pos;
        const uint32_t k      = frames < avail ? frames : avail;
        std::memcpy(dst, file.data() + pos, k * sizeof(float));
        pos += k;
        return k * sizeof(float);
    }
    uint32_t record_produce(DeckRef::Ref, const uint8_t*, uint32_t n) override { return n; }
    bool is_playing(DeckRef::Ref)   const override { return playing; }
    bool is_recording(DeckRef::Ref) const override { return false; }
    bool start_play(DeckRef::Ref, const char*)   override { playing = true; pos = 0; return true; }
    bool start_record(DeckRef::Ref, const char*) override { return false; }
    void stop(DeckRef::Ref)         override { playing = false; }
    void set_loop(DeckRef::Ref, bool) override {}
    uint32_t loop_frames(DeckRef::Ref) const override { return playing ? static_cast<uint32_t>(file.size()) : 0; }
    bool exists(const char*) const override { return true; }
};

// Smooth deterministic test signal (frac=0 at unity playback -> exact, no interpolation error).
float sig(size_t i) { return 0.5f * std::sin(static_cast<float>(i) * 0.05f); }

// Drive `frames` of a generated signal into BOTH deck inputs while a track records (so whichever
// deck's focused track is in record state captures content; the other decks are unaffected).
void feed_record(ShuttleEngine& e, size_t frames) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    size_t done = 0;
    while (done < frames) {
        for (size_t i = 0; i < host::kBlock; i++) { il[i] = ir[i] = sig(done + i); }
        e.process(in, out, host::kBlock);
        done += host::kBlock;
    }
}

// Record `frames` into a specific (deck, track): Rev-swap to focus it, then record around the feed.
void record_track(ShuttleEngine& e, FakeClock& clock, DeckRef::Ref d, int track, size_t frames) {
    while (e.active_track(d) != track) { clock.ms += 500; e.on_play_pad(d, true); }  // Rev to focus
    clock.ms += 500; e.on_record_pad(d, false);
    feed_record(e, frames);
    clock.ms += 500; e.on_record_pad(d, false);
}

// Process one silent block and return deck A's left-channel output.
void play_block(ShuttleEngine& e, float* outL) {
    float il[host::kBlock] = {0}, ir[host::kBlock] = {0}, ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    e.process(in, out, host::kBlock);
    std::memcpy(outL, ol, sizeof(ol));
}

} // namespace

int main() {
    std::printf("test_shuttle\n");
    host::HostArena arena; host::TimeSource htime;
    EngineContext base = host::make_context(arena, htime);
    FakeClock clock;

    // ---- 1. Bipolar speed map ---------------------------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        e.set_param(ParamId::Speed, DeckRef::A, 0.5f);
        check(approx(e.track_speed(DeckRef::A, 0), 0.f), "noon -> speed 0 (stopped)");
        e.set_param(ParamId::Speed, DeckRef::A, 0.51f);  // inside the deadzone
        check(approx(e.track_speed(DeckRef::A, 0), 0.f), "deadzone about noon -> still 0");
        e.set_param(ParamId::Speed, DeckRef::A, 1.0f);
        check(approx(e.track_speed(DeckRef::A, 0), 2.f), "full CW -> +2x");
        e.set_param(ParamId::Speed, DeckRef::A, 0.0f);
        check(approx(e.track_speed(DeckRef::A, 0), -2.f), "full CCW -> -2x (reverse)");

        // Unity must land at the off-centre knob value the Play snap targets, and map to exactly +1x.
        float prev = -3.f; bool mono = true;
        for (int k = 0; k <= 100; k++) {
            e.set_param(ParamId::Speed, DeckRef::A, k / 100.f);
            const float s = e.track_speed(DeckRef::A, 0);
            if (s < prev - 1e-4f) mono = false;
            prev = s;
        }
        check(mono, "speed is monotonic non-decreasing across the knob sweep");
    }

    // ---- 2. Record-to-RAM + forward playback at unity ---------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        const size_t frames = 4 * host::kBlock;
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);   // start record
        feed_record(e, frames);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);   // stop record
        check(e.buffer_frames(DeckRef::A, 0) == frames, "record captured the fed frame count");

        clock.ms += 500; e.on_play_pad(DeckRef::A, false);     // Play: snap to unity, roll
        check(approx(e.track_speed(DeckRef::A, 0), 1.f), "Play snaps the focused track to +1x");
        check(e.is_rolling(DeckRef::A, 0), "Play engages rolling");
        float outL[host::kBlock];
        play_block(e, outL);
        bool ok = true;
        for (size_t i = 0; i < host::kBlock; i++) if (!approx(outL[i], sig(i) * kPanCenter)) ok = false;
        check(ok, "forward playback at unity replays the recorded signal");
    }

    // ---- 3. Reverse wraps to the end; silence at noon; loop wrap ----------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        const size_t frames = 100 * host::kBlock;                // ~9600 frames
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        feed_record(e, frames);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);

        clock.ms += 500; e.on_play_pad(DeckRef::A, false);       // roll (read pointer at 0)
        e.set_param(ParamId::Speed, DeckRef::A, 0.25f);          // reverse (~-0.94x)
        check(e.track_speed(DeckRef::A, 0) < 0.f, "CCW gives negative (reverse) speed");
        float outL[host::kBlock];
        play_block(e, outL);
        check(e.read_position(DeckRef::A, 0) > frames / 2,
              "reverse from frame 0 wraps to the buffer end");

        e.set_param(ParamId::Speed, DeckRef::A, 0.5f);           // noon -> stop
        play_block(e, outL);
        bool silent = true;
        for (size_t i = 0; i < host::kBlock; i++) if (outL[i] != 0.f) silent = false;
        check(silent, "noon (speed 0) outputs silence while rolling");

        // Forward for well over a buffer length: the read pointer always stays within [0, len).
        e.set_param(ParamId::Speed, DeckRef::A, 1.0f);
        for (int b = 0; b < 250; b++) play_block(e, outL);       // 250*96 > 9600
        check(e.read_position(DeckRef::A, 0) < frames, "read pointer wraps (loops) within the buffer");
    }

    // ---- 4. Play->unity snap arms exactly one reseed ----------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        feed_record(e, host::kBlock);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);

        check(!e.take_param_reseed(DeckRef::A), "no reseed pending before any snap");
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);
        check(e.take_param_reseed(DeckRef::A), "Play snap arms a reseed");
        check(!e.take_param_reseed(DeckRef::A), "reseed fires exactly once");
        // param(Speed) reports the off-centre unity knob, so the pickup reseeds to it.
        check(e.param(ParamId::Speed, DeckRef::A) > 0.5f && e.param(ParamId::Speed, DeckRef::A) < 1.f,
              "param(Speed) reports the off-centre unity knob value");
    }

    // ---- 5. Rev-pad track swap repoints the knobs, tracks stay independent ------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        check(e.active_track(DeckRef::A) == 0, "deck A starts on track 0");

        // Record into track 0, then Rev-swap to track 1 and record a different length there.
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        feed_record(e, 2 * host::kBlock);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);

        clock.ms += 500; e.on_play_pad(DeckRef::A, true);        // Rev: swap focus to track 1
        check(e.active_track(DeckRef::A) == 1, "Rev pad swaps the focused track");
        check(e.take_param_reseed(DeckRef::A), "Rev swap arms a reseed");

        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        feed_record(e, 5 * host::kBlock);
        clock.ms += 500; e.on_record_pad(DeckRef::A, false);
        check(e.buffer_frames(DeckRef::A, 0) == 2 * host::kBlock, "track 0 length preserved across swap");
        check(e.buffer_frames(DeckRef::A, 1) == 5 * host::kBlock, "track 1 recorded independently");

        // PITCH now addresses the focused track (1); track 0's speed is untouched.
        e.set_param(ParamId::Speed, DeckRef::A, 1.0f);
        check(approx(e.track_speed(DeckRef::A, 1), 2.f), "PITCH drives the focused track (1)");
        check(approx(e.track_speed(DeckRef::A, 0), 0.f), "the unfocused track's speed is untouched");
        check(approx(e.param(ParamId::Speed, DeckRef::A), 1.0f), "param(Speed) reads the focused track");

        clock.ms += 500; e.on_play_pad(DeckRef::A, true);        // swap back to track 0
        check(e.active_track(DeckRef::A) == 0, "Rev pad swaps back");
        check(approx(e.param(ParamId::Speed, DeckRef::A), 0.5f), "param(Speed) follows focus back to track 0");
    }

    // ---- 6. SD-into-RAM load through a fake stream ------------------------------------------
    {
        FakeStream fs;
        fs.file.resize(500);
        for (size_t i = 0; i < fs.file.size(); i++) fs.file[i] = sig(i);
        EngineContext ctx = base; ctx.time = &clock; ctx.stream = &fs;
        ShuttleEngine e; e.init(ctx);

        e.set_param(ParamId::Aux, DeckRef::A, 0.20f);            // pick slot 1 -> request load
        for (int p = 0; p < 3; p++) e.prepare();                 // drain the stream into RAM
        check(e.buffer_frames(DeckRef::A, 0) == 500, "load drained the whole file into the buffer");

        clock.ms += 500; e.on_play_pad(DeckRef::A, false);       // play the loaded tape at unity
        float outL[host::kBlock];
        play_block(e, outL);
        bool ok = true;
        for (size_t i = 0; i < host::kBlock; i++) if (!approx(outL[i], sig(i) * kPanCenter)) ok = false;
        check(ok, "loaded audio plays back as the file content");
    }

    // ---- 7. Seq pad re-aligns all four tracks to their loop start --------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        // Record all four tracks; roll A0 and B0 for different durations so they diverge in phase.
        record_track(e, clock, DeckRef::A, 0, 4 * host::kBlock);
        record_track(e, clock, DeckRef::A, 1, 4 * host::kBlock);
        record_track(e, clock, DeckRef::B, 0, 4 * host::kBlock);
        record_track(e, clock, DeckRef::B, 1, 4 * host::kBlock);
        float outL[host::kBlock];

        while (e.active_track(DeckRef::A) != 0) { clock.ms += 500; e.on_play_pad(DeckRef::A, true); }
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);     // roll A0 (+1x)
        for (int b = 0; b < 5; b++) play_block(e, outL);
        while (e.active_track(DeckRef::B) != 0) { clock.ms += 500; e.on_play_pad(DeckRef::B, true); }
        clock.ms += 500; e.on_play_pad(DeckRef::B, false);     // roll B0 (+1x), 3 blocks later
        for (int b = 0; b < 2; b++) play_block(e, outL);
        check(e.read_position(DeckRef::A, 0) > 0.f && e.read_position(DeckRef::B, 0) > 0.f,
              "tracks advanced while rolling");
        check(e.read_position(DeckRef::A, 0) != e.read_position(DeckRef::B, 0),
              "free-running tracks drift out of phase");

        // Freeze the two rolling tracks at noon, then Seq-align all four and process one block.
        e.set_param(ParamId::Speed, DeckRef::A, 0.5f);          // A0 -> stop (it is focused)
        e.set_param(ParamId::Speed, DeckRef::B, 0.5f);          // B0 -> stop (it is focused)
        e.on_seq_trigger(DeckRef::A);
        play_block(e, outL);
        check(e.read_position(DeckRef::A, 0) == 0.f, "Seq align resets track A0 to the loop start");
        check(e.read_position(DeckRef::A, 1) == 0.f, "Seq align resets track A1");
        check(e.read_position(DeckRef::B, 0) == 0.f, "Seq align resets track B0");
        check(e.read_position(DeckRef::B, 1) == 0.f, "Seq align resets track B1 (all four atomic)");
    }

    // ---- 8. Realign declick: no click when re-aligning an audibly-rolling track ------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        record_track(e, clock, DeckRef::A, 0, 20 * host::kBlock);  // long buffer: no wrap in this test
        while (e.active_track(DeckRef::A) != 0) { clock.ms += 500; e.on_play_pad(DeckRef::A, true); }
        clock.ms += 500; e.on_play_pad(DeckRef::A, false);         // roll A0 at +1x

        float blk1[host::kBlock], blk2[host::kBlock];
        play_block(e, blk1);                                       // read 0->96; sig(96) is near a peak
        const float pos_before = e.read_position(DeckRef::A, 0);
        e.on_seq_trigger(DeckRef::A);                              // re-align while audibly rolling
        play_block(e, blk2);                                       // declick ramp masks the jump this block

        // The pointer jumped (was ~96, now well below it) - the realign happened, not a no-op.
        check(pos_before > 80.f && e.read_position(DeckRef::A, 0) < pos_before,
              "realign jumped an audible track's read pointer back");
        // Across the block boundary and the jump, no large sample-to-sample step (a raw jump from the
        // signal peak to frame 0 would step ~0.35; the declick keeps every step small).
        float max_step = 0.f, prev = blk1[host::kBlock - 1];
        for (size_t i = 0; i < host::kBlock; i++) {
            const float step = std::fabs(blk2[i] - prev);
            if (step > max_step) max_step = step;
            prev = blk2[i];
        }
        check(max_step < 0.1f, "declick keeps the realign click-free (no large output step)");
    }

    // ---- 9. POS / SIZE loop window ----------------------------------------------------------
    {
        EngineContext ctx = base; ctx.time = &clock;
        ShuttleEngine e; e.init(ctx);
        record_track(e, clock, DeckRef::A, 0, 10 * host::kBlock);   // 960-frame buffer
        const uint32_t L = e.buffer_frames(DeckRef::A, 0);
        check(e.loop_start(DeckRef::A, 0) == 0 && e.loop_len(DeckRef::A, 0) == L,
              "default window (POS=0, SIZE=full) is the whole buffer");

        e.set_param(ParamId::Size, DeckRef::A, 0.5f);              // half-length loop
        check(e.loop_len(DeckRef::A, 0) == L / 2, "SIZE sets the loop length");
        check(e.loop_start(DeckRef::A, 0) == 0, "POS=0 keeps the window at the buffer start");

        clock.ms += 500; e.on_play_pad(DeckRef::A, false);         // roll A0 at unity
        float outL[host::kBlock];
        for (int b = 0; b < 10; b++) play_block(e, outL);
        check(e.read_position(DeckRef::A, 0) < static_cast<float>(L / 2),
              "the read pointer stays inside the SIZE window");

        e.set_param(ParamId::Pos, DeckRef::A, 1.0f);              // slide the window to the buffer end
        const uint32_t S = e.loop_start(DeckRef::A, 0);
        check(S == L - L / 2, "POS slides the window start across the unused tail");
        for (int b = 0; b < 10; b++) play_block(e, outL);
        const float rp = e.read_position(DeckRef::A, 0);
        check(rp >= static_cast<float>(S) && rp < static_cast<float>(L),
              "the read pointer stays inside the shifted window");

        e.set_param(ParamId::Size, DeckRef::A, 0.f);
        check(e.loop_len(DeckRef::A, 0) == 64, "SIZE floors at kMinLoopFrames (no degenerate loop)");

        // Seq re-align snaps to the (shifted) window start, not frame 0.
        e.set_param(ParamId::Size, DeckRef::A, 0.5f);
        e.set_param(ParamId::Pos,  DeckRef::A, 1.0f);
        e.set_param(ParamId::Speed, DeckRef::A, 0.5f);            // stop (noon) so the snap is instant
        e.on_seq_trigger(DeckRef::A);
        play_block(e, outL);
        check(e.read_position(DeckRef::A, 0) == static_cast<float>(e.loop_start(DeckRef::A, 0)),
              "Seq realign snaps to the loop window start, not frame 0");
    }

    if (g_failures == 0) std::printf("  OK\n");
    else                 std::printf("  %d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
