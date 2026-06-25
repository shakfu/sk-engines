// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Full-MIDI plumbing for the ChucK engine: a lock-free ring of raw 3-byte MIDI messages handed from the
// main loop (the UI MIDI poll -> ChuckEngine::handle_midi_message) to the audio ISR, which drains them
// in process() and feeds each into ChucK's re-introduced MidiIn device via MidiInManager::inject() right
// before ck->run(). Unlike the earlier NoteOn-only path (which reused csound_midi.h's note ring), this
// carries the whole channel-voice vocabulary - real velocity, NoteOff, CC, pitch-bend, aftertouch,
// program change - plus system realtime (clock/start/stop), so a patch using `MidiIn min; min.recv(msg);`
// sees exactly what a desktop ChucK patch would. See docs/dev/chuck-midi-in-porting.md.

#include <atomic>
#include <cstdint>

namespace spotykach {

// One raw MIDI message. `status` includes the channel nibble for channel-voice messages, or is a system
// realtime status byte (0xF8/0xFA/0xFB/0xFC). data1/data2 are the message data bytes (0 where unused,
// e.g. the second byte of a 2-byte program-change / channel-pressure / realtime message).
struct MidiMessage { uint8_t status; uint8_t data1; uint8_t data2; };

// Lock-free single-producer / single-consumer ring. Producer = the main loop (handle_midi_message);
// consumer = the audio ISR (process(), draining before ck->run()). On overflow it drops the newest
// message (never blocks the producer). N must be a power of two. Same shape as csound_midi.h's NoteQueue.
template <uint32_t N>
class MidiMsgQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two >= 2");
public:
    // Producer side. Returns false (dropped) if the ring is full.
    bool push(MidiMessage m) {
        const uint32_t h = _head.load(std::memory_order_relaxed);
        const uint32_t t = _tail.load(std::memory_order_acquire);
        if (h - t >= N) return false;                 // full
        _buf[h & (N - 1)] = m;
        _head.store(h + 1, std::memory_order_release);
        return true;
    }
    // Consumer side. Returns false (and leaves `out` untouched) if the ring is empty.
    bool pop(MidiMessage& out) {
        const uint32_t t = _tail.load(std::memory_order_relaxed);
        const uint32_t h = _head.load(std::memory_order_acquire);
        if (t == h) return false;                     // empty
        out = _buf[t & (N - 1)];
        _tail.store(t + 1, std::memory_order_release);
        return true;
    }
private:
    MidiMessage           _buf[N];
    std::atomic<uint32_t> _head{0};
    std::atomic<uint32_t> _tail{0};
};

} // namespace spotykach
