#pragma once

#include "nocopy.h"
#include <array>
#include <math.h>

namespace spotykach {

/**
 * @brief
 * Pattern generator
 * Generates equaly distributed patterns of 1...16 onset.
 * This produces both "regular" and "euclidean" patterns.
 */
class CPattern {
public:
    static constexpr uint8_t kSize = 16;

    CPattern();
    ~CPattern() {}

    void set_shift(const float frac_offset) {
        // 0...half of the pattern
        _shift = static_cast<uint8_t>(std::roundf(frac_offset * kSize));
    }
    void set_max_onsets(uint8_t max_onsets) {
        //0...16
        _max_onsets = std::min(max_onsets, kSize);
    }

    void plus_onset();
    void minus_onset();
    void set_onsets(const uint8_t onsets);
    uint8_t onsets() const { return _onsets; }

    bool trigger();

    uint8_t next_point() {
        return _next_point;
    }

    uint8_t length() const { return _current_length; }

    void reset() { _next_point = 0; }

private:
    NOCOPY(CPattern)

    std::array<uint8_t, kSize> _pattern;
    std::array<uint8_t, kSize> _length;
    uint8_t _current_length;
    uint8_t _onsets;
    uint8_t _max_onsets;
    uint8_t _next_point;
    uint8_t _shift;
};

};
