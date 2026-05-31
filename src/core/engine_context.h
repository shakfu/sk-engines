// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstddef>

#include "itimesource.h"
#include "deck.h" // Deck::Count, Buffer::Frame (buffer.h), Event (track.h)

namespace spotykach {

// Large buffers handed to the core by the host (firmware: the SDRAM pool; desktop: malloc).
// Mirrors the per-deck buffers the core previously pulled from SDRAMBuffer::pool() directly.
struct EngineBuffers {
    Buffer::Frame* source[Deck::Count];     // per-deck loop buffer
    float*         detect[Deck::Count][2];  // per-deck detector buffer, 2 channels
    float*         delay[Deck::Count][2];   // per-deck flux delay buffer, 2 channels
    size_t*        slices[Deck::Count];     // per-deck slice-point array
    Event*         track[Deck::Count];      // per-deck sequencer event buffer
    size_t         source_frames;           // length of each source buffer in frames
};

// Everything the core needs from the platform, injected at init() time. Replaces the
// previously hardcoded sample rate / block size and the direct pool + daisy::System deps.
struct EngineContext {
    float              sample_rate;
    float              block_size;
    EngineBuffers      buffers;
    const ITimeSource* time;
};

};
