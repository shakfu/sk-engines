#pragma once
// Pure accounting for streaming an audio file body into the loop buffer, converting
// between sample widths when the file depth differs from the buffer storage. The FatFS I/O
// stays in the caller (card.cpp); this holds the size/offset/termination math so the exact
// logic that ships can be host-tested.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include "pcm_convert.h"

namespace spotykach {

class PcmLoader {
public:
    // file_data_bytes: WAV DataSize. file_bps / dst_bps: 2 (int16) or 4 (float).
    // dst + dst_capacity_bytes: the destination loop buffer. Loads min(file, capacity).
    void begin(size_t file_data_bytes, int file_bps,
               uint8_t* dst, size_t dst_capacity_bytes, int dst_bps)
    {
        _file_bps = file_bps;
        _dst_bps  = dst_bps;
        _convert  = (file_bps != dst_bps);
        _dst      = dst;

        auto file_frames = file_data_bytes / (2u * (size_t)file_bps);
        auto cap_frames  = dst_capacity_bytes / (2u * (size_t)dst_bps);
        _size   = std::min(file_frames, cap_frames) * 2u * (size_t)dst_bps;
        _offset = 0;
    }

    // Consume one chunk of file bytes (already read into `chunk`). Writes converted/copied
    // samples to the destination at the current offset, up to remaining capacity. Returns
    // true once the destination is full (the caller also stops on end-of-file).
    bool feed(const uint8_t* chunk, size_t chunk_bytes)
    {
        if (_convert) {
            auto avail = chunk_bytes / (size_t)_file_bps;       // source samples in chunk
            auto room  = (_size - _offset) / (size_t)_dst_bps;  // dest samples that fit
            auto n     = std::min(avail, room);
            convert_pcm_block(chunk, n, _file_bps, _dst + _offset, _dst_bps);
            _offset += n * (size_t)_dst_bps;
        }
        else {
            auto len = std::min(_size - _offset, chunk_bytes);
            std::memcpy(_dst + _offset, chunk, len);
            _offset += len;
        }
        return _offset >= _size;
    }

    size_t offset() const { return _offset; }
    size_t size_bytes() const { return _size; }
    size_t frames() const { return _dst_bps ? _offset / (2u * (size_t)_dst_bps) : 0; }

private:
    uint8_t* _dst     = nullptr;
    size_t   _size    = 0;
    size_t   _offset  = 0;
    int      _file_bps = 4;
    int      _dst_bps  = 4;
    bool     _convert  = false;
};

}  // namespace spotykach
