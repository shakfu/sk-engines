#pragma once
// Block conversion between PCM sample widths for the loop buffer's load path.
// Pure, host-testable; depends only on the sample16 helpers.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "sample16.h"

namespace spotykach {

// Convert `n_samples` interleaved samples (channel layout is irrelevant - each value is
// independent) from `src_bps`-byte to `dst_bps`-byte width, routed through normalized
// float. Widths: 4 = 32-bit IEEE float, 2 = 16-bit PCM. int16 output is clamped by
// float_to_i16. Little-endian, matching the on-disk WAV order and the in-memory layout on
// this little-endian target. `src` and `dst` must not overlap.
inline void convert_pcm_block(const uint8_t* src, size_t n_samples, int src_bps,
                              uint8_t* dst, int dst_bps)
{
    for (size_t i = 0; i < n_samples; i++) {
        float v;
        if (src_bps == 4) { std::memcpy(&v, src, 4); }
        else              { int16_t s; std::memcpy(&s, src, 2); v = i16_to_float(s); }
        src += src_bps;

        if (dst_bps == 4) { std::memcpy(dst, &v, 4); }
        else              { int16_t s = float_to_i16(v); std::memcpy(dst, &s, 2); }
        dst += dst_bps;
    }
}

}  // namespace spotykach
