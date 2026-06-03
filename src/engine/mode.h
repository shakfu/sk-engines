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

// The transport clock source (was Driver::Source), exposed via transport_source() + TransportLeds.
// UNSCOPED (struct-nested like DeckRef) because the values ARE the external PPQN and are used as
// ints (Driver::SetPPQNIn). Item 5c (R2): the last granular type-leak out of the contract; the
// Driver *class* stays in granular (relocating it is deferred to a transport-capable 2nd engine).
struct ClockSource {
    enum Source : uint8_t {
        internal = 1,
        ts4      = 4,
        midi     = 24
    };
};

// Key-interval grid (was driver.h's kKeyInterval): the musical lengths the key-quarter clock divides
// to, in 1/16 units (k4_4 = 1 bar). UNSCOPED + int8_t because the values ARE PPQN-relative tick
// counts used arithmetically - the UI's ring layout does interval/4, the Driver uses them as
// divisors. Contract-owned (Phase 5 R4) so the UI's key-interval ring display no longer includes the
// granular driver.h. The granular Driver keeps its kKeyIntervals[] lookup; only the enum is shared.
enum kKeyInterval: int8_t {
    k1_16   = 1,    // 1/16th
    k1_4    = 4,
    k2_4    = 8,
    k3_4    = 12,
    k4_4    = 16,   // 1 bar
    k5_4    = 20,
    k6_4    = 24,
    k7_4    = 28,
    k8_4    = 32,   // 2 bars
    k9_4    = 36,
    k10_4   = 40,
    k11_4   = 44,
    k12_4   = 48,   // 3 bars
    k13_4   = 52,
    k14_4   = 56,
    k15_4   = 60,
    k16_4   = 64    // 4 bars
};

};
