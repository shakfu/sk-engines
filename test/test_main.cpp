#include "check.h"

void run_divider_tests();
void run_synclock_tests();
void run_config_tests();

int main() {
  run_divider_tests();
  run_synclock_tests();
  run_config_tests();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
