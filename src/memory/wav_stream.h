#pragma once

#include "memory/audio_stream.h"  // IChunkSource / IChunkSink
#include "memory/byte_file.h"
#include "memory/wav.h"

#include <cstdint>

namespace spotykach {

// Streaming WAV codec: presents a WAV file on disk to PlayStream/RecordStream as a plain byte
// source/sink (IChunkSource/IChunkSink), so the ring machinery stays format-agnostic. Reuses the
// 44-byte canonical header build/parse in wav.h. The classic streaming-write problem - the final length
// isn't known when recording starts - is handled by writing a placeholder header, streaming the body,
// then seeking back to 0 and patching the size fields on finalize().

// Reads a WAV body as a byte stream: parse the header up front, seek to the data chunk, then hand out
// body bytes, stopping exactly at DataSize (so trailing chunks past `data` aren't streamed as audio).
class WavStreamReader : public IChunkSource {
public:
    // `f` must be an open file positioned at 0. Returns false on a missing/invalid header.
    bool begin(IByteFile* f) {
        _f = f; _remaining = 0;
        uint8_t hdr[64];
        const uint32_t got = f->read(hdr, sizeof(hdr));
        WavHeader h; size_t header_size = 0;
        if (!wav_header(hdr, got, h, header_size)) return false;
        if (!f->seek(static_cast<uint32_t>(header_size))) return false;
        _remaining = h.DataSize;
        return true;
    }

    uint32_t read(uint8_t* dst, uint32_t n) override {
        if (n > _remaining) n = _remaining;
        const uint32_t got = _f->read(dst, n);
        _remaining -= got;
        return got;
    }
    bool eof() const override { return _remaining == 0; }

    uint32_t body_remaining() const { return _remaining; }

private:
    IByteFile* _f = nullptr;
    uint32_t   _remaining = 0;  // body bytes not yet read
};

// Writes a WAV body as a byte stream: emit a placeholder header, append body bytes (counting them),
// then on finalize() rewrite the header with the real DataSize/RIFF size.
class WavStreamWriter : public IChunkSink {
public:
    // `f` must be an open, writable file positioned at 0.
    bool begin(IByteFile* f) {
        _f = f; _body = 0;
        const WavHeader h = wav_header(0);  // placeholder (DataSize 0) - patched in finalize()
        return _f->write(&h, sizeof(h)) == sizeof(h);
    }

    uint32_t write(const uint8_t* src, uint32_t n) override {
        const uint32_t w = _f->write(src, n);
        _body += w;
        return w;
    }

    void finalize() override {
        const WavHeader h = wav_header(_body);  // real sizes now known
        if (_f->seek(0)) _f->write(&h, sizeof(h));
        // The concrete file's owner closes it (flush happens there).
    }

    uint32_t body_bytes() const { return _body; }

private:
    IByteFile* _f = nullptr;
    uint32_t   _body = 0;  // body bytes written so far
};

} // namespace spotykach
