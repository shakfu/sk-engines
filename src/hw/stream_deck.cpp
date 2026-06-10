// Compiled into the firmware only for SPK_USE_STREAM engines (tape, shuttle) - lives in the src/hw
// wildcard; the guard keeps every non-streaming engine byte-identical.
#if defined(SPK_USE_STREAM)

#include "stream_deck.h"

// Size-optimize: main-loop SD glue, never the audio path.
#pragma GCC optimize("Os")

using namespace spotykach;

void StreamDeck::init(const Mem& m) {
    _d[0].ring.init(m.ring_a, m.ring_a_bytes);
    _d[1].ring.init(m.ring_b, m.ring_b_bytes);
    for (auto& d : _d) {
        d.play.init(&d.ring, m.scratch, m.scratch_bytes);     // play XOR record per deck -> one ring,
        d.record.init(&d.ring, m.scratch, m.scratch_bytes);   // and one scratch shared across decks
    }
}

bool StreamDeck::start_play(DeckRef::Ref deck, const char* path) {
    Deck& d = _d[deck];
    if (d.mode.load(std::memory_order_acquire) != Mode::idle || d.finalizing) return false;
    if (!d.file.open_read(path)) return false;
    if (!d.reader.begin(&d.file)) { d.file.close(); return false; }   // not a valid WAV
    d.play.start(&d.reader);
    d.mode.store(Mode::play, std::memory_order_release);             // ISR may now consume
    return true;
}

bool StreamDeck::start_record(DeckRef::Ref deck, const char* path) {
    Deck& d = _d[deck];
    if (d.mode.load(std::memory_order_acquire) != Mode::idle || d.finalizing) return false;
    if (!d.file.open_write(path)) return false;
    if (!d.writer.begin(&d.file, 1)) { d.file.close(); return false; }  // mono header; placeholder failed
    d.record.start(&d.writer);
    d.mode.store(Mode::record, std::memory_order_release);          // ISR may now produce
    return true;
}

void StreamDeck::stop(DeckRef::Ref deck) {
    Deck& d = _d[deck];
    const Mode m = d.mode.load(std::memory_order_acquire);
    if (m == Mode::play) {
        d.mode.store(Mode::idle, std::memory_order_release);       // ISR stops consuming
        d.file.close();
    } else if (m == Mode::record) {
        d.record.stop();                                          // request end-of-record (flush + patch)
        d.mode.store(Mode::idle, std::memory_order_release);       // ISR stops producing immediately...
        d.finalizing = true;                                      // ...main loop flushes the tail below
    }
}

void StreamDeck::set_loop(DeckRef::Ref deck, bool loop) {
    _d[deck].play.set_loop(loop);   // honored on the next play EOF (pump rewinds instead of finishing)
}

uint32_t StreamDeck::loop_frames(DeckRef::Ref deck) const {
    const Deck& d = _d[deck];
    if (d.mode.load(std::memory_order_acquire) != Mode::play) return 0;
    return d.reader.data_bytes() / static_cast<uint32_t>(sizeof(float));  // mono float: 4 bytes/frame
}

bool StreamDeck::exists(const char* path) const {
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK;   // f_stat uses no FIL handle - safe alongside an open deck file
}

void StreamDeck::process() {
    for (auto& d : _d) _pump(d);   // service each deck's slow SD I/O sequentially (shared scratch)
}

void StreamDeck::_pump(Deck& d) {
    const Mode m = d.mode.load(std::memory_order_acquire);
    if (m == Mode::play) {
        d.play.pump();
        if (d.play.finished()) {                                  // file reached EOF + drained
            d.mode.store(Mode::idle, std::memory_order_release);
            d.file.close();
        }
    } else if (m == Mode::record) {
        d.record.pump();                                          // drain captured audio to SD
    }
    if (d.finalizing) {
        d.record.pump();                                          // flush the tail; finalize() patches header
        if (d.record.finished()) { d.file.close(); d.finalizing = false; }
    }
}

uint32_t StreamDeck::play_consume(DeckRef::Ref deck, uint8_t* dst, uint32_t n) {
    Deck& d = _d[deck];
    if (d.mode.load(std::memory_order_acquire) != Mode::play) return 0;
    return d.play.consume(dst, n);   // zero-fills any shortfall (silence)
}

uint32_t StreamDeck::record_produce(DeckRef::Ref deck, const uint8_t* src, uint32_t n) {
    Deck& d = _d[deck];
    if (d.mode.load(std::memory_order_acquire) != Mode::record) return 0;
    return d.record.produce(src, n);
}

#endif // SPK_USE_STREAM
