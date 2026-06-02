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

// The two end-of-chain effects, addressed by the flux/grit pads.
enum class FxKind { Flux, Grit };

// Categorical (enum/topology) configuration the platform writes from the panel switches, kept
// separate from the continuous ParamId channel because the MValue pickup/threshold/value-display
// apparatus is meaningless for a categorical (item 3a-0). The platform passes a small selector
// int read from the switch bits; the engine maps it to its own enums and owns any side effects
// (panner inference, per-deck LFO palette). deck is ignored for global configs (Route).
enum class ConfigId : uint8_t {
    Route,       // global: 0=Stereo, 1=DoubleMono, 2=GenerativeStereo
    ModType,     // per-deck: 0=LFO, 1=Follow
    LfoShape,    // per-deck selector (0/1); the engine owns the per-deck shape palette
    Mode,        // per-deck: 0=Slice, 1=Reel, 2=Drift
    StartModOn,  // per-deck: 0/1
    SizeModOn,   // per-deck: 0/1
    Count
};

// Returned by toggle_grit_mode: the now-active grit sub-effect's intensity/mix, so the platform
// can reseed its MValue pickup to the values the live effect reports after the mode switch.
struct GritReseed { float intensity; float mix; };

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
