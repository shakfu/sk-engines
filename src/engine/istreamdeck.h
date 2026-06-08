#pragma once

#include "engine/deck_ref.h"

#include <cstdint>

namespace spotykach {

// Platform streaming service, injected into the engine via EngineContext (like ITransport). Lets an
// engine play arbitrarily long files from SD and record arbitrarily long takes to SD, bypassing the
// in-SDRAM loop-length cap: the audio ISR only ever touches lock-free rings (the *_consume/*_produce
// calls), while the platform does the slow FatFs I/O in the main loop.
//
// Two INDEPENDENT decks (A/B), each its own play-XOR-record state machine, file, and ring - so the
// tape engine can run them like a pair of record decks (e.g. play A while recording B). Every call is
// per-deck.
//
// Threading contract:
//  - play_consume / record_produce are called from the audio ISR (process()). They never block.
//  - start_play / start_record / stop are control ops called from the MAIN LOOP only (e.g. the engine's
//    on_play_pad/on_record_pad handlers, which the UI invokes outside the audio ISR). They touch FatFs.
//  - is_playing / is_recording are cheap state reads, safe from either side.
struct IStreamDeck {
    virtual ~IStreamDeck() = default;

    // --- ISR side (process()) --------------------------------------------------------------------
    // Pull `n` bytes of playback audio for `deck` into `dst`; shortfall is zero-filled (silence).
    virtual uint32_t play_consume(DeckRef::Ref deck, uint8_t* dst, uint32_t n) = 0;
    // Push `n` bytes of record audio for `deck`; bytes that don't fit are dropped (never blocks).
    virtual uint32_t record_produce(DeckRef::Ref deck, const uint8_t* src, uint32_t n) = 0;

    // --- state (either side) ---------------------------------------------------------------------
    virtual bool is_playing(DeckRef::Ref deck)   const = 0;
    virtual bool is_recording(DeckRef::Ref deck) const = 0;

    // --- main-loop control -----------------------------------------------------------------------
    virtual bool start_play(DeckRef::Ref deck, const char* path)   = 0;  // false if busy / file missing
    virtual bool start_record(DeckRef::Ref deck, const char* path) = 0;  // false if busy
    virtual void stop(DeckRef::Ref deck)                           = 0;  // stop deck; finalizes a record

    // Looping: when enabled, playback rewinds to the file start at EOF instead of finishing. set_loop is
    // a cheap flag write (safe from the main loop); loop_frames is the playing file's length in frames
    // (0 if the deck is not playing) so the engine can shape fades/decay per loop.
    virtual void     set_loop(DeckRef::Ref deck, bool loop)  = 0;
    virtual uint32_t loop_frames(DeckRef::Ref deck) const    = 0;

    // Main-loop file probe (f_stat): does `path` exist on the card? Used to show recorded-vs-empty
    // slots. Not for the audio path.
    virtual bool     exists(const char* path) const = 0;
};

} // namespace spotykach
