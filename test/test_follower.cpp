// Host tests for spotykach::Follower (mean-square envelope follower).
// daisysp::fclamp is supplied by test/stubs/daisysp.h.
#include "follower.h"
#include "check.h"

using namespace spotykach;

// Run a constant input through the follower for `n` steps and return value().
static float settle(Follower& f, float in, int n) {
  for (int i = 0; i < n; i++) f.process(in);
  return f.value();
}

void run_follower_tests() {
  std::printf("Follower:\n");

  // reset() forces the envelope to zero.
  {
    Follower f;
    f.set_speed(0.f);
    settle(f, 0.5f, 100);
    f.reset();
    CHECK_EQ(f.value(), 0.f);
  }

  // Steady state: value() converges to clamp(mean_square * amp). With the default
  // amp (1000) and in = 0.02236 (in^2 ~= 5e-4), the target is ~0.5.
  {
    Follower f;
    f.set_speed(0.f);  // fastest tracking
    float v = settle(f, 0.02236f, 40000);
    CHECK_NEAR(v, 0.5f, 2e-2);
  }

  // Output is clamped to [0,1]: a large input saturates value() at 1, never above.
  {
    Follower f;
    f.set_speed(0.f);
    bool over = false;
    for (int i = 0; i < 5000; i++) {
      f.process(1.0f);
      if (f.value() > 1.0f) over = true;
    }
    CHECK(!over);
    CHECK_NEAR(f.value(), 1.0f, 1e-6);
  }

  // amp scales the output; amp of zero mutes regardless of input.
  {
    Follower f;
    f.set_speed(0.f);
    f.set_amp(0.f);
    CHECK_EQ(settle(f, 0.5f, 1000), 0.f);
  }

  // Attack is faster than release: rising to a level takes fewer steps than
  // falling back from it. Small input keeps value() unsaturated (~0.1).
  {
    Follower f;
    f.set_speed(0.5f);
    const float in = 0.01f;     // in^2 = 1e-4 -> value() target ~0.1
    const float half = 0.05f;

    f.reset();
    int attack = 0;
    while (f.value() < half && attack < 1000000) {
      f.process(in);
      attack++;
    }

    settle(f, in, 200000);      // fully settle at the high level
    int release = 0;
    while (f.value() > half && release < 1000000) {
      f.process(0.f);
      release++;
    }

    CHECK(attack > 0);
    CHECK(release > 0);
    CHECK(attack < release);    // attack ms range (22) << release ms range (3992)
  }
}
