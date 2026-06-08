#pragma once

#include "engine/istreamdeck.h"
#include "memory/spsc_ring.h"
#include "memory/audio_stream.h"
#include "memory/wav_stream.h"
#include "hw/fat_file.h"

#include <atomic>
#include <cstdint>

namespace spotykach {

// Platform streaming service: bridges the audio ISR (lock-free rings) and FatFs (main-loop SD I/O) for
// the `tape` engine. One file at a time - play XOR record - so the two streams share one scratch buffer.
// Control (start_*/stop) runs in the main loop; the ISR only calls play_consume/record_produce.
class StreamDeck : public IStreamDeck {
public:
    struct Mem {
        uint8_t* play_ring;     uint32_t play_ring_bytes;     // power-of-two SDRAM buffers
        uint8_t* record_ring;   uint32_t record_ring_bytes;
        uint8_t* scratch;       uint32_t scratch_bytes;       // f_read/f_write staging
    };
    void init(const Mem& m);

    // Main-loop pump (AppImpl::Loop): does the slow SD I/O for whichever stream is active/finalizing.
    void process();

    // --- IStreamDeck ------------------------------------------------------------------------------
    uint32_t play_consume(uint8_t* dst, uint32_t n) override;       // ISR
    uint32_t record_produce(const uint8_t* src, uint32_t n) override; // ISR
    bool is_playing()   const override { return _mode.load(std::memory_order_acquire) == Mode::play; }
    bool is_recording() const override { return _mode.load(std::memory_order_acquire) == Mode::record; }
    bool start_play(const char* path)   override;   // main loop
    bool start_record(const char* path) override;   // main loop
    void stop()                         override;   // main loop

private:
    enum class Mode : uint8_t { idle, play, record };
    std::atomic<Mode> _mode{Mode::idle};
    bool _finalizing = false;     // record stopped; main loop is flushing the tail + finalizing the WAV

    SpscRing        _play_ring, _record_ring;
    PlayStream      _play;
    RecordStream    _record;
    WavStreamReader _reader;
    WavStreamWriter _writer;
    FatFile         _file;        // one file handle (play XOR record)
};

} // namespace spotykach
