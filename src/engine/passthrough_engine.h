// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "nocopy.h"

#include <cmath>

namespace spotykach {

// SKETCH / grounding artifact (host-only; not wired into the firmware or app.cpp).
//
// A deliberately-different, non-granular engine: stereo passthrough with a level-meter display.
// Its job is to make the platform<->engine contract concrete against a SECOND consumer before
// the LED redesign - especially what render(DisplayModel&) looks like for an engine that is not
// a looper, and capability opt-in (it declares only Transport: no Recording/Tape/Sequencer).
//
// To actually *host* a 2nd engine, the platform's concrete GranularEngine coupling must be
// lifted to a shared interface. The FULL platform-driven surface a real 2nd engine must
// implement (today these live concretely on GranularEngine, called by CoreUI/Storage):
//
//   params : set_param(ParamId,Deck::Ref,float) / param(...) / set_mod_speed(...)
//   midi   : handle_midi_note(ch,note) / handle_midi_transport(start)
//   pads   : set_fx / toggle_fx_lock / on_play_pad / on_record_pad / stop_if_generating /
//            clear_buffer / on_seq_toggle_arm / on_seq_trigger / clear_sequence / disarm_track
//   cv/gate: cv_mix / cv_size_pos / cv_voct / cv_crossfade / on_gate_trigger / gate_out_triggered
//   storage: audio_is_empty / audio_data / audio_recorded_bytes / audio_capacity_bytes /
//            audio_apply_loaded
//   display: render(DisplayModel&)              <-- the remaining hard piece (LED migration)
//
// This sketch implements only the audio lifecycle + capabilities + render(); the rest would be
// no-ops on a passthrough and are omitted here on purpose (a real variant lifts the interface).
class PassthroughEngine : public IEngine {
public:
    PassthroughEngine() = default;
    ~PassthroughEngine() override = default;

    void init(const EngineContext&) override { _peak = 0.f; }
    void prepare() override {}

    void process(const float* const* in, float** out, size_t size) override {
        float peak = 0.f;
        for (size_t i = 0; i < size; i++) {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
            peak = std::fmax(peak, std::fmax(std::fabs(out[0][i]), std::fabs(out[1][i])));
        }
        _peak = peak;
    }

    Capabilities capabilities() const { return CapTransport; }

    // A non-granular display: a symmetric level meter on both rings + lit play indicators.
    void render(DisplayModel& m) const {
        m.clear();
        const float level = _peak > 1.f ? 1.f : _peak;
        const int lit = static_cast<int>(level * DisplayModel::kRingPixels + 0.5f);
        for (int r = 0; r < 2; r++) {
            for (int p = 0; p < lit && p < DisplayModel::kRingPixels; p++) {
                m.ring[r][p] = { 0xffffff, 1.f };
            }
            m.play[r] = { 0x00ff00, 1.f };
        }
    }

private:
    NOCOPY(PassthroughEngine)

    float _peak = 0.f;
};

};
