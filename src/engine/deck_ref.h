// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace spotykach {

// The A/B deck (channel) selector the platform and IEngine speak. Contract-owned (item 5b) so the
// interface no longer pulls the granular `core/deck.h` for it. Kept an UNSCOPED enum so it still
// implicitly converts to int - it is used pervasively as an array index (e.g. timers[DeckRef::A]).
// Nested in a struct (not a bare top-level enum) so A/B/Count/None don't leak into the namespace.
// The granular `class Deck` aliases this (`using Ref = DeckRef::Ref;`).
struct DeckRef {
    enum Ref : uint8_t {
        A,
        B,
        Count,
        None = 0xff
    };
};

};
