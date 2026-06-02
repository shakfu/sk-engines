// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core/engine_context.h"  // EngineContext, Deck::Ref (via deck.h)
#include "core/driver.h"          // Driver::Source - leaks here pending the transport relocation
#include "core/mode.h"            // Route (for route())
#include "engine/engine_params.h" // ParamId, FxKind, Capabilities
#include "engine/display_model.h" // DisplayModel
#include "engine/engine_leds.h"   // FxLeds/PlayLeds/AltLeds/TransportLeds/DeckLeds/RingGeometry

namespace spotykach {

// IEngine is the contract between the fixed hardware/UI platform and a swappable DSP engine.
// A "firmware variant" is a different IEngine implementation behind the same platform.
//
// Audio lifecycle (init/prepare/process) is pure-virtual: every engine must implement it; the
// audio path flows app -> IEngine -> engine -> DSP.
//
// Item 2 (interface lift, Strategy A): the rest of the platform-driven surface is declared here
// with **no-op default bodies** - an engine overrides only what it supports, and capabilities()
// advertises which optional regions are live. CoreUI/Storage drive the engine through this
// interface instead of a concrete GranularEngine.
//
// Deliberately NOT on IEngine yet (see docs/item2-interface-lift-plan.md):
//   - The LED query methods (*_leds / render_ring on GranularEngine) are retired by
//     render(DisplayModel&) in sub-round 2b; until then they stay concrete.
//   - process_cv (the DAC modulation outputs) lands in sub-round 2c, once its block-rate
//     mechanism is designed (no per-sample virtual on the DAC ISR).
//   - Switch-config writes + deck-state readbacks (Categories 2-3) await the item-3 toolkit and
//     stay concrete on GranularEngine via core().
// The transport_* group forwards to the engine's Driver today (Driver::Source leaks above);
// relocating Driver to a platform service is deferred and will likely remove these from IEngine.
class IEngine {
public:
    virtual ~IEngine() = default;

    // --- Audio lifecycle (required of every engine) ---------------------------------------------
    // Allocate/initialise from the injected platform context (buffers, sample rate, clock).
    virtual void init(const EngineContext& ctx) = 0;
    // Non-real-time, main-loop housekeeping.
    virtual void prepare() = 0;
    // Real-time audio block (audio ISR). Keep per-sample work inside non-virtual engine code.
    virtual void process(const float* const* in, float** out, size_t size) = 0;

    // --- Capabilities: which optional regions this engine opts into --------------------------
    virtual Capabilities capabilities() const { return 0; }

    // --- Parameters (platform control -> logical ParamId; engine owns the mode-dep dispatch) -
    virtual void  set_param(ParamId, Deck::Ref, float) {}
    virtual float param(ParamId, Deck::Ref) const { return 0.f; }
    virtual void  set_mod_speed(Deck::Ref, float value, bool sync) {}

    // --- Categorical config (item 3a-0): switch-position writes the platform used to make
    //     directly on Core. The engine maps the selector int to its enums and owns side effects.
    //     set_config returns true iff the value CHANGED (only Mode uses this, so the platform can
    //     re-apply size for the new mode). tempo_to_fit gives the BPM that fits `fraction` of the
    //     deck's loop (the Slice tap-hold gesture). toggle_grit_mode cycles the grit sub-effect and
    //     returns the now-active intensity/mix for the platform's MValue reseed. ---
    virtual bool       set_config(ConfigId, Deck::Ref, int) { return false; }
    virtual float      tempo_to_fit(Deck::Ref, float fraction) { return 0.f; }
    virtual GritReseed toggle_grit_mode(Deck::Ref) { return {}; }

    // --- MIDI meaning ------------------------------------------------------------------------
    virtual Deck::Ref handle_midi_note(uint8_t channel, uint8_t note) { return Deck::Count; }
    virtual void      handle_midi_transport(bool start) {}

    // --- FX pads -----------------------------------------------------------------------------
    virtual void set_fx(Deck::Ref, FxKind, bool on) {}
    virtual void toggle_fx_lock(Deck::Ref, FxKind) {}

    // --- Play/Rev pads (on_play_pad returns is_empty for the LED) -----------------------------
    virtual void stop_if_generating(Deck::Ref) {}
    virtual void clear_buffer(Deck::Ref) {}
    virtual void on_record_pad(Deck::Ref, bool reverse) {}
    virtual bool on_play_pad(Deck::Ref, bool reverse) { return true; }

    // --- Seq pads ----------------------------------------------------------------------------
    virtual void on_seq_toggle_arm(Deck::Ref) {}
    virtual void on_seq_trigger(Deck::Ref) {}
    virtual void clear_sequence(Deck::Ref) {}
    virtual void disarm_track(Deck::Ref) {}

    // --- CV inputs ---------------------------------------------------------------------------
    virtual void cv_mix(Deck::Ref, float value) {}
    virtual void cv_size_pos(Deck::Ref, float value) {}
    virtual void cv_voct(Deck::Ref, float value) {}
    virtual void cv_crossfade(float value) {}

    // --- Gate (gate_out_triggered reports a loop-reset for the platform's gate-out) ----------
    virtual void on_gate_trigger(Deck::Ref) {}
    virtual bool gate_out_triggered(Deck::Ref) { return false; }

    // --- Storage audio port (TapeStorage capability) -----------------------------------------
    virtual bool     audio_is_empty(Deck::Ref) { return true; }
    virtual uint8_t* audio_data(Deck::Ref) { return nullptr; }
    virtual size_t   audio_recorded_bytes(Deck::Ref) { return 0; }
    virtual size_t   audio_capacity_bytes(Deck::Ref) { return 0; }
    virtual void     audio_apply_loaded(Deck::Ref, size_t frames) {}

    // --- Transport (Transport capability; forwards to the engine's clock for now) ------------
    virtual void  transport_set_on_quarter(std::function<void(const bool)> cb) {}
    virtual void  transport_set_on_clock_out(std::function<void()> cb) {}
    virtual Driver::Source transport_source() { return Driver::Source::internal; }
    virtual void  transport_tick(const bool external_tick) {}
    virtual bool  transport_is_external_sync() { return false; }
    virtual void  transport_reset() {}
    virtual void  transport_toggle_source() {}
    virtual void  transport_tap_tempo() {}
    virtual float transport_tempo() { return 0.f; }
    virtual void  transport_set_tempo_norm(const float norm) {}

    // --- LED queries (item 2b): the engine reports indicator + ring-geometry state; the platform
    //     owns the color palette + blink/timer/storage/_touched compositing + the MValue overlays.
    //     Defaults are inert (a non-granular engine returns empty state and the granular-specific
    //     platform LED code reads nothing). These + render() below are transitional: item 3 unifies
    //     them into render(DisplayModel&) + the MValue->ParamId value-display toolkit. ---
    virtual FxLeds   fx_leds(Deck::Ref) { return {}; }
    virtual PlayLeds play_leds(Deck::Ref) { return {}; }
    virtual AltLeds  alt_leds(Deck::Ref) { return {}; }
    virtual TransportLeds transport_leds() { return {}; }
    virtual DeckLeds      deck_leds(Deck::Ref) { return {}; }
    virtual float mix() const { return 0.5f; }   // A/B crossfade (fader LEDs)
    virtual Route route() const { return Route::Stereo; } // channel topology (mode L/C/R LED)
    virtual RingGeometry render_ring(LEDRing& ring, Deck::Ref, float breathe_brightness) { return {}; }

    // --- CV outputs (DAC). The platform's DAC ISR calls this ONCE per block (not per sample),
    //     fills n samples of the two modulation/LFO CV channels, then converts to the DAC range.
    //     Block-rate by design so the ISR pays no per-sample virtual dispatch. Default = silence. ---
    virtual void process_cv(float* cv0, float* cv1, size_t n) {
        for (size_t i = 0; i < n; i++) { cv0[i] = 0.f; cv1[i] = 0.f; }
    }

    // --- Display: engine fills the panel model; platform blits + composites its overlays.
    //     Default draws nothing; the granular engine still drives LEDs via the queries above until
    //     item 3 moves it to render(DisplayModel&). ---
    virtual void render(DisplayModel&) {}
};

};
