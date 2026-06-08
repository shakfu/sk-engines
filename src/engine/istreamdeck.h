#pragma once

#include <cstdint>

namespace spotykach {

// Platform streaming service, injected into the engine via EngineContext (like ITransport). Lets an
// engine play arbitrarily long files from SD and record arbitrarily long takes to SD, bypassing the
// in-SDRAM loop-length cap: the audio ISR only ever touches lock-free rings (the *_consume/*_produce
// calls), while the platform does the slow FatFs I/O in the main loop.
//
// Threading contract:
//  - play_consume / record_produce are called from the audio ISR (process()). They never block.
//  - start_play / start_record / stop are control ops called from the MAIN LOOP only (e.g. the engine's
//    on_play_pad/on_record_pad handlers, which the UI invokes outside the audio ISR). They touch FatFs.
//  - is_playing / is_recording are cheap state reads, safe from either side.
struct IStreamDeck {
    virtual ~IStreamDeck() = default;

    // --- ISR side (process()) --------------------------------------------------------------------
    // Pull `n` bytes of interleaved playback audio into `dst`; shortfall is zero-filled (silence).
    virtual uint32_t play_consume(uint8_t* dst, uint32_t n) = 0;
    // Push `n` bytes of interleaved record audio; bytes that don't fit are dropped (never blocks).
    virtual uint32_t record_produce(const uint8_t* src, uint32_t n) = 0;

    // --- state (either side) ---------------------------------------------------------------------
    virtual bool is_playing()   const = 0;
    virtual bool is_recording() const = 0;

    // --- main-loop control -----------------------------------------------------------------------
    virtual bool start_play(const char* path)   = 0;  // false if busy / file missing
    virtual bool start_record(const char* path) = 0;  // false if busy
    virtual void stop()                         = 0;  // stop whichever is active; finalizes a record
};

} // namespace spotykach
