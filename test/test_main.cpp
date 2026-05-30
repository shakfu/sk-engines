#include "check.h"

void run_divider_tests();
void run_synclock_tests();
void run_config_tests();
void run_wav_tests();
void run_speedmap_tests();
void run_follower_tests();
void run_sample16_tests();
void run_pcmconvert_tests();
void run_pcmloader_tests();

int main() {
  run_divider_tests();
  run_synclock_tests();
  run_config_tests();
  run_wav_tests();
  run_speedmap_tests();
  run_follower_tests();
  run_sample16_tests();
  run_pcmconvert_tests();
  run_pcmloader_tests();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
