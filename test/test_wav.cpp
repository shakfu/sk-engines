// Host tests for the WAV header builder/parser in src/memory/wav.h.
// Locks the canonical 44-byte layout and a build->serialize->parse round-trip.
#include <cstdint>
#include <cstring>
#include "wav.h"
#include "check.h"

void run_wav_tests() {
  std::printf("WAV:\n");

  // Layout invariant relied on by the SD writer (writes sizeof(WavHeader) bytes).
  CHECK_EQ((int)sizeof(WavHeader), 44);

  // Builder: a 1000-byte data chunk produces a 48 kHz / stereo / 32-bit float header.
  {
    WavHeader h = wav_header((size_t)1000);
    CHECK(std::memcmp(h.FileTypeBlocID, "RIFF", 4) == 0);
    CHECK(std::memcmp(h.FileFormatID, "WAVE", 4) == 0);
    CHECK(std::memcmp(h.FormatBlocID, "fmt ", 4) == 0);
    CHECK(std::memcmp(h.DataBlocID, "data", 4) == 0);
    CHECK_EQ((int)h.AudioFormat, 3);     // IEEE-754 float
    CHECK_EQ((int)h.NbrChannels, 2);
    CHECK_EQ((int)h.SampleRate, 48000);
    CHECK_EQ((int)h.BitsPerSample, 32);
    CHECK_EQ((int)h.BytePerBloc, 8);     // 4 bytes * 2 ch
    CHECK_EQ((int)h.BytePerSec, 384000); // 48000 * 8
    CHECK_EQ((int)h.DataSize, 1000);
    CHECK_EQ((int)h.size, 1000 + 44 - 8);  // RIFF size = data + header - 8
  }

  // Round-trip: the struct is a canonical little-endian WAV header, so writing it
  // out as bytes and parsing them back must recover the same fields.
  {
    WavHeader h = wav_header((size_t)2048);
    uint8_t bytes[44];
    std::memcpy(bytes, &h, sizeof(h));

    WavHeader parsed;
    size_t header_size = 0;
    CHECK(wav_header(bytes, sizeof(bytes), parsed, header_size));
    CHECK_EQ((int)header_size, 44);
    CHECK_EQ((int)parsed.AudioFormat, 3);
    CHECK_EQ((int)parsed.NbrChannels, 2);
    CHECK_EQ((int)parsed.SampleRate, 48000);
    CHECK_EQ((int)parsed.BitsPerSample, 32);
    CHECK_EQ((int)parsed.DataSize, 2048);
  }

  // Rejection: too-short input and a non-RIFF buffer must both fail cleanly.
  {
    WavHeader h = wav_header((size_t)512);
    uint8_t bytes[44];
    std::memcpy(bytes, &h, sizeof(h));

    WavHeader out;
    size_t hs = 0;
    CHECK(!wav_header(bytes, 8, out, hs));   // < 12 bytes -> false

    uint8_t garbage[44] = {0};
    CHECK(!wav_header(garbage, sizeof(garbage), out, hs));  // no "RIFF"
  }

  // Format compatibility: the parser reads whatever depth the file declares, so it can
  // distinguish a 16-bit PCM file from a 32-bit float one on load (LOFI_INT16 migration).
  {
    // Start from a valid header, patch the fmt fields to 16-bit PCM, parse it back.
    WavHeader h = wav_header((size_t)4096);
    h.AudioFormat = 1;       // PCM integer
    h.BitsPerSample = 16;
    h.BytePerBloc = 4;       // 2 bytes * 2 channels
    h.BytePerSec = 48000 * 4;
    uint8_t bytes[44];
    std::memcpy(bytes, &h, sizeof(h));

    WavHeader parsed;
    size_t hs = 0;
    CHECK(wav_header(bytes, sizeof(bytes), parsed, hs));
    CHECK_EQ((int)parsed.AudioFormat, 1);
    CHECK_EQ((int)parsed.BitsPerSample, 16);
    CHECK_EQ((int)parsed.DataSize, 4096);
  }
}
