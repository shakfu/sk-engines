// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>

#include "engine/mode.h"     // Mode, Route, ModType, GritMode, DeckSource (contract-owned, item 5b)
#include "core/driver.h"     // Driver::Source - last granular leak here, removed in item 5c (R2)
#include "engine/display_model.h" // LEDRing (render_ring target)

namespace spotykach {

// LED query result types the platform reads to render indicators + rings. Lifted out of
// GranularEngine so they can be IEngine's query-method return types (item 2). These are the
// LED migration's intermediate channel: the engine reports state, the platform owns the color
// palette + blink/timer/storage/_touched compositing. The end state (engine fills DisplayModel
// via render()) plus the MValue->ParamId value-display toolkit will retire these in item 3.
struct FxLeds   { GritMode grit_mode; bool grit_on; bool flux_on; };
struct PlayLeds { Mode mode; bool playing; bool play_queued; bool reverse; bool armed; bool recording; DeckSource source; };
struct AltLeds  { bool track_armed; bool track_recording; };

// Transport + topology indicator state for the ISR LED render (_draw_leds) and launch-quant display.
struct TransportLeds { Driver::Source source; bool key_at_quarter; bool key_sub_quarter; bool external_sync; uint8_t key_interval; };
struct DeckLeds      { Mode mode; ModType mod_type; bool mod_synced; };

// Geometry of the steady-state ring the engine drew, for the platform's transient overlays
// (pos / size-change / overdub-head) to render against. playing == false means the engine drew
// the empty-breathe / recording / nothing case and the platform skips those overlays.
struct RingGeometry {
    bool  playing      = false;  // engine drew the !empty playing segment
    float start        = 0.f;    // raw norm_start (overlays recompute against this)
    float size         = 0.f;    // segment_size drawn (post-.95 spread for Drift)
    Mode  mode         = Mode::Reel;
    bool  overdubbing  = false;
    float overdub_head = 0.f;    // write_head / rec_size, drawn AFTER the size overlay (order)
};

};
