#pragma once
// Minimal zero-dependency assertion helper for the host test harness.
// Host-only: this code is never compiled into the firmware.
#include <cstdio>

inline int g_pass = 0;
inline int g_fail = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (cond) {                                                                \
      ++g_pass;                                                                \
    } else {                                                                   \
      ++g_fail;                                                                \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                          \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
  do {                                                                         \
    double _a = (a);                                                           \
    double _b = (b);                                                           \
    double _d = _a - _b;                                                       \
    if (_d < 0) _d = -_d;                                                      \
    if (_d <= (eps)) {                                                         \
      ++g_pass;                                                                \
    } else {                                                                   \
      ++g_fail;                                                                \
      std::printf("  FAIL %s:%d  %s ~= %s  (got %g vs %g)\n", __FILE__,        \
                  __LINE__, #a, #b, _a, _b);                                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    auto _a = (a);                                                             \
    auto _b = (b);                                                             \
    if (_a == _b) {                                                            \
      ++g_pass;                                                                \
    } else {                                                                   \
      ++g_fail;                                                                \
      std::printf("  FAIL %s:%d  %s == %s  (got %lld vs %lld)\n", __FILE__,    \
                  __LINE__, #a, #b, (long long)_a, (long long)_b);             \
    }                                                                          \
  } while (0)
