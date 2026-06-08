// Compiled into the firmware only for the streaming `tape` engine (lives in the src/hw wildcard; the
// guard keeps every other engine byte-identical).
#if defined(SPK_ENGINE_TAPE)

#include "stream_deck.h"

// Size-optimize: main-loop SD glue, never the audio path.
#pragma GCC optimize("Os")

using namespace spotykach;

void StreamDeck::init(const Mem& m) {
    _play_ring.init(m.play_ring, m.play_ring_bytes);
    _record_ring.init(m.record_ring, m.record_ring_bytes);
    _play.init(&_play_ring, m.scratch, m.scratch_bytes);
    _record.init(&_record_ring, m.scratch, m.scratch_bytes);  // play XOR record -> shared scratch
}

bool StreamDeck::start_play(const char* path) {
    if (_mode.load(std::memory_order_acquire) != Mode::idle || _finalizing) return false;
    if (!_file.open_read(path)) return false;
    if (!_reader.begin(&_file)) { _file.close(); return false; }   // not a valid WAV
    _play.start(&_reader);
    _mode.store(Mode::play, std::memory_order_release);            // ISR may now consume
    return true;
}

bool StreamDeck::start_record(const char* path) {
    if (_mode.load(std::memory_order_acquire) != Mode::idle || _finalizing) return false;
    if (!_file.open_write(path)) return false;
    if (!_writer.begin(&_file)) { _file.close(); return false; }   // placeholder header failed
    _record.start(&_writer);
    _mode.store(Mode::record, std::memory_order_release);          // ISR may now produce
    return true;
}

void StreamDeck::stop() {
    const Mode m = _mode.load(std::memory_order_acquire);
    if (m == Mode::play) {
        _mode.store(Mode::idle, std::memory_order_release);        // ISR stops consuming
        _file.close();
    } else if (m == Mode::record) {
        _record.stop();                                           // request end-of-record (flush + patch)
        _mode.store(Mode::idle, std::memory_order_release);        // ISR stops producing immediately...
        _finalizing = true;                                       // ...main loop flushes the tail below
    }
}

void StreamDeck::process() {
    const Mode m = _mode.load(std::memory_order_acquire);
    if (m == Mode::play) {
        _play.pump();
        if (_play.finished()) {                                   // file reached EOF + drained
            _mode.store(Mode::idle, std::memory_order_release);
            _file.close();
        }
    } else if (m == Mode::record) {
        _record.pump();                                           // drain captured audio to SD
    }
    if (_finalizing) {
        _record.pump();                                           // flush the tail; finalize() patches header
        if (_record.finished()) { _file.close(); _finalizing = false; }
    }
}

uint32_t StreamDeck::play_consume(uint8_t* dst, uint32_t n) {
    if (_mode.load(std::memory_order_acquire) != Mode::play) return 0;
    return _play.consume(dst, n);   // zero-fills any shortfall (silence)
}

uint32_t StreamDeck::record_produce(const uint8_t* src, uint32_t n) {
    if (_mode.load(std::memory_order_acquire) != Mode::record) return 0;
    return _record.produce(src, n);
}

#endif // SPK_ENGINE_TAPE
