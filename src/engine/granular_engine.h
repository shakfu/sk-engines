// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/engine_leds.h"   // FxLeds/PlayLeds/AltLeds/TransportLeds/DeckLeds/RingGeometry
#include "engine/display_model.h"
#include "core/core.h"
#include "core/speed.map.h"
#include "nocopy.h"

#include <cstdint>


namespace spotykach {

// The granular looper as an IEngine. It owns the Core graph, forwards the audio lifecycle, and
// (after the input migration) owns all granular *input* meaning: parameters, MIDI, and pad
// gestures - see the grouped methods below. The refactor is PAUSED at this input-decoupled
// milestone; the output/IO side (LEDs, CV, gate, storage) still reaches the graph through the
// core() escape hatch documented at that method. See docs/refactor-status.md.
class GranularEngine : public IEngine {
public:
    GranularEngine() = default;
    ~GranularEngine() override = default;

    void init(const EngineContext& ctx) override;  // pre-seeds the param cache (impl in .cpp, -Os)
    void prepare() override { _core.prepare(); }
    void process(const float* const* in, float** out, size_t size) override {
        _core.process(in, out, size);
    }

    // Parameter API (Phase 3a). The platform will drive these in place of reaching into Core
    // directly. set_param owns the mode-dependent dispatch/fan-out (Reel/Slice/Drift); the
    // deck arg is ignored for global params. param() returns the last-set value (cache).
    void set_param(ParamId id, Deck::Ref deck, float value) override;
    float param(ParamId id, Deck::Ref deck) const override;

    // Categorical config (item 3a-0). Maps the platform's switch selectors to Core enums and owns
    // the side effects (panner inference on mode/route, per-deck LFO palette). set_config returns
    // whether Mode changed; toggle_grit_mode returns the reseed values; tempo_to_fit the fit BPM.
    bool       set_config(ConfigId id, Deck::Ref deck, int value) override;
    float      tempo_to_fit(Deck::Ref deck, float fraction) override;
    GritReseed toggle_grit_mode(Deck::Ref deck) override;

    // Knob layout (item 3a-3): map the deck's granular Mode to the platform-facing DeckLayout, and
    // report tempo-fit eligibility (Slice && non-empty), so the platform stops reading Core's Mode.
    DeckLayout deck_layout(Deck::Ref deck) override;
    bool       size_sets_tempo(Deck::Ref deck) override;

    Capabilities capabilities() const override;

    // MIDI meaning (Phase 3c). The platform parses MIDI and clocks transport; the engine
    // decides what notes and transport mean for this instrument.
    // handle_midi_note: channel->deck, note->speed, trigger. Returns the triggered deck
    // (or Deck::Count if the channel is unmatched) so the platform can flash the gate LED.
    Deck::Ref handle_midi_note(uint8_t channel, uint8_t note) override;
    void handle_midi_transport(bool start) override; // true=start/continue, false=stop

    // FX pads (Phase 3c). The platform owns the pad gesture + modifier state; the engine
    // owns what flux/grit do. set_fx is momentary on/off; toggle_fx_lock is the alt+pad latch.
    void set_fx(Deck::Ref, FxKind, bool on) override;
    void toggle_fx_lock(Deck::Ref, FxKind) override;

    // Play/Rev pads (Phase 3c). The platform owns tap/hold + storage/tape + LED; the engine
    // owns the granular play/record/stop decision. on_play_pad returns is_empty (for the LED).
    void stop_if_generating(Deck::Ref) override;
    void clear_buffer(Deck::Ref) override;
    void on_record_pad(Deck::Ref, bool reverse) override;
    bool on_play_pad(Deck::Ref, bool reverse) override;

    // Seq pads (Phase 3c). Platform owns storage/tape + the hold-to-clear timer; the engine
    // owns the sequencer: arm/disarm the track, trigger it, and clear the recorded sequence.
    void on_seq_toggle_arm(Deck::Ref) override;
    void on_seq_trigger(Deck::Ref) override;
    void clear_sequence(Deck::Ref) override;
    void disarm_track(Deck::Ref) override; // disarm the deck's track if armed (Alt-pad action)

    // Modulator speed (Phase 3c). sync = the Alt modifier (LFO sync vs free).
    void set_mod_speed(Deck::Ref, float value, bool sync) override;

    // CV inputs (Phase 3c). The platform reads + calibrates each jack and routes by role; the
    // engine decides what each CV does. cv_voct caches the V/Oct speed for the gate trigger.
    void cv_mix(Deck::Ref, float value) override;
    void cv_size_pos(Deck::Ref, float value) override;
    void cv_voct(Deck::Ref, float value) override;
    void cv_crossfade(float value) override;

    // Gate (Phase 3c). on_gate_trigger fires the deck at the last V/Oct speed; the platform
    // owns edge/latency detection + the gate-out pulse timing. gate_out_triggered reports a
    // loop-reset event for the platform's gate-out.
    void on_gate_trigger(Deck::Ref) override;
    bool gate_out_triggered(Deck::Ref) override;

    // Storage audio port (Phase 3c, "TapeStorage" capability). The platform's Storage owns the
    // tape/slot state machine + SD I/O; it gets the deck's loop buffer as a raw byte range to
    // save/load. `frames` in audio_apply_loaded is the card's WAV-derived size_audio().
    bool   audio_is_empty(Deck::Ref) override;
    uint8_t* audio_data(Deck::Ref) override;
    size_t audio_recorded_bytes(Deck::Ref) override;
    size_t audio_capacity_bytes(Deck::Ref) override;
    void   audio_apply_loaded(Deck::Ref, size_t frames) override;

    // Transport (item 1). Thin forwards to the engine's Driver so the platform drives transport
    // through the engine instead of reaching through core().driver(). Per the faithful stopgap
    // (B1), the platform still owns clock-source selection + edge detection in tick(); these only
    // forward. The Driver still lives in Core today (see the core() note below) - relocating it to
    // a platform transport service is item 2's decision. Inline so they cost nothing (no LTO here).
    void  transport_set_on_quarter(std::function<void(const bool)> cb) override { _core.driver().set_on_quarter(cb); }
    void  transport_set_on_clock_out(std::function<void()> cb) override         { _core.driver().set_on_clock_out(cb); }
    Driver::Source transport_source() override      { return _core.driver().source(); }
    void  transport_tick(const bool external_tick) override { _core.driver().tick(external_tick); }
    bool  transport_is_external_sync() override     { return _core.driver().is_external_sync(); }
    void  transport_reset() override                { _core.driver().reset(); }
    void  transport_toggle_source() override        { _core.driver().toggle_source(); }
    void  transport_tap_tempo() override            { _core.driver().tap_tempo(); }
    float transport_tempo() override                { return _core.driver().tempo(); }
    void  transport_set_tempo_norm(const float norm) override { _core.driver().set_tempo_norm(norm); }

    // CV outputs (DAC, block-rate). Fills n samples of the two modulator CV channels. Faithful to
    // the old per-sample DACCallback loop: each sample is zeroed then written by the deck's mod.
    void process_cv(float* cv0, float* cv1, size_t n) override {
        auto& ma = _core.mod(Deck::A);
        auto& mb = _core.mod(Deck::B);
        for (size_t i = 0; i < n; i++) {
            cv0[i] = 0.f; cv1[i] = 0.f;
            ma.process(cv0[i]);
            mb.process(cv1[i]);
        }
    }

    // LED indicator state (LED migration Round 2). The platform reads these to render the
    // flux/grit, play/rev, and alt indicators; it keeps the colors + blink/timer/storage logic.
    FxLeds   fx_leds(Deck::Ref) override;
    PlayLeds play_leds(Deck::Ref) override;
    AltLeds  alt_leds(Deck::Ref) override;

    // Transport + topology state for the ISR LED render (_draw_leds) and launch-quant display.
    TransportLeds transport_leds() override;
    DeckLeds      deck_leds(Deck::Ref) override;
    float         mix() const override;   // A/B crossfade (fader LEDs)
    Route         route() const override; // channel topology (mode L/C/R LED)

    // Steady-state ring draw (LED migration Round 3). Caller must have cleared the ring and set
    // the default (mode) color + 0.5 brightness; this draws the empty/recording/playing segment +
    // heads on that baseline and returns the geometry the platform's pos/size/overdub overlays use.
    RingGeometry render_ring(LEDRing& ring, Deck::Ref, float breathe_brightness) override;

private:
    NOCOPY(GranularEngine)

    static Deck::Ref _safe_ref(Deck::Ref ref) { return ref < Deck::Count ? ref : Deck::A; }

    Core _core;
    SpeedMap<60> _speed_map;
    float _voct_speed[Deck::Count] = { 1.f, 1.f }; // last V/Oct CV speed, used by gate triggers
    float _param_cache[static_cast<size_t>(ParamId::Count)][Deck::Count] = {};
};

};
