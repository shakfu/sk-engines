#pragma once

#include <stdint.h>

namespace spotykach {

enum class Mode: uint8_t {
    Reel = 0,
    Slice = 1,
    Drift = 2,
    None = 0xff
};

// Channel topology (deck A/B routing). Co-located with Mode as the other engine-facing
// selector the platform reads for LEDs; moved here from core.h so the IEngine contract can
// depend on it without pulling the whole DSP graph.
enum class Route: uint8_t {
    DoubleMono = 1,
    Stereo,
    GenerativeStereo
};
};
