// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>

#include "ui/led.ring.h"

namespace spotykach {

// Hardware-agnostic description of the panel's LEDs, produced by an engine's render() and
// blitted to the WS2812 chain by the platform. The engine never touches Hardware (no apply());
// it draws into these fields. The platform maps:
//   ring[0..1] -> Hardware::LED_RING_A / LED_RING_B (32 px each)
//   the named indicators -> their Hardware::LedId.
//
// Option A (locked after reading led.ring.{h,cpp}): a ring IS a LEDRing - now hardware-free, so
// the engine reuses LEDRing's existing drawing primitives (set_segment/add_point/set_hex_color)
// verbatim and the platform blits each ring via LEDRing::apply(sink). The indicators are plain
// {rgb, brightness} data; their blit (the platform's _led[]) is a later migration sub-round.
struct DisplayModel {
    struct Indicator { uint32_t rgb; float brightness; };

    LEDRing ring[2];                     // per-deck 32-LED ring canvases (hardware-free)

    Indicator play[2], rev[2], grit[2], flux[2], gate_in[2], cycle[2], alt[2];
    Indicator mode_left, mode_center, mode_right, clock_in, fader[2], spot;

    void clear() {
        ring[0].clear();
        ring[1].clear();
        // Zero every indicator. Reassigning the arrays/scalars is cheap and avoids memset on a
        // struct that now holds non-trivial LEDRing members.
        const Indicator off = { 0, 0.f };
        for (int i = 0; i < 2; i++) { play[i] = rev[i] = grit[i] = flux[i] = off;
                                      gate_in[i] = cycle[i] = alt[i] = fader[i] = off; }
        mode_left = mode_center = mode_right = clock_in = spot = off;
    }
};

};
