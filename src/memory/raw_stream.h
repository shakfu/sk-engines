#pragma once

#include "memory/audio_stream.h"  // IChunkSource
#include "memory/byte_file.h"

#include <cstdint>
#include <cstring>

namespace spotykach {

// Streaming reader for a 16-bit signed mono PCM body, little-endian, used by the radio engine. It serves
// two layouts behind one int16 streaming path:
//   - begin()     : HEADERLESS raw (the RadioMusic ".raw" format) - the whole file is the body, so the
//                   length is filesize/2 and the body starts at byte 0.
//   - begin_wav() : a 16-bit-mono PCM ".wav" - the header is parsed (chunk walk) to find where the body
//                   starts and how long it is, and the file's own sample rate is reported out.
// Either way the body bytes are int16 mono, so the engine converts them the same way; only the body
// offset differs (raw = 0, wav = past the header). seek_to_frame()/rewind() are relative to that offset,
// so the free-running-playhead jump and looping work identically for both. Endianness: byte-oriented,
// and device (STM32) + host are little-endian, matching the format, so bytes pass straight through.
class RawStreamReader : public IChunkSource {
public:
    static constexpr uint32_t kBytesPerFrame = 2;  // signed 16-bit, mono
    static constexpr uint32_t kMaxChunks = 64;     // WAV chunk-walk bound

    // Headerless raw. `filesize` is the total byte length (from f_stat). Body = the whole file.
    bool begin(IByteFile* f, uint32_t filesize) {
        _f = f;
        _data_start = 0;
        _data_size  = filesize & ~(kBytesPerFrame - 1u);   // floor to a whole frame
        _remaining  = _data_size;
        return _f != nullptr && _data_size >= kBytesPerFrame;
    }

    // 16-bit-mono PCM WAV. Parses the header (spec-compliant chunk walk), validates PCM/16-bit/mono,
    // sets the body offset+length, and reports the file's sample rate in `out_rate`. Returns false on a
    // missing/invalid/unsupported header (caller then treats it as a non-station). `filesize` clamps the
    // data chunk to what is actually present (a truncated file).
    bool begin_wav(IByteFile* f, uint32_t filesize, uint32_t& out_rate) {
        _f = f; _data_start = 0; _data_size = 0; _remaining = 0; out_rate = 0;
        uint8_t riff[12];
        if (!f->seek(0) || f->read(riff, sizeof(riff)) != sizeof(riff)) return false;
        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return false;

        uint16_t fmt = 0, ch = 0, bits = 0; uint32_t rate = 0; bool haveFmt = false;
        uint32_t pos = 12;
        for (uint32_t guard = 0; guard < kMaxChunks; guard++) {
            uint8_t hdr[8];
            if (!f->seek(pos) || f->read(hdr, sizeof(hdr)) != sizeof(hdr)) return false;
            const uint32_t size = rd32(hdr, 4);
            const uint32_t body = pos + 8;
            if (std::memcmp(hdr, "fmt ", 4) == 0) {
                if (size < 16) return false;
                uint8_t fb[16];
                if (f->read(fb, sizeof(fb)) != sizeof(fb)) return false;
                fmt = rd16(fb, 0); ch = rd16(fb, 2); rate = rd32(fb, 4); bits = rd16(fb, 14);
                if (fmt == 0xFFFE && size >= 40) {        // WAVE_FORMAT_EXTENSIBLE: real tag in the GUID
                    uint8_t tag[2];
                    if (!f->seek(body + 24) || f->read(tag, sizeof(tag)) != sizeof(tag)) return false;
                    fmt = rd16(tag, 0);
                }
                haveFmt = true;
            } else if (std::memcmp(hdr, "data", 4) == 0) {
                if (!haveFmt || fmt != 1 || bits != 16 || ch != 1) return false;  // 16-bit mono PCM only
                uint32_t ds = size;
                if (body + ds > filesize) ds = (filesize > body) ? (filesize - body) : 0;  // truncated
                ds &= ~(kBytesPerFrame - 1u);
                if (ds < kBytesPerFrame) return false;
                _data_start = body; _data_size = ds; _remaining = ds; out_rate = rate;
                return f->seek(body);
            }
            pos = body + size + (size & 1u);              // next chunk (word-aligned)
        }
        return false;
    }

    uint32_t read(uint8_t* dst, uint32_t n) override {
        if (n > _remaining) n = _remaining;
        const uint32_t got = _f->read(dst, n);
        _remaining -= got;
        return got;
    }
    bool eof() const override { return _remaining == 0; }

    // Loop support: seek back to the body start so a looping station repeats seamlessly.
    void rewind() override { if (_f && _f->seek(_data_start)) _remaining = _data_size; }

    // Seek to source frame `frame` (clamped into the body); subsequent reads continue from there. The
    // radio's free-running playhead calls this on a station change before streaming forward.
    bool seek_to_frame(uint32_t frame) {
        uint32_t off = frame * kBytesPerFrame;
        if (off > _data_size) off = _data_size;           // clamp; an empty tail just reads as eof
        if (!_f || !_f->seek(_data_start + off)) return false;
        _remaining = _data_size - off;
        return true;
    }

    uint32_t frames()     const { return _data_size / kBytesPerFrame; }
    uint32_t data_bytes() const { return _data_size; }

private:
    static uint16_t rd16(const uint8_t* p, int o) { return static_cast<uint16_t>(p[o] | (p[o + 1] << 8)); }
    static uint32_t rd32(const uint8_t* p, int o) {
        return static_cast<uint32_t>(p[o]) | (static_cast<uint32_t>(p[o + 1]) << 8) |
               (static_cast<uint32_t>(p[o + 2]) << 16) | (static_cast<uint32_t>(p[o + 3]) << 24);
    }

    IByteFile* _f = nullptr;
    uint32_t   _remaining  = 0;   // body bytes not yet read
    uint32_t   _data_start = 0;   // byte offset of the body (0 raw, past the header for wav)
    uint32_t   _data_size  = 0;   // whole-frame-floored body length, in bytes
};

} // namespace spotykach
