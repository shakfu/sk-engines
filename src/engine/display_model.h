// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <cstring>

namespace spotykach {

// Hardware-agnostic description of the panel's LEDs, produced by an engine's render() and
// blitted to the WS2812 chain by the platform. The engine never touches Hardware/LEDRing
// (those carry apply(Hardware&)); it only fills this data. The platform maps:
//   ring[0..1] -> Hardware::LED_RING_A / LED_RING_B (32 px each)
//   the named indicators -> their Hardware::LedId.
//
// SKETCH NOTE: the rings are flat pixel buffers here. The real LED migration (3c-v) will most
// likely add primitive helpers on the ring (set_segment/add_point/...) by extracting LEDRing's
// drawing half (minus apply()), so the granular render() can reuse its existing drawing logic.
// This struct is the *data contract*; those helpers are an implementation convenience.
struct DisplayModel {
    static constexpr int kRingPixels = 32;

    struct Pixel     { uint32_t rgb; float brightness; };
    struct Indicator { uint32_t rgb; float brightness; };

    Pixel ring[2][kRingPixels];          // per-deck 32-LED rings

    Indicator play[2], rev[2], grit[2], flux[2], gate_in[2], cycle[2], alt[2];
    Indicator mode_left, mode_center, mode_right, clock_in, fader[2], spot;

    void clear() { std::memset(this, 0, sizeof(*this)); }
};

};
