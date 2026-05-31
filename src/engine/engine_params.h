// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace spotykach {

// Logical parameters an engine exposes to the platform. One physical control may map to
// several of these (via modifier layers / mode), and a single ParamId may fan out to
// different leaf setters depending on deck mode - that mode-dependence lives inside the
// engine's set_param, not in the platform. Mirrors the granular engine's MValue-backed set.
enum class ParamId : uint8_t {
    Pos,            // start position
    FluxFb,         // flux feedback
    Env,            // envelope shape
    EnvSize,        // loop size from the env knob (Drift)
    Size,           // loop size (Reel/Slice) / window spread (Drift)
    Win,            // window size (Drift)
    PolySlice,      // mono/poly slice select
    Speed,          // playback speed (Reel/Drift) / pitch (Slice)
    FluxIntensity,
    GritIntensity,
    FluxMix,
    GritMix,
    Feedback,       // overdub feedback
    Mix,            // deck in/out mix
    ModSpeed,       // modulator (LFO/follower) speed
    ModAmp,         // modulator depth
    Tempo,          // global
    ClickMix,       // global
    PanSpeed,       // global
    PanRange,       // global
    KeyInterval,    // global (launch quantization)
    Crossfade,      // global deck A/B mix
    Count
};

// Optional behaviours an engine opts into. The platform offers these as a toolkit; a
// variant enables only the subset it needs (a non-looper omits Recording/TapeStorage/etc.).
enum Capability : uint32_t {
    CapRecording     = 1u << 0,
    CapTapeStorage   = 1u << 1,
    CapStepSequencer = 1u << 2,
    CapLaunchQuant   = 1u << 3,
    CapTransport     = 1u << 4,
    CapDualDeck      = 1u << 5,
};
using Capabilities = uint32_t;

};
