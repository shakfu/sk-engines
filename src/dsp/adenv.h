#pragma once

#include <cmath>
#include <cstddef>   // size_t
#include "nocopy.h"

namespace spotykach {

class ADEnvelope {
public:
  ADEnvelope();
  ~ADEnvelope() {};

  size_t length() const { return _length; }
  void set_length(const size_t);

  void set_shape(float);

  void reverse();

  void trigger();

  float process(const float increment);

  void reset();

private:
  NOCOPY(ADEnvelope)

  static constexpr float kCurve = .05f;
  static constexpr float kCurveCof = 128.f * kCurve * kCurve;

  void _make_shape();

  // Derived (not copied) from Stages by Emilie Gillet ///////
  float _amp_attack(const float ph) {
    return ph / (1.f + kCurveCof * (1.f - ph));
  }
  float _amp_decay(const float ph) {
    return (1.f - ph) / (1.f + kCurveCof * ph);
  }
  float _ph_attack(const float amp) {
    return std::roundf(_t_attack * amp * (1 + kCurveCof) / (1 + amp * kCurveCof));
  }
  float _ph_decay(const float amp) {
    return std::roundf(_t_decay * (1 - amp) / (amp * kCurveCof + 1));
  }
  ////////////////////////////////////////////////
  
  float _out;
  float _target_out;
  float _shape;
  float _t_attack_kof;
  float _t_decay_kof;
  float _t_attack;
  float _t_decay;
  float _decay_phase;
  float _phase;
  float _length;
  bool  _smooth_shape;
};

};
