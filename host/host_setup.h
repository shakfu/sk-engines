// Shared host-side scaffolding for the desktop harness and tests: a monotonic time source
// and heap-allocated EngineBuffers + EngineContext, mirroring what the SDRAM pool hands the
// firmware. Keeps main_host.cpp and the tests from duplicating the boilerplate.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "core/itimesource.h"
#include "core/engine_context.h"
#include "core/buffer.h"   // Buffer::Frame
#include "core/event.h"    // Event
#include "core/detector.h" // Detector::kWindow
#include "core/fx.h"       // Fx::kEchoDelayBufferLength
#include "core/track.h"    // Track::kLength
#include "config.h"        // kMaxSlicePointCount

namespace host {

constexpr float  kSampleRate = 48000.f;
constexpr size_t kBlock      = 96;
constexpr size_t kSourceFrames = static_cast<size_t>(kSampleRate) * 15; // 15 s loop buffer

struct TimeSource : spotykach::ITimeSource {
    using clock = std::chrono::steady_clock;
    clock::time_point start = clock::now();
    uint32_t now_ms() const override {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
    }
    uint32_t now_us() const override {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count());
    }
};

// Owns the heap allocations the core borrows through EngineBuffers.
struct Buffers {
    std::vector<spotykach::Buffer::Frame> source[spotykach::Deck::Count];
    std::vector<float>                    detect[spotykach::Deck::Count][2];
    std::vector<float>                    delay[spotykach::Deck::Count][2];
    std::vector<size_t>                   slices[spotykach::Deck::Count];
    std::vector<spotykach::Event>         track[spotykach::Deck::Count];

    void allocate() {
        using namespace spotykach;
        for (int d = 0; d < Deck::Count; d++) {
            source[d].assign(kSourceFrames, Buffer::Frame{});
            for (int c = 0; c < 2; c++) {
                detect[d][c].assign(Detector::kWindow, 0.f);
                delay[d][c].assign(Fx::kEchoDelayBufferLength, 0.f);
            }
            slices[d].assign(kMaxSlicePointCount, 0);
            track[d].assign(Track::kLength, Event{});
        }
    }

    void fill(spotykach::EngineBuffers& b) {
        using namespace spotykach;
        b.source_frames = kSourceFrames;
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

// Allocates `buffers` and returns a ready EngineContext pointing into them and `time`.
inline spotykach::EngineContext make_context(Buffers& buffers, TimeSource& time) {
    buffers.allocate();
    spotykach::EngineContext ctx;
    ctx.sample_rate = kSampleRate;
    ctx.block_size = static_cast<float>(kBlock);
    ctx.time = &time;
    buffers.fill(ctx.buffers);
    return ctx;
}

} // namespace host
