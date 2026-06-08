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
        _f = f; _remaining = 0; _data_start = 0; _data_size = 0;
        uint8_t hdr[64];
        const uint32_t got = f->read(hdr, sizeof(hdr));
        WavHeader h{}; size_t header_size = 0;
        if (!wav_header(hdr, got, h, header_size)) return false;
        if (!f->seek(static_cast<uint32_t>(header_size))) return false;
        _data_start = static_cast<uint32_t>(header_size);
        _data_size  = h.DataSize;
        _remaining  = h.DataSize;
        return true;
    }

    uint32_t read(uint8_t* dst, uint32_t n) override {
        if (n > _remaining) n = _remaining;
        const uint32_t got = _f->read(dst, n);
        _remaining -= got;
        return got;
    }
    bool eof() const override { return _remaining == 0; }

    // Loop support: seek back to the data-chunk start and refill the body counter so reading repeats.
    void rewind() override { if (_f && _f->seek(_data_start)) _remaining = _data_size; }

    uint32_t body_remaining() const { return _remaining; }
    uint32_t data_bytes()     const { return _data_size; }   // total body size (loop length, in bytes)

private:
    IByteFile* _f = nullptr;
    uint32_t   _remaining  = 0;  // body bytes not yet read
    uint32_t   _data_start = 0;  // byte offset of the data-chunk body (rewind target)
    uint32_t   _data_size  = 0;  // total body bytes (DataSize)
};

// Writes a WAV body as a byte stream: emit a placeholder header, append body bytes (counting them),
// then on finalize() rewrite the header with the real DataSize/RIFF size.
class WavStreamWriter : public IChunkSink {
public:
    // `f` must be an open, writable file positioned at 0. `channels` is written into the header (and
    // re-used on finalize) - the streaming body is channel-agnostic, but the header must state the
    // truth so the file opens correctly elsewhere (the tape engine records one mono file per deck).
    bool begin(IByteFile* f, uint16_t channels = 2) {
        _f = f; _body = 0; _channels = channels;
        const WavHeader h = wav_header(0, _channels);  // placeholder (DataSize 0) - patched in finalize()
        return _f->write(&h, sizeof(h)) == sizeof(h);
    }

    uint32_t write(const uint8_t* src, uint32_t n) override {
        const uint32_t w = _f->write(src, n);
        _body += w;
        return w;
    }

    void finalize() override {
        const WavHeader h = wav_header(_body, _channels);  // real sizes now known
        if (_f->seek(0)) _f->write(&h, sizeof(h));
        // The concrete file's owner closes it (flush happens there).
    }

    uint32_t body_bytes() const { return _body; }

private:
    IByteFile* _f = nullptr;
    uint32_t   _body = 0;       // body bytes written so far
    uint16_t   _channels = 2;   // header channel count (2 = stereo default, 1 = mono per tape deck)
};

} // namespace spotykach
