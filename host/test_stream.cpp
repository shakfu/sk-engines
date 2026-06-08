// Host test for the SD-streaming core (no FatFs, no hardware): the lock-free SpscRing and the
// PlayStream/RecordStream state machines, driven against memory-backed fake source/sink at the audio
// block rate. Proves the hard part - wrap correctness, read-ahead, underrun/overrun policy, EOF and
// finalize - off-target, before any of it touches the Card/FatFs layer. Build: `make -C host test-stream`.

#include "memory/spsc_ring.h"
#include "memory/audio_stream.h"
#include "memory/byte_file.h"
#include "memory/wav_stream.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

// Deterministic, order-sensitive byte pattern: any reorder/drop/dup shows up in a full-vector compare.
std::vector<uint8_t> ramp(uint32_t n) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = static_cast<uint8_t>(i * 31u + 7u);
    return v;
}

// A slow file body on "SD": hands out at most max_per_read bytes per read (simulates chunked FatFs).
struct MemSource : IChunkSource {
    std::vector<uint8_t> data;
    uint32_t pos = 0;
    uint32_t max_per_read;
    explicit MemSource(std::vector<uint8_t> d, uint32_t mpr = 0xffffffffu)
        : data(std::move(d)), max_per_read(mpr) {}
    uint32_t read(uint8_t* dst, uint32_t n) override {
        uint32_t avail = static_cast<uint32_t>(data.size()) - pos;
        uint32_t k = std::min(std::min(n, avail), max_per_read);
        std::memcpy(dst, data.data() + pos, k);
        pos += k;
        return k;
    }
    bool eof() const override { return pos >= data.size(); }
};

// A file being written on "SD": collects everything and records the finalize() call.
struct MemSink : IChunkSink {
    std::vector<uint8_t> data;
    bool finalized = false;
    uint32_t write(const uint8_t* src, uint32_t n) override {
        data.insert(data.end(), src, src + n);
        return n;
    }
    void finalize() override { finalized = true; }
};

// A seekable in-memory file standing in for an SD file (FatFs on device): grows on write, like a fresh
// FA_CREATE_ALWAYS file, so the streaming-WAV placeholder-then-patch path works.
struct MemFile : IByteFile {
    std::vector<uint8_t> buf;
    uint32_t cur = 0;
    uint32_t read(void* dst, uint32_t n) override {
        uint32_t avail = (cur < buf.size()) ? static_cast<uint32_t>(buf.size()) - cur : 0;
        uint32_t k = std::min(n, avail);
        std::memcpy(dst, buf.data() + cur, k);
        cur += k;
        return k;
    }
    uint32_t write(const void* src, uint32_t n) override {
        if (cur + n > buf.size()) buf.resize(cur + n);
        std::memcpy(buf.data() + cur, src, n);
        cur += n;
        return n;
    }
    bool seek(uint32_t pos) override { cur = pos; return true; }
};

constexpr uint32_t kBlock = 384; // 96 stereo int16 frames - the platform audio block, in bytes

} // namespace

int main() {
    // --- 1. SpscRing: wrap correctness + accounting under many small interleaved transfers ----------
    {
        uint8_t buf[16];
        SpscRing r; r.init(buf, sizeof(buf));   // tiny power-of-two cap forces frequent wraps
        check(r.capacity() == 16, "ring reports its capacity");
        check(r.writable() == 16 && r.readable() == 0, "fresh ring is empty / fully writable");

        const auto src = ramp(1000);
        std::vector<uint8_t> out;
        uint32_t written = 0, guard = 0;
        while (out.size() < src.size() && guard++ < 100000) {
            written += r.write(src.data() + written, std::min<uint32_t>(7, (uint32_t)src.size() - written));
            uint8_t tmp[5];
            uint32_t got = r.read(tmp, 5);
            out.insert(out.end(), tmp, tmp + got);
        }
        for (uint8_t tmp[5]; r.readable(); ) { uint32_t g = r.read(tmp, 5); out.insert(out.end(), tmp, tmp + g); }
        check(out.size() == src.size() && out == src, "ring delivers bytes in order across wraps (no drop/dup)");

        // full/empty edges
        SpscRing r2; uint8_t b2[8]; r2.init(b2, 8);
        check(r2.write(src.data(), 100) == 8, "write into empty ring caps at capacity");
        check(r2.writable() == 0 && r2.readable() == 8, "ring reports full");
        uint8_t sink8[8];
        check(r2.read(sink8, 100) == 8, "read from full ring caps at what's available");
        check(r2.readable() == 0, "ring empty after draining");
    }

    // --- 2. PlayStream happy path: no underrun, byte-exact, EOF finish ------------------------------
    {
        const auto file = ramp(19 * kBlock + 137);   // not block-aligned -> exercises the EOF tail
        MemSource src(file, 200);                     // SD hands out <=200 bytes/read
        uint8_t ringbuf[4096]; SpscRing ring; ring.init(ringbuf, sizeof(ringbuf));
        uint8_t scratch[512];
        PlayStream play; play.init(&ring, scratch, sizeof(scratch));
        play.start(&src);

        std::vector<uint8_t> out;
        uint8_t blk[kBlock];
        uint32_t guard = 0;
        while (!play.finished() && guard++ < 100000) {
            play.pump();                     // main loop reads ahead
            uint32_t got = play.consume(blk, kBlock);   // ISR drains a block
            out.insert(out.end(), blk, blk + got);
        }
        check(out.size() == file.size() && out == file, "play delivers the whole file, byte-exact, in order");
        check(play.underruns() == 0, "play has zero underruns when the pump keeps up");
        check(play.finished(), "play reports finished at EOF + drained");
    }

    // --- 3. PlayStream underrun: pump starved -> silence + counted (not a false 'finish') -----------
    {
        const auto file = ramp(8 * kBlock);
        MemSource src(file);
        uint8_t ringbuf[256]; SpscRing ring; ring.init(ringbuf, sizeof(ringbuf));
        uint8_t scratch[128];
        PlayStream play; play.init(&ring, scratch, sizeof(scratch));
        play.start(&src);
        play.pump();                          // fill the tiny ring once, then starve it

        uint8_t blk[kBlock];
        uint32_t got1 = play.consume(blk, kBlock);   // drains 256, 128 short -> silence + underrun
        bool tail_silent = true;
        for (uint32_t i = got1; i < kBlock; i++) if (blk[i] != 0) tail_silent = false;
        uint32_t got2 = play.consume(blk, kBlock);   // nothing left -> all silence + underrun
        check(play.underruns() >= 2, "starved play counts underruns");
        check(tail_silent && got2 == 0, "underrun shortfall is zero-filled (silence), not stale data");
        check(!play.finished(), "underrun is not mistaken for end-of-stream (file not at EOF)");
    }

    // --- 4. RecordStream happy path: byte-exact to sink, flush + finalize on stop -------------------
    {
        const auto take = ramp(23 * kBlock + 51);
        MemSink sink;
        uint8_t ringbuf[4096]; SpscRing ring; ring.init(ringbuf, sizeof(ringbuf));
        uint8_t scratch[512];
        RecordStream rec; rec.init(&ring, scratch, sizeof(scratch));
        rec.start(&sink);

        uint32_t pi = 0, guard = 0;
        while (pi < take.size()) {
            uint32_t chunk = std::min<uint32_t>(kBlock, (uint32_t)take.size() - pi);
            rec.produce(take.data() + pi, chunk);   // ISR pushes a block of input
            pi += chunk;
            rec.pump();                              // main loop drains to SD
        }
        rec.stop();
        while (!rec.finished() && guard++ < 100000) rec.pump();   // flush remaining, finalize
        check(sink.data.size() == take.size() && sink.data == take, "record writes the whole take, byte-exact");
        check(rec.overruns() == 0, "record has zero overruns when the pump keeps up");
        check(sink.finalized, "record finalizes the sink on stop");
    }

    // --- 5. RecordStream overrun: pump starved -> excess dropped + counted, never blocks ------------
    {
        MemSink sink;
        uint8_t ringbuf[256]; SpscRing ring; ring.init(ringbuf, sizeof(ringbuf));
        uint8_t scratch[128];
        RecordStream rec; rec.init(&ring, scratch, sizeof(scratch));
        rec.start(&sink);
        const auto blkdata = ramp(kBlock);
        rec.produce(blkdata.data(), kBlock);   // 256 fit, 128 dropped
        rec.produce(blkdata.data(), kBlock);   // 0 fit, 384 dropped
        check(rec.overruns() == (kBlock - 256) + kBlock, "record drops and counts exactly the overflow bytes");
    }

    // --- 6. Round trip: record a take to SD, then stream it back -> identical -----------------------
    {
        const auto take = ramp(31 * kBlock + 200);
        // record
        MemSink sink;
        uint8_t rb1[2048]; SpscRing ring1; ring1.init(rb1, sizeof(rb1));
        uint8_t sc1[256];
        RecordStream rec; rec.init(&ring1, sc1, sizeof(sc1)); rec.start(&sink);
        for (uint32_t pi = 0; pi < take.size(); ) {
            uint32_t c = std::min<uint32_t>(kBlock, (uint32_t)take.size() - pi);
            rec.produce(take.data() + pi, c); pi += c; rec.pump();
        }
        rec.stop(); while (!rec.finished()) rec.pump();
        // play back the recorded file
        MemSource src(sink.data, 173);
        uint8_t rb2[2048]; SpscRing ring2; ring2.init(rb2, sizeof(rb2));
        uint8_t sc2[256];
        PlayStream play; play.init(&ring2, sc2, sizeof(sc2)); play.start(&src);
        std::vector<uint8_t> out; uint8_t blk[kBlock]; uint32_t guard = 0;
        while (!play.finished() && guard++ < 100000) { play.pump(); uint32_t g = play.consume(blk, kBlock); out.insert(out.end(), blk, blk + g); }
        check(out == take, "round trip (record -> SD -> stream back) reproduces the input exactly");
    }

    // --- 7. Streaming WAV codec: placeholder header -> stream body -> patch on finalize -------------
    {
        const auto body = ramp(17 * kBlock + 99);
        MemFile file;
        WavStreamWriter w;
        check(w.begin(&file), "WAV writer emits a placeholder header");
        for (uint32_t pi = 0; pi < body.size(); ) {       // stream the body in blocks
            uint32_t c = std::min<uint32_t>(kBlock, (uint32_t)body.size() - pi);
            w.write(body.data() + pi, c); pi += c;
        }
        w.finalize();
        check(file.buf.size() == 44u + body.size(), "streamed WAV = 44-byte header + body");
        WavHeader h; size_t hs = 0;
        bool parsed = wav_header(file.buf.data(), (uint32_t)file.buf.size(), h, hs);
        check(parsed && hs == 44 && h.DataSize == body.size(),
              "finalized header patches DataSize / body offset correctly");

        file.cur = 0;
        WavStreamReader r;
        check(r.begin(&file), "WAV reader parses the streamed header");
        std::vector<uint8_t> out; uint8_t blk[kBlock];
        for (uint32_t guard = 0; !r.eof() && guard < 100000; guard++) {
            uint32_t g = r.read(blk, kBlock);
            out.insert(out.end(), blk, blk + g);
            if (g == 0) break;
        }
        check(out == body, "WAV reader reproduces the body byte-exact (stops at DataSize)");
    }

    // --- 8. Full stack end-to-end: ISR -> ring -> WAV file -> ring -> ISR ---------------------------
    // The whole step-1 + step-2 path minus FatFs: record through the ring into a streamed WAV, then
    // play that WAV back through the ring. This is what the device must reproduce once Card backs IByteFile.
    {
        const auto take = ramp(29 * kBlock + 211);
        MemFile file;
        // record: produce() -> RecordStream -> WavStreamWriter -> MemFile
        WavStreamWriter w; w.begin(&file);
        uint8_t rb1[2048]; SpscRing ring1; ring1.init(rb1, sizeof(rb1));
        uint8_t sc1[256];
        RecordStream rec; rec.init(&ring1, sc1, sizeof(sc1)); rec.start(&w);
        for (uint32_t pi = 0; pi < take.size(); ) {
            uint32_t c = std::min<uint32_t>(kBlock, (uint32_t)take.size() - pi);
            rec.produce(take.data() + pi, c); pi += c; rec.pump();
        }
        rec.stop(); for (uint32_t g = 0; !rec.finished() && g < 100000; g++) rec.pump();

        // play: MemFile -> WavStreamReader -> PlayStream -> consume()
        file.cur = 0;
        WavStreamReader r; check(r.begin(&file), "end-to-end: reader opens the just-recorded WAV");
        uint8_t rb2[2048]; SpscRing ring2; ring2.init(rb2, sizeof(rb2));
        uint8_t sc2[256];
        PlayStream play; play.init(&ring2, sc2, sizeof(sc2)); play.start(&r);
        std::vector<uint8_t> out; uint8_t blk[kBlock];
        for (uint32_t g = 0; !play.finished() && g < 100000; g++) {
            play.pump(); uint32_t got = play.consume(blk, kBlock); out.insert(out.end(), blk, blk + got);
        }
        check(out == take, "end-to-end record->WAV->play reproduces the input exactly");
        check(play.underruns() == 0, "end-to-end playback has no underruns");
    }

    // --- 9. Looping: PlayStream rewinds the WAV at EOF and repeats the body seamlessly --------------
    {
        const auto body = ramp(5 * kBlock + 137);
        MemFile file;
        WavStreamWriter w; w.begin(&file);
        for (uint32_t pi = 0; pi < body.size(); ) {
            uint32_t c = std::min<uint32_t>(kBlock, (uint32_t)body.size() - pi);
            w.write(body.data() + pi, c); pi += c;
        }
        w.finalize();

        file.cur = 0;
        WavStreamReader r; check(r.begin(&file), "loop: reader opens the WAV");
        check(r.data_bytes() == body.size(), "loop: reader reports body length for loop sizing");

        uint8_t rb[2048]; SpscRing ring; ring.init(rb, sizeof(rb));
        uint8_t sc[256];
        PlayStream play; play.init(&ring, sc, sizeof(sc)); play.start(&r);
        play.set_loop(true);

        // Read ~2.5 loops' worth; it must equal the body tiled (rewind wraps cleanly) and never finish.
        const uint32_t want = body.size() * 2 + body.size() / 2;
        std::vector<uint8_t> out; uint8_t blk[kBlock]; uint32_t guard = 0;
        while (out.size() < want && guard++ < 100000) {
            play.pump(); uint32_t g = play.consume(blk, kBlock); out.insert(out.end(), blk, blk + g);
        }
        check(!play.finished(), "loop: a looping stream never reports finished");
        bool tiled_ok = out.size() >= want;
        for (uint32_t i = 0; i < want && tiled_ok; i++) if (out[i] != body[i % body.size()]) tiled_ok = false;
        check(tiled_ok, "loop: playback repeats the body seamlessly across the rewind");
    }

    if (g_failures == 0) { std::printf("OK: all stream checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
