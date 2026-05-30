#pragma once
// Float <-> int16 sample conversion for the optional 16-bit ("lo-fi") loop buffer.
// Pure, host-testable; no hardware or library dependencies.
//
// Scope note: these helpers are the foundation for storing the loop buffer as int16
// (see docs/lofi-int16-scope.md). They are always compiled, but are only wired into the
// buffer/persistence path when LOFI_INT16 is enabled - which is a later, larger step.
// Enabling LOFI_INT16 alone changes only the WAV header format (wav.h); it does NOT by
// itself change Buffer::Frame, so do not ship LOFI_INT16 until the buffer storage and
// load-conversion are done, or saved files will mislabel float data as 16-bit PCM.

#include <cstdint>
#include <algorithm>

namespace spotykach {

// Encode a normalized sample to int16. The float buffer tolerated values beyond +/-1.0;
// int16 must not wrap, so out-of-range input is clamped (a hard clip). The scale is 32767
// so +1.0 -> 32767 and -1.0 -> -32767 (symmetric; +1.0 never overflows int16's +32767 max).
inline int16_t float_to_i16(float x)
{
    x = std::clamp(x, -1.f, 1.f);
    // Round-to-nearest without pulling in <cmath>/lround.
    return static_cast<int16_t>(x * 32767.f + (x >= 0.f ? 0.5f : -0.5f));
}

// Decode int16 back to a normalized float. Uses the same 32767 scale, so the endpoints
// (+/-32767) map exactly back to +/-1.0.
inline float i16_to_float(int16_t x)
{
    return static_cast<float>(x) * (1.f / 32767.f);
}

}  // namespace spotykach
