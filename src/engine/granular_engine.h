// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "core/core.h"
#include "core/speed.map.h"
#include "nocopy.h"

#include <cstdint>


namespace spotykach {

// Indicator state the platform's LED code reads to render the flux/grit, play/rev, and alt
// (track) indicators. This is the LED migration's intermediate step (Round 2): the engine
// reports granular indicator state and the platform still owns the color palette + the
// blink/timer/storage/_touched compositing. The end state (render(DisplayModel&)) will have the
// engine fill the DisplayModel indicators directly; these queries retire then.
struct FxLeds   { Fx::GritMode grit_mode; bool grit_on; bool flux_on; };
struct PlayLeds { Mode mode; bool playing; bool play_queued; bool reverse; bool armed; bool recording; Deck::Source source; };
struct AltLeds  { bool track_armed; bool track_recording; };

// Transport + topology indicator state for the platform's ISR LED render (_draw_leds) and the
// launch-quant display. Same query pattern as the *Leds above. NOTE: the Driver/transport lives
// in Core today but is conceptually platform; this keeps the core() reads off the LED code
// without yet resolving that ownership question (a later round).
struct TransportLeds { Driver::Source source; bool key_at_quarter; bool key_sub_quarter; bool external_sync; uint8_t key_interval; };
struct DeckLeds      { Mode mode; Modulator::Type mod_type; bool mod_synced; };

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

// The granular looper as an IEngine. It owns the Core graph, forwards the audio lifecycle, and
// (after the input migration) owns all granular *input* meaning: parameters, MIDI, and pad
// gestures - see the grouped methods below. The refactor is PAUSED at this input-decoupled
// milestone; the output/IO side (LEDs, CV, gate, storage) still reaches the graph through the
// core() escape hatch documented at that method. See docs/refactor-status.md.
class GranularEngine : public IEngine {
public:
    GranularEngine() = default;
    ~GranularEngine() override = default;

    void init(const EngineContext& ctx) override { _core.init(ctx); _speed_map.init(); }
    void prepare() override { _core.prepare(); }
    void process(const float* const* in, float** out, size_t size) override {
        _core.process(in, out, size);
    }

    // Parameter API (Phase 3a). The platform will drive these in place of reaching into Core
    // directly. set_param owns the mode-dependent dispatch/fan-out (Reel/Slice/Drift); the
    // deck arg is ignored for global params. param() returns the last-set value (cache).
    void set_param(ParamId id, Deck::Ref deck, float value);
    float param(ParamId id, Deck::Ref deck) const;

    Capabilities capabilities() const;

    // MIDI meaning (Phase 3c). The platform parses MIDI and clocks transport; the engine
    // decides what notes and transport mean for this instrument.
    // handle_midi_note: channel->deck, note->speed, trigger. Returns the triggered deck
    // (or Deck::Count if the channel is unmatched) so the platform can flash the gate LED.
    Deck::Ref handle_midi_note(uint8_t channel, uint8_t note);
    void handle_midi_transport(bool start); // true=start/continue, false=stop

    // FX pads (Phase 3c). The platform owns the pad gesture + modifier state; the engine
    // owns what flux/grit do. set_fx is momentary on/off; toggle_fx_lock is the alt+pad latch.
    void set_fx(Deck::Ref, FxKind, bool on);
    void toggle_fx_lock(Deck::Ref, FxKind);

    // Play/Rev pads (Phase 3c). The platform owns tap/hold + storage/tape + LED; the engine
    // owns the granular play/record/stop decision. on_play_pad returns is_empty (for the LED).
    void stop_if_generating(Deck::Ref);
    void clear_buffer(Deck::Ref);
    void on_record_pad(Deck::Ref, bool reverse);
    bool on_play_pad(Deck::Ref, bool reverse);

    // Seq pads (Phase 3c). Platform owns storage/tape + the hold-to-clear timer; the engine
    // owns the sequencer: arm/disarm the track, trigger it, and clear the recorded sequence.
    void on_seq_toggle_arm(Deck::Ref);
    void on_seq_trigger(Deck::Ref);
    void clear_sequence(Deck::Ref);
    void disarm_track(Deck::Ref); // disarm the deck's track if armed (Alt-pad action)

    // Modulator speed (Phase 3c). sync = the Alt modifier (LFO sync vs free).
    void set_mod_speed(Deck::Ref, float value, bool sync);

    // CV inputs (Phase 3c). The platform reads + calibrates each jack and routes by role; the
    // engine decides what each CV does. cv_voct caches the V/Oct speed for the gate trigger.
    void cv_mix(Deck::Ref, float value);
    void cv_size_pos(Deck::Ref, float value);
    void cv_voct(Deck::Ref, float value);
    void cv_crossfade(float value);

    // Gate (Phase 3c). on_gate_trigger fires the deck at the last V/Oct speed; the platform
    // owns edge/latency detection + the gate-out pulse timing. gate_out_triggered reports a
    // loop-reset event for the platform's gate-out.
    void on_gate_trigger(Deck::Ref);
    bool gate_out_triggered(Deck::Ref);

    // Storage audio port (Phase 3c, "TapeStorage" capability). The platform's Storage owns the
    // tape/slot state machine + SD I/O; it gets the deck's loop buffer as a raw byte range to
    // save/load. `frames` in audio_apply_loaded is the card's WAV-derived size_audio().
    bool   audio_is_empty(Deck::Ref);
    uint8_t* audio_data(Deck::Ref);
    size_t audio_recorded_bytes(Deck::Ref);
    size_t audio_capacity_bytes(Deck::Ref);
    void   audio_apply_loaded(Deck::Ref, size_t frames);

    // Transport (item 1). Thin forwards to the engine's Driver so the platform drives transport
    // through the engine instead of reaching through core().driver(). Per the faithful stopgap
    // (B1), the platform still owns clock-source selection + edge detection in tick(); these only
    // forward. The Driver still lives in Core today (see the core() note below) - relocating it to
    // a platform transport service is item 2's decision. Inline so they cost nothing (no LTO here).
    void  transport_set_on_quarter(std::function<void(const bool)> cb) { _core.driver().set_on_quarter(cb); }
    void  transport_set_on_clock_out(std::function<void()> cb)         { _core.driver().set_on_clock_out(cb); }
    Driver::Source transport_source()      { return _core.driver().source(); }
    void  transport_tick(const bool external_tick) { _core.driver().tick(external_tick); }
    bool  transport_is_external_sync()     { return _core.driver().is_external_sync(); }
    void  transport_reset()                { _core.driver().reset(); }
    void  transport_toggle_source()        { _core.driver().toggle_source(); }
    void  transport_tap_tempo()            { _core.driver().tap_tempo(); }
    float transport_tempo()                { return _core.driver().tempo(); }
    void  transport_set_tempo_norm(const float norm) { _core.driver().set_tempo_norm(norm); }

    // LED indicator state (LED migration Round 2). The platform reads these to render the
    // flux/grit, play/rev, and alt indicators; it keeps the colors + blink/timer/storage logic.
    FxLeds   fx_leds(Deck::Ref);
    PlayLeds play_leds(Deck::Ref);
    AltLeds  alt_leds(Deck::Ref);

    // Transport + topology state for the ISR LED render (_draw_leds) and launch-quant display.
    TransportLeds transport_leds();
    DeckLeds      deck_leds(Deck::Ref);
    float         mix() const;   // A/B crossfade (fader LEDs)
    Route         route() const; // channel topology (mode L/C/R LED)

    // Steady-state ring draw (LED migration Round 3). Caller must have cleared the ring and set
    // the default (mode) color + 0.5 brightness; this draws the empty/recording/playing segment +
    // heads on that baseline and returns the geometry the platform's pos/size/overdub overlays use.
    RingGeometry render_ring(LEDRing& ring, Deck::Ref, float breathe_brightness);

    // Escape hatch: direct access to the granular Core. LED rendering and transport (item 1) are
    // off it now; the remaining users are the two non-transport coupling categories the platform
    // still reaches through core() for (see docs/refactor-status.md "Still coupled"):
    //   - Category 2: switch-config writes in _process_switches (set_route, mod type, deck mode,
    //     size/pos mod flags, grit mode).
    //   - Category 3: deck-state readbacks in the apply pass / pot queue (deck.mode(), is_empty(),
    //     norm_start()/fx state for MValue seeding, tempo_to_fit()).
    // Both are slated to fold into the item-3 MValue->ParamId toolkit (engine-declared bindings)
    // rather than be hand-wrapped here; once they migrate, core() can be removed.
    Core& core() { return _core; }

private:
    NOCOPY(GranularEngine)

    static Deck::Ref _safe_ref(Deck::Ref ref) { return ref < Deck::Count ? ref : Deck::A; }

    Core _core;
    SpeedMap<60> _speed_map;
    float _voct_speed[Deck::Count] = { 1.f, 1.f }; // last V/Oct CV speed, used by gate triggers
    float _param_cache[static_cast<size_t>(ParamId::Count)][Deck::Count] = {};
};

};
