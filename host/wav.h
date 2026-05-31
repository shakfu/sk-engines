// Minimal WAV I/O for the desktop host harness. Reads/writes 16-bit PCM, mono or stereo.
// Deliberately tiny (no external deps); enough to push audio through the core off-target.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace host {

struct Audio {
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    std::vector<float> l; // deinterleaved, [-1, 1]
    std::vector<float> r;
    size_t frames() const { return l.size(); }
};

namespace detail {
    inline uint32_t rd_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
    inline uint16_t rd_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
}

// Read a 16-bit PCM WAV. Returns false on any parse problem.
inline bool read_wav(const std::string& path, Audio& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> buf;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 44) { std::fclose(f); return false; }
    buf.resize(static_cast<size_t>(n));
    size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (got != buf.size()) return false;
    if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    size_t pos = 12;
    const uint8_t* data = nullptr;
    uint32_t data_bytes = 0;
    while (pos + 8 <= buf.size()) {
        const uint8_t* chunk = buf.data() + pos;
        uint32_t sz = detail::rd_u32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            fmt = detail::rd_u16(chunk + 8);
            ch = detail::rd_u16(chunk + 10);
            rate = detail::rd_u32(chunk + 12);
            bits = detail::rd_u16(chunk + 22);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            data_bytes = sz;
            break;
        }
        pos += 8 + sz + (sz & 1);
    }
    if (!data || fmt != 1 || bits != 16 || ch < 1 || ch > 2) return false;

    out.sample_rate = rate;
    out.channels = ch;
    size_t frames = data_bytes / (2u * ch);
    out.l.resize(frames);
    out.r.resize(frames);
    const int16_t* s = reinterpret_cast<const int16_t*>(data);
    for (size_t i = 0; i < frames; i++) {
        out.l[i] = s[i * ch + 0] / 32768.0f;
        out.r[i] = (ch == 2) ? s[i * ch + 1] / 32768.0f : out.l[i];
    }
    return true;
}

// Write a 16-bit PCM stereo WAV.
inline bool write_wav(const std::string& path, const Audio& a) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t ch = 2;
    const uint32_t bits = 16;
    const uint32_t rate = a.sample_rate;
    const uint32_t frames = static_cast<uint32_t>(a.frames());
    const uint32_t data_bytes = frames * ch * (bits / 8);
    const uint32_t byte_rate = rate * ch * (bits / 8);
    const uint16_t block_align = ch * (bits / 8);

    auto w32 = [&](uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); };
    auto w16 = [&](uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); };

    std::fwrite("RIFF", 1, 4, f); w32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(ch); w32(rate); w32(byte_rate); w16(block_align); w16(bits);
    std::fwrite("data", 1, 4, f); w32(data_bytes);

    auto clamp16 = [](float x) -> int16_t {
        float v = x * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        return static_cast<int16_t>(v);
    };
    for (uint32_t i = 0; i < frames; i++) {
        w16(static_cast<uint16_t>(clamp16(a.l[i])));
        w16(static_cast<uint16_t>(clamp16(a.r[i])));
    }
    std::fclose(f);
    return true;
}

} // namespace host
