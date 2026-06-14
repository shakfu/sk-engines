#pragma once

#include "memory/audio_stream.h"  // IChunkSource
#include "memory/byte_file.h"

#include <cstdint>

namespace spotykach {

// Streaming reader for a HEADERLESS raw PCM body - the RadioMusic ".raw" format: signed 16-bit mono,
// little-endian, at the device sample rate (48 kHz). There is no header to parse, so the whole file IS
// the body: the length in frames is filesize / 2, and seeking to frame f is a byte seek to f * 2. It
// presents the file to PlayStream as a plain byte IChunkSource (the engine converts the int16 body to
// float in the audio ISR). Endianness: the stream is byte-oriented and both the device (STM32) and the
// host are little-endian, matching the .raw layout, so the bytes pass straight through with no swap.
//
// Sibling of WavStreamReader (wav_stream.h): same IChunkSource role, but no header/format gate (the
// .raw convention IS the format) and an extra seek_to_frame() the radio's free-running playhead uses to
// jump a freshly-opened station to its virtual position before streaming forward.
class RawStreamReader : public IChunkSource {
public:
    static constexpr uint32_t kBytesPerFrame = 2;  // signed 16-bit, mono

    // `f` must be an open file; `filesize` is its total byte length (from f_stat). Always succeeds for a
    // non-empty file - there is no format to validate. Leaves the read position wherever the caller next
    // seeks it (start_play_raw seeks to the computed offset immediately after begin()).
    bool begin(IByteFile* f, uint32_t filesize) {
        _f = f;
        _data_size = filesize & ~(kBytesPerFrame - 1u);  // floor to a whole frame
        _remaining = _data_size;
        return _f != nullptr && _data_size >= kBytesPerFrame;
    }

    uint32_t read(uint8_t* dst, uint32_t n) override {
        if (n > _remaining) n = _remaining;
        const uint32_t got = _f->read(dst, n);
        _remaining -= got;
        return got;
    }
    bool eof() const override { return _remaining == 0; }

    // Loop support: seek back to the body start (frame 0) so a looping station repeats seamlessly.
    void rewind() override { if (_f && _f->seek(0)) _remaining = _data_size; }

    // Seek to source frame `frame` (clamped into the file); subsequent reads continue from there. The
    // radio's free-running playhead calls this on a station change before streaming forward, so the file
    // resumes at (clock + start) mod length and sounds as though it kept broadcasting.
    bool seek_to_frame(uint32_t frame) {
        uint32_t byte = frame * kBytesPerFrame;
        if (byte > _data_size) byte = _data_size;         // clamp; an empty tail just reads as eof
        if (!_f || !_f->seek(byte)) return false;
        _remaining = _data_size - byte;
        return true;
    }

    uint32_t frames()     const { return _data_size / kBytesPerFrame; }
    uint32_t data_bytes() const { return _data_size; }    // whole-frame-floored file length, in bytes

private:
    IByteFile* _f = nullptr;
    uint32_t   _remaining = 0;   // body bytes not yet read
    uint32_t   _data_size = 0;   // whole-frame-floored file length, in bytes
};

} // namespace spotykach
