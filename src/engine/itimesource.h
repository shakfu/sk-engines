// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace spotykach {

// Platform clock abstraction. The DSP core depends only on this interface, never on
// libDaisy's daisy::System directly, so the same core can run on hardware or on a host.
// now_ms() is wall-clock milliseconds (tap tempo); now_us() is microseconds (reset timing).
struct ITimeSource {
    virtual ~ITimeSource() = default;
    virtual uint32_t now_ms() const = 0;
    virtual uint32_t now_us() const = 0;
};

};
