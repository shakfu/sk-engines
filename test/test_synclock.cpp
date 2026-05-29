// Host tests for spotykach::SynClock.
// Locks the S3 finding: external-clock sync emits exactly _ticks_per_clock
// internal ticks per incoming clock pulse (no off-by-one, no drift), driven by
// the hold/resync mechanism around `_tempo_ticks >= _ticks_per_clock - 1`.
#include "synclock.h"
#include "check.h"
#include <vector>

using namespace spotykach;

// Drive `external_periods` clock pulses, firing `internal_per_period` internal
// timer interrupts between each pulse. Returns the count of on_tick callbacks
// attributed to each external period.
static std::vector<int> drive(uint32_t ppqn_in, int external_periods,
                              int internal_per_period) {
  SynClock c;
  int ticks = 0;
  c.SetOnTick([&](const bool) { ticks++; });
  c.Init(10417, 48);          // ~1 internal tick per interrupt at the 120 BPM default
  c.SetPPQNIn(ppqn_in);       // _ticks_per_clock = 48 / ppqn_in
  c.SetExternalClock(true);
  c.Run();                    // schedules start; first external pulse kicks off

  std::vector<int> per;
  int prev = 0;
  for (int p = 0; p < external_periods; p++) {
    c.Tick(true);                                  // external clock pulse -> resync
    for (int i = 0; i < internal_per_period; i++)  // internal timer interrupts
      c.Tick(false);
    per.push_back(ticks - prev);
    prev = ticks;
  }
  return per;
}

void run_synclock_tests() {
  std::printf("SynClock:\n");

  // S3, the headline case: 24 PPQN MIDI clock into the 48 PPQN internal grid.
  // Expect exactly 2 internal ticks per external pulse in steady state.
  {
    const int N = 64, WARMUP = 4, EXPECT = 2;  // 48/24
    auto per = drive(24, N, /*internal_per_period=*/2);

    // Cumulative invariant: no drift in total tick count over steady periods.
    int sum = 0;
    for (int p = WARMUP; p < N; p++) sum += per[p];
    CHECK_EQ(sum, EXPECT * (N - WARMUP));

    // Strong per-period invariant: each steady period emits exactly EXPECT.
    int exact = 0;
    for (int p = WARMUP; p < N; p++)
      if (per[p] == EXPECT) exact++;
    CHECK_EQ(exact, N - WARMUP);
  }

  // Over-driving the internal timer must not emit extra ticks: the hold caps
  // free-run at _ticks_per_clock-1 and resync tops up to exactly 2.
  {
    const int N = 64, WARMUP = 4, EXPECT = 2;
    auto per = drive(24, N, /*internal_per_period=*/8);
    int sum = 0;
    for (int p = WARMUP; p < N; p++) sum += per[p];
    CHECK_EQ(sum, EXPECT * (N - WARMUP));
  }

  // A different ratio: 12 PPQN -> 48 PPQN expects 4 internal ticks per pulse.
  {
    const int N = 64, WARMUP = 4, EXPECT = 4;  // 48/12
    auto per = drive(12, N, /*internal_per_period=*/4);
    int sum = 0;
    for (int p = WARMUP; p < N; p++) sum += per[p];
    CHECK_EQ(sum, EXPECT * (N - WARMUP));
  }
}
