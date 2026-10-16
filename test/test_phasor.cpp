// Host tests for the granular fixed-point playhead (src/engine/granular/phasor.h).
#include <cmath>
#include "phasor.h"
#include "check.h"

using namespace spotykach;

void run_phasor_tests() {
  std::printf("phasor:\n");

  // Integer construction has no fraction.
  CHECK_EQ(Phasor(int32_t(7)).integral(), 7);
  CHECK_EQ(Phasor(int32_t(7)).fraction(), 0.f);

  // Float construction splits into floor + fraction, including negatives.
  CHECK_EQ(Phasor(2.5f).integral(), 2);
  CHECK_NEAR(Phasor(2.5f).fraction(), 0.5, 1e-7);
  CHECK_EQ(Phasor(-0.25f).integral(), -1);
  CHECK_NEAR(Phasor(-0.25f).fraction(), 0.75, 1e-7);

  // The fraction never rounds up to 1.0, so a read never lands on the next frame.
  CHECK(Phasor(0.99999994f).fraction() < 1.f);

  // Reverse playback subtracts below zero: integral is floor, fraction stays in [0, 1).
  Phasor rev(int32_t(1));
  rev -= Phasor(1.5f);
  CHECK_EQ(rev.integral(), -1);
  CHECK_NEAR(rev.fraction(), 0.5, 1e-7);

  // The defect this replaces: a float playhead 2M frames in (~42 s at 48 kHz) has a 1/8-frame ulp,
  // so each add rounds the pitch increment. The fixed-point sum stays exact.
  const float inc = 1.0594631f;               // one semitone up
  const int steps = 2000000;
  Phasor ph;
  const Phasor pinc(inc);
  float fl = 0.f;
  for (int i = 0; i < steps; i++) { ph += pinc; fl += inc; }
  const double exact = static_cast<double>(inc) * steps;
  CHECK_NEAR(ph.integral() + static_cast<double>(ph.fraction()), exact, 1e-3);
  CHECK(std::fabs(static_cast<double>(fl) - exact) > 1.0);   // float drifts by whole frames
}
