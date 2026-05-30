#pragma once
// Minimal host stub for DaisySP, sufficient to compile the pure-logic headers
// under test. Only the symbols actually referenced are provided. This is NOT
// DaisySP; tests that depend on exact DaisySP behaviour must say so.
#include <algorithm>

namespace daisysp {

enum class Mapping { LINEAR, EXP, LOG };

// Linear branch of DaisySP's fmap: clamp `in` to [0,1] then map to [min,max].
// SpeedMap uses the default (LINEAR) form only.
inline float fmap(float in, float min, float max, Mapping = Mapping::LINEAR) {
  in = std::clamp(in, 0.f, 1.f);
  return min + in * (max - min);
}

}  // namespace daisysp
