#pragma once

#include <array>
#include <algorithm>
#include "nocopy.h"
#include "itimesource.h"

namespace spotykach {

class Tempo {
public:
    static float abs_to_norm(const float abs)
    {
        return (abs - kMin) / (kMax - kMin);
    }

    Tempo();
    ~Tempo() = default;

    void set_time_source(const ITimeSource* time) { _time = time; }

    void set_norm(const float value) {
        _bpm = kMin + std::clamp(value, 0.f, 1.f) * (kMax - kMin);
    }
    float bpm() const { return _bpm; }

    void tap();

private:
    NOCOPY(Tempo)

    static constexpr auto kMax = 250.f;
    static constexpr auto kMin = 20.f;

    const ITimeSource* _time = nullptr;
    std::array<size_t, 4> _times;
    size_t _prev_time;
    float _avg;
    float _bpm;
    uint8_t _pointer;
    bool _full;
};

};
