#pragma once

#include <cstdint>

namespace spotykach {

// Minimal random-access byte file, abstracted so the streaming-WAV codec (wav_stream.h) is independent
// of FatFs and host-testable. On device a `FatFile` wraps a FatFs `FIL`; in `host/` a `MemFile` wraps a
// vector. The concrete type owns open/close + path/handle lifetime; this interface is just the I/O the
// codec needs. All counts are bytes; `seek` is absolute from the start of the file.
struct IByteFile {
    virtual ~IByteFile() = default;
    virtual uint32_t read(void* dst, uint32_t n) = 0;        // bytes read (< n at EOF)
    virtual uint32_t write(const void* src, uint32_t n) = 0; // bytes written (< n only on a device error)
    virtual bool     seek(uint32_t pos) = 0;                 // absolute byte offset
};

} // namespace spotykach
