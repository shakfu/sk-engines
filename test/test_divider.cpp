// Host tests for spotykach::Divider.
// Locks the S1 finding: triplet timing re-derives from an absolute per-bar grid,
// so it does NOT accumulate phase drift over long runs.
#include "divider.h"
#include "check.h"
#include <set>
#include <vector>

using namespace spotykach;

void run_divider_tests() {
  std::printf("Divider:\n");

  // Baseline: 1/8 grid at 48 PPQN, no triplets/swing -> 8 triggers per bar,
  // landing exactly on pulses 0, 24, 48, ... 168.
  {
    Divider d(48, Every::_8th);
    std::vector<int> pos;
    for (int i = 0; i < 192; i++) {
      if (d.tick()) pos.push_back(i);
    }
    CHECK_EQ((int)pos.size(), 8);
    for (int k = 0; k < (int)pos.size(); k++) CHECK_EQ(pos[k], k * 24);
  }

  // S1: with triplets on, the set of trigger positions within a bar must be
  // identical across bars even after a long run. If the 0.6667 truncation
  // accumulated, an early bar and a late bar would differ.
  {
    Divider d(48, Every::_8th);
    d.set_triplets_on(true);
    const int BAR = 192;
    const int BARS = 2000;
    std::set<int> early, late;
    for (int b = 0; b < BARS; b++) {
      for (int i = 0; i < BAR; i++) {
        if (d.tick()) {
          if (b == 1) early.insert(i);
          if (b == BARS - 1) late.insert(i);
        }
      }
    }
    CHECK(!early.empty());
    CHECK(early == late);  // no cumulative drift between bar 1 and bar 1999
  }

  // S1, stronger: the full per-bar trigger pattern repeats with a fixed period.
  // Compare bar 2 against bar 2 + 500; identical => bounded, periodic, no drift.
  {
    Divider d(48, Every::_16th);  // 16th enables swing latch (swing magnitude 0)
    d.set_triplets_on(true);
    const int BAR = 192;
    std::vector<int> ref, later;
    for (int b = 0; b < 600; b++) {
      for (int i = 0; i < BAR; i++) {
        bool t = d.tick();
        if (b == 2 && t) ref.push_back(i);
        if (b == 502 && t) later.push_back(i);
      }
    }
    CHECK(ref == later);
  }
}
