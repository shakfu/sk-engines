// Host tests for the float<->int16 sample conversion (src/core/sample16.h),
// foundation for the optional 16-bit loop buffer.
#include "sample16.h"
#include "check.h"

using namespace spotykach;

void run_sample16_tests() {
  std::printf("sample16:\n");

  // Zero maps to zero both ways.
  CHECK_EQ(float_to_i16(0.f), 0);
  CHECK_EQ(i16_to_float(0), 0.f);

  // Endpoints encode symmetrically and round-trip exactly.
  CHECK_EQ(float_to_i16(1.f), 32767);
  CHECK_EQ(float_to_i16(-1.f), -32767);
  CHECK_NEAR(i16_to_float(32767), 1.f, 1e-6);
  CHECK_NEAR(i16_to_float(-32767), -1.f, 1e-6);

  // Out-of-range input is clamped (hard clip), never wraps past int16 range.
  CHECK_EQ(float_to_i16(2.f), 32767);
  CHECK_EQ(float_to_i16(-2.f), -32767);
  CHECK_EQ(float_to_i16(100.f), 32767);
  CHECK_EQ(float_to_i16(-100.f), -32767);

  // Mid-scale.
  CHECK_EQ(float_to_i16(0.5f), 16384);   // 0.5 * 32767 = 16383.5 -> rounds to 16384
  CHECK_NEAR(i16_to_float(16384), 0.5f, 5e-5);

  // Round-trip across the range stays within one quantization step (~1/32767).
  bool within = true;
  for (int i = -1000; i <= 1000; i++) {
    float x = i / 1000.f;  // -1.0 .. 1.0
    float r = i16_to_float(float_to_i16(x));
    float d = r - x;
    if (d < 0) d = -d;
    if (d > 2e-5f) within = false;
  }
  CHECK(within);

  // Encoded values always stay inside int16 (no wrap) for any input.
  bool in_range = true;
  for (int i = -2000; i <= 2000; i++) {
    int16_t v = float_to_i16(i / 1000.f);  // includes out-of-range +/-2.0
    if (v > 32767 || v < -32767) in_range = false;
  }
  CHECK(in_range);
}
