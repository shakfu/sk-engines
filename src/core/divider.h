#pragma once

#include <cmath>    // std::round
#include <stddef.h> // size_t
#include <stdint.h>

namespace spotykach {

enum class Every: uint8_t {
  _4th  = 1,
  _8th  = 2,
  _16th = 4,
  _32th = 8
};

class Divider {
public:
  Divider(size_t ppqn, Every resolution = Every::_16th);

  Every resolution() const { return _resolution; }

  void set_swing(const float frac_swing) {
      // For 48 ppqn. Conversion to actual ppqn is
      // considered in _swing_kof ////////////////
      // |  0  |  1  |  2  |  3  |  4  |  5  |
      // | 50% | 54% | 58% | 62% | 66% | 70% |
      if (_swing_on) _swing = static_cast<size_t>(std::round(frac_swing * _swing_kof));
  }

  void set_triplets_on(const bool on) { _triplets_on = on; }

  bool tick();

  void reset();

private:
    float _swing_kof;
    size_t _swing;
    size_t _pulses_per_bar;
    size_t _pulses_per_trigger;
    size_t _triggers_per_bar;
    size_t _iterator;
    size_t _trigger_count;
    size_t _next_trigger;
    size_t _odd_count;
    size_t _odd_count_max;
    Every _resolution;
    bool _is_odd;
    bool _swing_on;
    bool _triplets_on;
};

};
