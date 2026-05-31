// Host shim for <daisy.h>.
//
// The DSP core itself no longer depends on libDaisy (Phase 0), but it pulls in the
// project's common.h, which includes <daisy.h> for the logger and a couple of macros.
// On the desktop host we don't link libDaisy, so this minimal shim provides just those
// symbols. It is placed first on the host include path so <daisy.h> resolves here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#ifndef DSY_CLAMP
#define DSY_CLAMP(in, mn, mx) ((in) < (mn) ? (mn) : ((in) > (mx) ? (mx) : (in)))
#endif

// Float-format logging macros (real definitions live in libDaisy's hid/logger.h).
// Logging is a no-op on the host, so these only need to be syntactically valid call args.
#ifndef FLT_FMT
#define FLT_FMT(_n) "%f"
#define FLT_VAR(_n, _x) (_x)
#define FLT_FMT3 "%f"
#define FLT_VAR3(_x) (_x)
#endif

namespace daisy {

enum LoggerDestination {
    LOGGER_NONE = 0,
    LOGGER_INTERNAL,
    LOGGER_EXTERNAL,
};

template <LoggerDestination dest = LOGGER_NONE>
struct Logger {
    static void StartLog(bool = false) {}
    static void Print(const char*, ...) {}
    static void PrintLine(const char*, ...) {}
    static void PrintLineV(const char*, va_list) {}
};

} // namespace daisy
