// Host tests for spotykach::SpeedMap (pitch-in-semitones -> playback-speed ratio).
// Strong assertions target the pure paths (init, bipolar_pitch2speed); the
// daisysp::fmap dependency is satisfied by test/stubs/daisysp.h.
#include "speed.map.h"
#include "check.h"
#include <cmath>

using namespace spotykach;

void run_speedmap_tests() {
  std::printf("SpeedMap:\n");

  // half_range = 60 semitones (+/- 5 octaves) -> 121 table entries.
  SpeedMap<60> m;
  m.init();
  CHECK_EQ(m.range(), 121);

  // Center and octave anchors: speed = 2^(semitones/12).
  CHECK_NEAR(m.bipolar_pitch2speed(0.f), 1.0f, 1e-5);    // no shift
  CHECK_NEAR(m.bipolar_pitch2speed(12.f), 2.0f, 1e-4);   // +1 octave
  CHECK_NEAR(m.bipolar_pitch2speed(-12.f), 0.5f, 1e-4);  // -1 octave
  CHECK_NEAR(m.bipolar_pitch2speed(24.f), 4.0f, 1e-4);   // +2 octaves
  CHECK_NEAR(m.bipolar_pitch2speed(7.f), std::pow(2.f, 7.f / 12.f), 1e-4);  // a fifth

  // Clamping to the table ends (+/- 5 octaves).
  CHECK_NEAR(m.bipolar_pitch2speed(-1000.f), std::pow(2.f, -5.f), 1e-4);  // front
  CHECK_NEAR(m.bipolar_pitch2speed(1000.f), std::pow(2.f, 5.f), 1e-3);    // back

  // Fractional pitch interpolates monotonically between the integer anchors.
  float a = m.bipolar_pitch2speed(0.f);
  float mid = m.bipolar_pitch2speed(0.5f);
  float b = m.bipolar_pitch2speed(1.f);
  CHECK(mid > a && mid < b);

  // Monotonic increasing across the usable range.
  bool increasing = true;
  float prev = m.bipolar_pitch2speed(-48.f);
  for (float p = -47.f; p <= 48.f; p += 1.f) {
    float cur = m.bipolar_pitch2speed(p);
    if (cur <= prev) increasing = false;
    prev = cur;
  }
  CHECK(increasing);
}
