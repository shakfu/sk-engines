// Host tests for convert_pcm_block (src/core/pcm_convert.h), the loop-buffer load shim.
#include "pcm_convert.h"
#include "check.h"
#include <cstring>
#include <vector>

using namespace spotykach;

void run_pcmconvert_tests() {
  std::printf("pcm_convert:\n");

  // f32 -> i16: float input bytes convert to clamped 16-bit, half the byte count.
  {
    float in[4] = {0.f, 1.f, -1.f, 2.f};  // last is out of range -> clamps
    int16_t out[4] = {0, 0, 0, 0};
    convert_pcm_block(reinterpret_cast<const uint8_t*>(in), 4, 4,
                      reinterpret_cast<uint8_t*>(out), 2);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 32767);
    CHECK_EQ(out[2], -32767);
    CHECK_EQ(out[3], 32767);  // clamped
  }

  // i16 -> f32: 16-bit input expands to float, double the byte count.
  {
    int16_t in[3] = {0, 32767, -32767};
    float out[3] = {0, 0, 0};
    convert_pcm_block(reinterpret_cast<const uint8_t*>(in), 3, 2,
                      reinterpret_cast<uint8_t*>(out), 4);
    CHECK_NEAR(out[0], 0.f, 1e-6);
    CHECK_NEAR(out[1], 1.f, 1e-6);
    CHECK_NEAR(out[2], -1.f, 1e-6);
  }

  // f32 -> i16 -> f32 round-trips within one quantization step (the legacy-tape path,
  // mimicking save-as-float then load-into-int16-buffer, or the reverse).
  {
    const int N = 2001;
    std::vector<float> orig(N), back(N);
    std::vector<int16_t> mid(N);
    for (int i = 0; i < N; i++) orig[i] = (i - 1000) / 1000.f;  // -1.0 .. 1.0

    convert_pcm_block(reinterpret_cast<const uint8_t*>(orig.data()), N, 4,
                      reinterpret_cast<uint8_t*>(mid.data()), 2);
    convert_pcm_block(reinterpret_cast<const uint8_t*>(mid.data()), N, 2,
                      reinterpret_cast<uint8_t*>(back.data()), 4);

    bool within = true;
    for (int i = 0; i < N; i++) {
      float d = back[i] - orig[i];
      if (d < 0) d = -d;
      if (d > 2e-5f) within = false;
    }
    CHECK(within);
  }

  // Zero samples is a no-op (does not write or read).
  {
    uint8_t dst[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    convert_pcm_block(nullptr, 0, 4, dst, 2);
    CHECK_EQ((int)dst[0], 0xAA);  // untouched
  }
}
