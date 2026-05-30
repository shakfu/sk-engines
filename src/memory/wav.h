#pragma once

//https://en.wikipedia.org/wiki/WAV

#include <cstdint>
#include <cstddef>
#include <cstring>

struct WavHeader {
  // Master RIFF chunk
  uint8_t FileTypeBlocID[4] = {'R', 'I', 'F', 'F'};
  uint32_t size;  // RIFF chunk size: a 32-bit field per the WAV spec (was size_t,
                  // which is 4 bytes only on the 32-bit target and breaks the
                  // 44-byte layout / static_assert on a 64-bit host).
  uint8_t FileFormatID[4] = {'W', 'A', 'V', 'E'};
  
  // Chunk describing the data format
  uint8_t FormatBlocID[4] = {'f', 'm', 't', ' '};
  uint32_t BlocSize = 16; // Fixed
  uint16_t AudioFormat;   // 1 = PCM integer. 3=IEEE754 float.
  uint16_t NbrChannels;
  uint32_t SampleRate;
  uint32_t BytePerSec;
  uint16_t BytePerBloc;
  uint16_t BitsPerSample;
  // Chunk containing the sampled data
  uint8_t DataBlocID[4] = {'d', 'a', 't', 'a'};
  uint32_t DataSize;
};

inline WavHeader wav_header(const size_t size) {
    WavHeader header;
    static_assert(sizeof(header) == 44, "");

    header.AudioFormat = 3;
    header.NbrChannels = 2;
    header.SampleRate = 48000;
    header.BytePerBloc = sizeof(float) * header.NbrChannels;
    header.BytePerSec = header.SampleRate * header.BytePerBloc;
    header.BitsPerSample = 32;

    header.DataSize = size;
    header.size = header.DataSize + sizeof(header) - 8;

    return header;
};

template <typename T>
inline T read_val(const uint8_t* data, size_t offset) 
{
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}
inline bool check_id(const uint8_t* data, size_t offset, const char* id) 
{
  return std::memcmp(data + offset, id, 4) == 0;
}

inline bool wav_header(const uint8_t* bytes, size_t size, WavHeader& header, size_t& header_size)
{
    size_t cursor = 0;
    
    // Need at least 12 bytes for RIFF header
    if (size < 12) return false;

    if (!check_id(bytes, cursor, "RIFF")) return false;
    
    std::memcpy(header.FileTypeBlocID, bytes + cursor, 4);
    cursor += 4;

    uint32_t riffChunkSize = read_val<uint32_t>(bytes, cursor);
    header.size = riffChunkSize; 
    cursor += 4;

    if (!check_id(bytes, cursor, "WAVE")) return false;
    
    std::memcpy(header.FileFormatID, bytes + cursor, 4);
    cursor += 4;
    
    bool foundFmt = false;
    bool foundData = false;

    while (cursor < size) {
        if (cursor + 8 > size) break;

        char chunkID[4];
        std::memcpy(chunkID, bytes + cursor, 4);
        cursor += 4;

        uint32_t chunkSize = read_val<uint32_t>(bytes, cursor);
        cursor += 4;

        if (std::memcmp(chunkID, "fmt ", 4) == 0) {
            std::memcpy(header.FormatBlocID, chunkID, 4);
            header.BlocSize = chunkSize;

            if (chunkSize < 16) return false;

            header.AudioFormat   = read_val<uint16_t>(bytes, cursor + 0);
            header.NbrChannels   = read_val<uint16_t>(bytes, cursor + 2);
            header.SampleRate    = read_val<uint32_t>(bytes, cursor + 4);
            header.BytePerSec    = read_val<uint32_t>(bytes, cursor + 8);
            header.BytePerBloc   = read_val<uint16_t>(bytes, cursor + 12);
            header.BitsPerSample = read_val<uint16_t>(bytes, cursor + 14);

            foundFmt = true;
        } 
        else if (std::memcmp(chunkID, "data", 4) == 0) {
            std::memcpy(header.DataBlocID, chunkID, 4);
            header.DataSize = chunkSize;
            header_size = cursor;
            foundData = true; 
        }

        if (foundFmt && foundData) return true;
        
        cursor += chunkSize;
        if (cursor % 2 != 0) cursor++;
        if (cursor > size) return false;
    }

    return false;
}
