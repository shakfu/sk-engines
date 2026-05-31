// Desktop host harness for the Spotykach DSP core.
//
// Runs the real spotykach::Core off-target over a WAV file: allocates the large buffers
// on the heap, injects a monotonic ITimeSource, and pumps audio through Core::process in
// 96-frame blocks at the same 500 Hz block rate as the hardware (48000 / 96). This gives a
// fast iteration loop and a regression net before any UI/engine refactor.
//
// Usage:
//   host_spotykach [input.wav] [output.wav] [--record]
//   - no input:  a 1 s 220 Hz test tone is synthesised
//   - --record:  arm deck A, record the input, then play the loop back (best-effort)
//
// Default behaviour (no --record) is a pass-through, which deterministically exercises the
// whole DSP graph end to end.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/itimesource.h"
#include "core/mode.h"
#include "core/event.h"

#include "wav.h"

using namespace spotykach;

namespace {

constexpr size_t kBlock = 96;
constexpr float  kSampleRate = 48000.f;
constexpr size_t kHostSourceFrames = static_cast<size_t>(kSampleRate) * 15; // 15 s loop buffer

// Monotonic millisecond/microsecond clock for the core. Wall-clock based, which is fine for
// tap-tempo and reset timing in an offline run.
struct HostTimeSource : ITimeSource {
    using clock = std::chrono::steady_clock;
    clock::time_point start = clock::now();
    uint32_t now_ms() const override {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
    }
    uint32_t now_us() const override {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count());
    }
};

// Owns the heap allocations the core borrows through EngineBuffers.
struct HostBuffers {
    std::vector<Buffer::Frame> source[Deck::Count];
    std::vector<float>         detect[Deck::Count][2];
    std::vector<float>         delay[Deck::Count][2];
    std::vector<size_t>        slices[Deck::Count];
    std::vector<Event>         track[Deck::Count];

    void allocate() {
        for (int d = 0; d < Deck::Count; d++) {
            source[d].assign(kHostSourceFrames, Buffer::Frame{});
            for (int c = 0; c < 2; c++) {
                detect[d][c].assign(Detector::kWindow, 0.f);
                delay[d][c].assign(Fx::kEchoDelayBufferLength, 0.f);
            }
            slices[d].assign(kMaxSlicePointCount, 0);
            track[d].assign(Track::kLength, Event{});
        }
    }

    void fill(EngineBuffers& b) {
        b.source_frames = kHostSourceFrames;
        for (int d = 0; d < Deck::Count; d++) {
            b.source[d] = source[d].data();
            b.detect[d][0] = detect[d][0].data();
            b.detect[d][1] = detect[d][1].data();
            b.delay[d][0] = delay[d][0].data();
            b.delay[d][1] = delay[d][1].data();
            b.slices[d] = slices[d].data();
            b.track[d] = track[d].data();
        }
    }
};

void synth_tone(host::Audio& a, float seconds, float freq) {
    a.sample_rate = 48000;
    a.channels = 2;
    size_t n = static_cast<size_t>(seconds * a.sample_rate);
    a.l.resize(n);
    a.r.resize(n);
    for (size_t i = 0; i < n; i++) {
        float v = 0.5f * std::sin(2.f * 3.14159265f * freq * i / a.sample_rate);
        a.l[i] = v;
        a.r[i] = v;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string in_path, out_path = "build/host_out.wav";
    bool do_record = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--record") do_record = true;
        else if (in_path.empty()) in_path = arg;
        else out_path = arg;
    }

    host::Audio in;
    if (!in_path.empty()) {
        if (!host::read_wav(in_path, in)) {
            std::fprintf(stderr, "Failed to read 16-bit PCM WAV: %s\n", in_path.c_str());
            return 1;
        }
        std::printf("Read %s: %zu frames, %u Hz, %u ch\n", in_path.c_str(), in.frames(), in.sample_rate, in.channels);
    } else {
        synth_tone(in, 1.0f, 220.f);
        std::printf("No input given; synthesised 1 s 220 Hz tone\n");
    }

    // --- set up the core ---
    HostTimeSource time;
    HostBuffers hb;
    hb.allocate();

    EngineContext ctx;
    ctx.sample_rate = kSampleRate;
    ctx.block_size = static_cast<float>(kBlock);
    ctx.time = &time;
    hb.fill(ctx.buffers);

    auto core = std::make_unique<Core>();
    core->init(ctx);

    // The driver fires these per quarter / clock-out; the UI installs them on hardware, so
    // the host must provide no-ops or the empty std::functions throw bad_function_call.
    core->driver().set_on_quarter([](bool) {});
    core->driver().set_on_clock_out([]() {});

    core->set_route(Route::Stereo);
    core->set_route(Route::DoubleMono); // force a known route (set_route early-returns on no-op)
    core->infer_panner_mode();
    for (auto ref : {Deck::A, Deck::B}) {
        core->deck(ref).set_mode(Mode::Reel);
    }

    // Tail to let any loop play out after the input ends.
    size_t tail_frames = do_record ? static_cast<size_t>(kSampleRate) * 2 : 0;
    size_t total_frames = in.frames() + tail_frames;
    // round up to whole blocks
    total_frames = ((total_frames + kBlock - 1) / kBlock) * kBlock;

    host::Audio out;
    out.sample_rate = 48000;
    out.channels = 2;
    out.l.resize(total_frames);
    out.r.resize(total_frames);

    if (do_record) core->deck(Deck::A).toggle_recording(); // arm; detector records on sound

    float in_l[kBlock], in_r[kBlock];
    float out_l[kBlock], out_r[kBlock];
    const float* in_ptrs[2] = {in_l, in_r};
    float* out_ptrs[2] = {out_l, out_r};

    bool switched_to_play = false;
    for (size_t pos = 0; pos < total_frames; pos += kBlock) {
        // Once the input is consumed, optionally stop recording and start playback.
        if (do_record && !switched_to_play && pos >= in.frames()) {
            core->deck(Deck::A).disarm();
            core->deck(Deck::A).play();
            switched_to_play = true;
        }

        for (size_t i = 0; i < kBlock; i++) {
            size_t s = pos + i;
            in_l[i] = (s < in.frames()) ? in.l[s] : 0.f;
            in_r[i] = (s < in.frames()) ? in.r[s] : 0.f;
        }

        core->driver().tick(false);          // internal clock advances one 500 Hz block
        core->process(in_ptrs, out_ptrs, kBlock);

        for (size_t i = 0; i < kBlock; i++) {
            out.l[pos + i] = out_l[i];
            out.r[pos + i] = out_r[i];
        }
    }

    // Report peak so a silent/NaN run is obvious.
    float peak = 0.f;
    bool nan = false;
    for (size_t i = 0; i < total_frames; i++) {
        peak = std::max(peak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
        if (std::isnan(out.l[i]) || std::isnan(out.r[i])) nan = true;
    }
    std::printf("Processed %zu frames; output peak %.4f%s\n", total_frames, peak, nan ? "  [WARNING: NaN in output]" : "");

    if (!host::write_wav(out_path, out)) {
        std::fprintf(stderr, "Failed to write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", out_path.c_str());
    return nan ? 2 : 0;
}
