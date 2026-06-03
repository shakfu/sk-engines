// Shared host-side scaffolding for the desktop harness and tests: a monotonic time source and a
// heap-allocated SDRAM arena + EngineContext, mirroring what the firmware's pool hands the engine.
// Since item "EngineBuffers generalization" the engine sub-allocates its own buffers from the arena,
// so the host no longer mirrors the granular buffer shape - it just provides a big enough block.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "engine/itimesource.h"
#include "engine/engine_context.h"

namespace host {

constexpr float  kSampleRate = 48000.f;
constexpr size_t kBlock      = 96;
// Big enough for the granular engine's buffers (~35 MB: 2x source + delay + detector + ...); matches
// the firmware arena budget. On the desktop this is an ordinary heap allocation.
constexpr size_t kArenaBytes = 48u * 1024u * 1024u;

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

// Owns the heap SDRAM-arena the engine sub-allocates from.
struct HostArena {
    std::vector<uint8_t> mem;
    void allocate() { mem.assign(kArenaBytes, 0); }
    spotykach::EngineArena view() { return { mem.data(), mem.size() }; }
};

// Allocates `arena` and returns a ready EngineContext pointing into it and `time`.
inline spotykach::EngineContext make_context(HostArena& arena, TimeSource& time) {
    arena.allocate();
    spotykach::EngineContext ctx;
    ctx.sample_rate = kSampleRate;
    ctx.block_size = static_cast<float>(kBlock);
    ctx.time = &time;
    ctx.arena = arena.view();
    return ctx;
}

} // namespace host
