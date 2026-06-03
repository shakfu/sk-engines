// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>

#include "itimesource.h"
#include "engine/deck_ref.h" // DeckRef::Count (contract; no granular deck.h dependency)

namespace spotykach {

// An opaque block of SDRAM the platform hands the engine to sub-allocate however it likes (via
// engine/arena.h's Arena). This is the generalization target that replaces the granular-shaped
// EngineBuffers below: Stage 1 (item: EngineBuffers generalization) adds it and routes the delay
// engine through it; Stage 2 will migrate granular's buffers onto it and retire EngineBuffers.
struct EngineArena {
    uint8_t* base  = nullptr;
    size_t   bytes = 0;
};

// Large buffers handed to the engine by the platform (firmware: the SDRAM pool; desktop: malloc).
// Type-opaque so this contract header carries no granular types (item 5b): `source`/`track` are
// raw pointers the granular engine casts back to Buffer::Frame*/Event* in Core::init. The SHAPE is
// still granular-specific (source/detect/delay/slices/track) - generalizing that to an opaque arena
// the engine sub-allocates is deferred to the first real second engine that needs different buffers.
struct EngineBuffers {
    void*   source[DeckRef::Count];     // per-deck loop buffer (granular: Buffer::Frame*)
    float*  detect[DeckRef::Count][2];  // per-deck detector buffer, 2 channels
    float*  delay[DeckRef::Count][2];   // per-deck flux delay buffer, 2 channels
    size_t* slices[DeckRef::Count];     // per-deck slice-point array
    void*   track[DeckRef::Count];      // per-deck sequencer event buffer (granular: Event*)
    size_t  source_frames;              // length of each source buffer in frames
};

// Everything the core needs from the platform, injected at init() time. Replaces the
// previously hardcoded sample rate / block size and the direct pool + daisy::System deps.
struct EngineContext {
    float              sample_rate;
    float              block_size;
    EngineBuffers      buffers;   // granular-shaped (Stage 2 will fold these into `arena`)
    EngineArena        arena;     // opaque SDRAM the engine sub-allocates (item: EngineBuffers generalization)
    const ITimeSource* time;
};

};
