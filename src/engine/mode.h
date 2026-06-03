// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <stdint.h>

namespace spotykach {

// Engine-facing selectors the platform reads for LED rendering. Contract-owned (item 5b) so the
// IEngine surface depends on them without pulling the granular DSP. `Mode` is granular-specific
// (the looper's three modes) but is exposed via the engine_leds query structs; the end state
// (render(DisplayModel)) retires that. `Route` is the channel topology the platform's mode switch
// drives. The granular `core/mode.h` redirects here so its relative includes still resolve.
enum class Mode: uint8_t {
    Reel = 0,
    Slice = 1,
    Drift = 2,
    None = 0xff
};

enum class Route: uint8_t {
    DoubleMono = 1,
    Stereo,
    GenerativeStereo
};

// The remaining engine-facing enums the platform reads via the engine_leds query structs (item 5b).
// Contract-owned so engine_leds.h / the platform LED code no longer include core/modulator.h,
// core/fx.h, core/deck.h. The granular classes alias these (`using Type = ModType;` etc.), so their
// internals are unchanged. Values/order mirror the originals exactly (codegen-identical).
enum class ModType: uint8_t {   // was Modulator::Type
    Follow,
    LFO
};

enum class GritMode: uint8_t {  // was Fx::GritMode
    Drive,
    Reduce
};

enum class DeckSource: uint8_t { // was Deck::Source (recording input select)
    external,
    internal
};

};
