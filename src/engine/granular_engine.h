// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "core/core.h"
#include "core/speed.map.h"
#include "nocopy.h"

#include <cstdint>


namespace spotykach {

// The granular looper as an IEngine. Phase 2: it owns the Core graph and forwards the audio
// lifecycle to it. The UI/storage/CV paths still reach the graph through core() until the
// interaction layer is migrated into this class (Phase 3). The escape hatch is intentional
// and temporary - it is the seam those later increments collapse.
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

    // Temporary direct access for the still-coupled UI/storage/CV paths.
    Core& core() { return _core; }

private:
    NOCOPY(GranularEngine)

    static Deck::Ref _safe_ref(Deck::Ref ref) { return ref < Deck::Count ? ref : Deck::A; }

    Core _core;
    SpeedMap<60> _speed_map;
    float _param_cache[static_cast<size_t>(ParamId::Count)][Deck::Count] = {};
};

};
