#pragma once

#include <assert.h>
#include "dev/sdram.h"
#include "daisy_seed.h"
#include "nocopy.h"
#include "config.h"
#include "../core/track.h"
#include "../core/detector.h"
#include "../core/fx.h"
#include "../core/buffer.h"
#include "card.h"
#include "core/engine_context.h" // EngineArena

namespace spotykach {

static constexpr size_t kSampleRate            { 48000 };
static constexpr uint8_t kDecksCount           { 2 };
static constexpr uint8_t kBuffersPerDeckCount  { 1 };
static constexpr uint8_t kChannelsCount        { 2 };
// 16-bit storage halves bytes/frame, so the same SDRAM holds twice the seconds.
#if LOFI_INT16
static constexpr uint8_t kSourceMaxSeconds     { 84 };
#else
static constexpr uint8_t kSourceMaxSeconds     { 42 };
#endif

class SDRAMBuffer {
public:
    static SDRAMBuffer& pool() {
        static SDRAMBuffer instance;
        return instance;
    }

    Buffer::Frame* sourceBuffer();
    size_t sourceBufferSize();

    float* delayBuffer();

    float* detectorBuffer();

    Event* track_buffer_a() const;
    Event* track_buffer_b() const;

    size_t* slices_a() const;
    size_t* slices_b() const;

    uint8_t* card_buffer() const;

    // Opaque SDRAM arena for non-granular engines to sub-allocate (item: EngineBuffers
    // generalization, Stage 1). Reuses the currently-unused third source ("Undo") buffer; Stage 2
    // will make this the single backing store and retire the typed granular buffers above.
    EngineArena engineArena();

private:
    NOCOPY(SDRAMBuffer)

    SDRAMBuffer();
    ~SDRAMBuffer() = default;

    size_t _providedSourceBufCount      { 0 };
    size_t _providedDetectorBufCount    { 0 };
    size_t _providedDelayBufCount       { 0 };
};
};
