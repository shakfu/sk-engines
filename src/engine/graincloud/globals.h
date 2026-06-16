#pragma once

#include <array>
#include <stdint.h>
#include <cmath>

namespace spotykach {
    
    static const float kSecondsPerMinute    { 60.0 };

    static constexpr float logVolume(float value) {
        return (std::pow(10.0, value) - 1) / 9.0;
    }

    static const uint32_t kPPQN             { 96 };

    using Step = int;
    static constexpr std::array<Step, 11> EvenSteps {{
        4 * kPPQN,      //1
        3 * kPPQN,      //1 / 2.
        2 * kPPQN,      //1 / 2
        3 * kPPQN / 2,  //1 / 4.
        2 * kPPQN / 3,  //1 / 4T
        1 * kPPQN,      //1 / 4
        1 * kPPQN / 2,  //1 / 8
        1 * kPPQN / 3,  //1 / 8T
        1 * kPPQN / 4,  //1 / 16
        1 * kPPQN / 6,  //1 / 16T
        1 * kPPQN / 8,  //1 / 32
    }};
};
