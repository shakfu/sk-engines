// Host tests for spotykach::Config's text parser (config.txt).
// Documents the S5 brittleness (8-char fixed property names, whitespace stripped
// mid-token, unknown/overlong names silently ignored) and exercises the V1 fix
// (is_loaded() defaults to false). Config is a singleton, so these run against the
// shared instance; each case checks only the properties it sets, keeping order
// independence, except the is_loaded() transition which is asserted first.
#include "config.h"
#include "check.h"
#include <cstring>

using namespace spotykach;

static void fill(const char* s) {
  Config::dynamic().fill(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

void run_config_tests() {
  std::printf("Config:\n");
  Config& c = Config::dynamic();

  // V1: with no config loaded yet, the flag is false (not indeterminate).
  CHECK(!c.is_loaded());

  // Happy path. Property name on one line, value on the next. midi_channel_*
  // is stored as value-1; pre_load is a bool; play_stop is stored as-is.
  {
    fill("mid_ch_a\n5\n"
         "mid_ch_b\n3\n"
         "mid_ps_a\n1\n"
         "mid_ps_b\n1\n"
         "pre_load\n0\n");
    CHECK_EQ((int)c.midi_channel_a(), 4);
    CHECK_EQ((int)c.midi_channel_b(), 2);
    CHECK_EQ((int)c.midi_play_stop_a(), 1);
    CHECK_EQ((int)c.midi_play_stop_b(), 1);
    CHECK(!c.is_preload_on());
    CHECK(c.is_loaded());  // flips true once any known property is parsed
  }

  // S5 quirk: spaces are stripped *anywhere* in a token, not just trimmed at the
  // ends. "mid_ch _a" collapses to the 8-char "mid_ch_a" and still matches.
  {
    fill("mid_ch _a\n2\n");
    CHECK_EQ((int)c.midi_channel_a(), 1);  // 2 - 1, i.e. the space was ignored
  }

  // S5 quirk: unknown or overlong property names are silently ignored (they do
  // not match any 8-byte memcmp), leaving prior values untouched.
  {
    fill("pre_load\n1\n");            // set a known baseline
    CHECK(c.is_preload_on());
    fill("midi_channel_a\n9\n");      // >8 chars -> truncated, matches nothing
    fill("nope\n7\n");                // unknown -> ignored
    CHECK(c.is_preload_on());         // baseline unchanged by the ignored lines
  }

  // Robustness: empty and null input must not crash or alter state.
  {
    fill("");
    Config::dynamic().fill(nullptr, 0);
    CHECK(c.is_loaded());  // still loaded from earlier; no spurious reset
  }
}
