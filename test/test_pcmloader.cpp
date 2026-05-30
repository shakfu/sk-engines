// Simulation tests for PcmLoader (src/core/pcm_loader.h) - the exact accounting that
// card.cpp's audio load runs, exercised here without FatFS. Verifies frame counts,
// truncation to capacity, termination, and correct sample placement, for every width combo
// and across many chunk sizes.
#include "pcm_loader.h"
#include "pcm_convert.h"
#include "check.h"
#include <vector>
#include <cstring>
#include <utility>

using namespace spotykach;

// Stream a simulated file of `file_frames` stereo frames (src_bps each) into a buffer of
// `cap_frames` capacity (dst_bps each), feeding `chunk` bytes per step like card.cpp.
// Returns frames loaded.
static size_t run_load(size_t file_frames, int src_bps, size_t cap_frames, int dst_bps,
                       size_t chunk, std::vector<uint8_t>* out_dst = nullptr,
                       std::vector<uint8_t>* out_file = nullptr) {
  std::vector<uint8_t> file(file_frames * 2 * src_bps);
  for (size_t i = 0; i < file.size(); i++) file[i] = (uint8_t)((i * 131 + 7) & 0xFF);
  std::vector<uint8_t> dst(cap_frames * 2 * dst_bps, 0);

  PcmLoader L;
  L.begin(file.size(), src_bps, dst.data(), dst.size(), dst_bps);

  size_t pos = 0;
  while (true) {
    size_t n = (file.size() - pos < chunk) ? (file.size() - pos) : chunk;
    bool full = L.feed(file.data() + pos, n);
    pos += n;
    if (n < chunk || full) break;  // short read (EOF) or buffer full - mirrors card.cpp
  }
  if (out_dst) *out_dst = std::move(dst);
  if (out_file) *out_file = std::move(file);
  return L.frames();
}

void run_pcmloader_tests() {
  std::printf("PcmLoader:\n");

  const size_t K = 32768;  // real card kChunk

  // Match (no conversion), both widths: load exactly the file when it fits.
  CHECK_EQ((long long)run_load(100000, 4, 200000, 4, K), 100000);
  CHECK_EQ((long long)run_load(50000, 2, 50000, 2, K), 50000);

  // Truncation to buffer capacity when the file is longer.
  CHECK_EQ((long long)run_load(300000, 4, 200000, 4, K), 200000);

  // Convert float -> int16 (legacy tape into a lo-fi build): same frame count, fits.
  CHECK_EQ((long long)run_load(100000, 4, 200000, 2, K), 100000);
  // ...and an 84s-ish int16 tape truncated into a 42s-ish float buffer.
  CHECK_EQ((long long)run_load(300000, 2, 200000, 4, K), 200000);

  // Convert int16 -> float (lo-fi tape into a stock build): exact fit.
  CHECK_EQ((long long)run_load(123456, 2, 123456, 4, K), 123456);

  // Small file (less than one chunk) loads fully, in both directions.
  CHECK_EQ((long long)run_load(10, 4, 1000, 2, K), 10);
  CHECK_EQ((long long)run_load(3, 2, 1000, 4, K), 3);

  // Robust across many small chunk sizes (more iterations, exact-fill edges).
  for (size_t chunk : {64u, 256u, 4096u}) {
    CHECK_EQ((long long)run_load(5000, 4, 5000, 2, chunk), 5000);   // exact fill, convert
    CHECK_EQ((long long)run_load(7000, 4, 5000, 2, chunk), 5000);   // truncate, convert
    CHECK_EQ((long long)run_load(5000, 4, 5000, 4, chunk), 5000);   // exact fill, match
    CHECK_EQ((long long)run_load(3000, 2, 5000, 4, chunk), 3000);   // short file, convert
  }

  // Placement / integrity: a converted load must equal a one-shot convert_pcm_block of the
  // whole file (proves chunked offset advancement writes contiguously, no gaps/overlap).
  {
    std::vector<uint8_t> dst, file;
    size_t frames = run_load(20000, 4, 20000, 2, 4096, &dst, &file);
    CHECK_EQ((long long)frames, 20000);

    std::vector<uint8_t> expect(20000 * 2 * 2);
    convert_pcm_block(file.data(), 20000 * 2, 4, expect.data(), 2);
    CHECK(std::memcmp(dst.data(), expect.data(), expect.size()) == 0);
  }
}
