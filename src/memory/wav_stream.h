#pragma once

#include "memory/audio_stream.h"  // IChunkSource / IChunkSink
#include "memory/byte_file.h"
#include "memory/wav.h"

#include <cstdint>
#include <cstring>

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
    // Device sample rate; the streaming path does no resampling, so a non-48k file would play at the
    // wrong pitch. Matches the rate wav.h writes into recorded headers.
    static constexpr uint32_t kPlaybackSampleRate = 48000;
    static constexpr uint32_t kMaxChunks = 64;   // scan bound: refuse a pathological chunk list rather than loop

    // `f` must be an open file positioned at 0. Returns false on a missing/invalid/unsupported header.
    //
    // Spec-compliant chunk walk. After the 12-byte RIFF/WAVE header, the file is a list of chunks, each
    // `<4-byte id><LE32 size><body>` plus a single pad byte when `size` is odd. We step that list,
    // capturing `fmt ` and stopping at `data`, and SKIP every chunk we don't recognise (`fact`, `LIST`,
    // `JUNK`, `bext`, `cue `, ...) by its size - so `data` is found no matter how much metadata precedes
    // it, exactly as a conformant reader must. We walk via seek/read instead of a fixed buffer, so there
    // is no offset ceiling (the old 256-byte window mis-handled externally-authored files whose metadata
    // pushed `data` further in). The mono/float32/48k checks at `data` are NOT WAV-spec strictness - they
    // are engine-capability gates: the streaming path hands raw body bytes straight to the engine's float
    // frames with no conversion, so a non-native file would be reinterpreted as garbage. A reject here
    // becomes the deck's error flash (via start_play), not a mis-play. kWav* track the build (float32
    // default, int16 under LOFI_INT16).
    bool begin(IByteFile* f) {
        _f = f; _remaining = 0; _data_start = 0; _data_size = 0;

        uint8_t riff[12];
        if (f->read(riff, sizeof(riff)) != sizeof(riff)) return false;
        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return false;

        uint16_t audioFormat = 0, channels = 0, bits = 0;
        uint32_t sampleRate = 0;
        bool haveFmt = false;

        uint32_t pos = 12;                               // first chunk begins right after RIFF/WAVE
        for (uint32_t guard = 0; guard < kMaxChunks; guard++) {
            uint8_t ch[8];
            if (!f->seek(pos)) return false;
            if (f->read(ch, sizeof(ch)) != sizeof(ch)) return false;  // ran off the end before `data`
            const uint32_t size = read_val<uint32_t>(ch, 4);
            const uint32_t body = pos + 8;

            if (std::memcmp(ch, "fmt ", 4) == 0) {
                if (size < 16) return false;             // malformed: WAVEFORMAT is at least 16 bytes
                uint8_t fb[16];
                if (f->read(fb, sizeof(fb)) != sizeof(fb)) return false;
                audioFormat = read_val<uint16_t>(fb, 0);
                channels    = read_val<uint16_t>(fb, 2);
                sampleRate  = read_val<uint32_t>(fb, 4);
                bits        = read_val<uint16_t>(fb, 14);
                // WAVE_FORMAT_EXTENSIBLE: the real format tag is the first 2 bytes of the SubFormat GUID
                // (at body+24, past cbSize(2)+wValidBitsPerSample(2)+dwChannelMask(4)).
                if (audioFormat == 0xFFFE && size >= 40) {
                    uint8_t tag[2];
                    if (!f->seek(body + 24) || f->read(tag, sizeof(tag)) != sizeof(tag)) return false;
                    audioFormat = read_val<uint16_t>(tag, 0);
                }
                haveFmt = true;
            } else if (std::memcmp(ch, "data", 4) == 0) {
                if (!haveFmt)                           return false;  // `fmt ` must precede `data`
                if (audioFormat != kWavAudioFormat)     return false;  // engine-capability gates (below)
                if (bits        != kWavBitsPerSample)   return false;
                if (channels    != 1)                   return false;
                if (sampleRate  != kPlaybackSampleRate) return false;
                if (!f->seek(body)) return false;
                _data_start = body;
                _data_size  = size;
                _remaining  = size;
                return true;
            }
            pos = body + size + (size & 1u);             // next chunk; chunks are word-aligned
        }
        return false;                                    // no `data` within kMaxChunks
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
