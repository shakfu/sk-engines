#pragma once
#include <algorithm>
#include "nocopy.h"

namespace spotykach {

// The square law crossfade
// Adopted from Will. C. Pirkle "Designing Software Synthesizer Plugins in C++".
class XFade {
public:
  XFade():
    _stage  { 0.f },
    _lhs    { 1.f },
    _rhs    { 0.f } 
    {}

  void Process(const float lhs0, const float lhs1, const float rhs0, const float rhs1, float& out0, float& out1) {
    out0 = lhs0 * _lhs + rhs0 * _rhs;
    out1 = lhs1 * _lhs + rhs1 * _rhs;
  }

  void SetStage(const float value) {
    _stage = std::clamp(value, 0.f, 1.f);
    auto sq = _stage * _stage;
    _lhs = 1.f - sq;
    _rhs = 2.f * _stage - sq;
  }

  float Stage() const {
    return _stage;
  }

private:
  NOCOPY(XFade)

  float _stage;
  float _lhs;
  float _rhs;
};

};
