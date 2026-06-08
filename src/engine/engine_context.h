// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/itimesource.h"
#include "engine/itransport.h"
#include "engine/istreamdeck.h"

namespace spotykach {

// An opaque block of SDRAM the platform hands the engine to sub-allocate however it likes (via
// engine/arena.h's Arena). The engine owns its own buffer layout; the platform/HAL provides only
// raw memory and knows nothing of any engine's buffer shape (item: EngineBuffers generalization).
struct EngineArena {
    uint8_t* base  = nullptr;
    size_t   bytes = 0;
};

// Everything the core needs from the platform, injected at init() time: sample rate, block size,
// the clock abstraction, the platform transport (read-only/subscribe view), and the SDRAM arena.
struct EngineContext {
    float              sample_rate;
    float              block_size;
    EngineArena        arena;
    const ITimeSource* time;
    ITransport*        transport;
    IStreamDeck*       stream = nullptr;  // SD streaming service (the tape engine uses it; null otherwise)
};

};
