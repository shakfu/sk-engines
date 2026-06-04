// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <functional>

#include "engine/mode.h"   // ClockSource (transport clock-source enum, contract-owned)

namespace spotykach {

// Per-tick event the platform Transport fans out to whichever engine subscribed via
// ITransport::set_on_tick. This is the engine-agnostic clock contract: a granular looper, a
// tempo-synced delay, or a Euclidean sequencer all reconstruct what they need from these fields.
//
// `index`   - monotonic counter of common (divided) ticks since boot; a step sequencer uses it to
//             place pulses on the grid without tracking its own tick count.
// `tick`    - a common/divided tick fired this step (the Divider's resolution); false on the
//             in-between sub-ticks emitted to keep tempo tracking smooth.
// `key`     - a key (bar) boundary, per the key-interval.
// `quarter` - a quarter-note boundary (metronome / LED blink).
// `tempo`   - current BPM.
// `reset`   - the musical grid was just realigned (clock reset / external-sync resync); a sequencer
//             should realign its own divider. Carries the normal {tick,key,quarter} flags too so a
//             realign can also fire an immediate downbeat.
struct TransportTick {
    uint32_t index   = 0;
    bool     tick    = false;
    bool     key     = false;
    bool     quarter = false;
    float    tempo   = 0.f;
    bool     reset   = false;
};

// Engine-facing view of the platform clock: READ-ONLY queries plus a tick subscription. The
// clock-driving control API (tick/reset/tap/source/tempo set, the on_quarter/on_clock_out
// callbacks) lives on the concrete Transport, held only by the platform (app + CoreUI) - an engine
// can observe the transport and react to ticks, but cannot command it. Injected at init() via
// EngineContext, mirroring how ITimeSource is already handed to the core.
struct ITransport {
    virtual ~ITransport() = default;

    virtual float               tempo() const = 0;
    virtual ClockSource::Source source() const = 0;
    virtual bool                is_external_sync() const = 0;
    virtual uint8_t             key_interval() const = 0;
    virtual bool                is_key_sub_quarter() const = 0;

    virtual void set_on_tick(std::function<void(const TransportTick&)> on_tick) = 0;
};

};
