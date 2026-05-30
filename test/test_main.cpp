#include "check.h"

void run_divider_tests();
void run_synclock_tests();
void run_config_tests();
void run_wav_tests();
void run_speedmap_tests();

int main() {
  run_divider_tests();
  run_synclock_tests();
  run_config_tests();
  run_wav_tests();
  run_speedmap_tests();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
