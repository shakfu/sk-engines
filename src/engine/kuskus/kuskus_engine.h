// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "nocopy.h"

#include <cstddef>
#include <cstdint>

namespace spotykach {

// kuskus - the Mutable Instruments *Beads* granular processor (thirdparty/beads), one stereo instance
// spread across both decks. Design and control map: docs/dev/kuskus.md; user reference:
// docs/engines/kuskus.md.
//
// PIMPL: all Beads/stmlib types live in kuskus_engine.cpp. stmlib.h declares a global `namespace impl`
// that collides with app.cpp's `impl` instance, so no Beads header may reach this one (as reso/mosc).
class KuskusEngine : public IEngine {
public:
    KuskusEngine() = default;
    ~KuskusEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override;
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override {
        return CapDualDeck | CapOwnDisplay | CapTransport;
    }

#if SPK_TERMINAL
    // The ids set_param acts on (kuskus_engine.cpp). ModSpeed (DENSITY / input gain) is bound but
    // platform-owned: it arrives through set_mod_speed and `describe` filters it.
    ParamMask live_params() const override {
        return (1u << static_cast<uint32_t>(ParamId::Speed))
             | (1u << static_cast<uint32_t>(ParamId::Pos))
             | (1u << static_cast<uint32_t>(ParamId::Size))
             | (1u << static_cast<uint32_t>(ParamId::Env))
             | (1u << static_cast<uint32_t>(ParamId::ModAmp))
             | (1u << static_cast<uint32_t>(ParamId::Mix))
             | (1u << static_cast<uint32_t>(ParamId::Crossfade));
    }
    ConfigMask live_configs() const override {
        return static_cast<ConfigMask>((1u << static_cast<uint32_t>(ConfigId::Route))
                                     | (1u << static_cast<uint32_t>(ConfigId::Mode)));
    }
    // Beads' front-panel names for the deck A meaning. Deck B carries the attenuverters and the
    // gain/reverb/level controls under the same slots (see the control map).
    const char* param_label(ParamId id) const override {
        switch (id) {
            case ParamId::Speed:     return "pitch";
            case ParamId::Pos:       return "time";
            case ParamId::Size:      return "size";
            case ParamId::Env:       return "shape";
            case ParamId::ModAmp:    return "feedback";
            case ParamId::Mix:       return "drywet";
            case ParamId::Crossfade: return "drywet";
            default: return nullptr;
        }
    }
#endif

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;
    void  set_mod_speed(DeckRef::Ref deck, float value, bool sync) override;
    bool  set_config(ConfigId id, DeckRef::Ref deck, int value) override;
    Route route() const override;
    float mix() const override;

    DeckRef::Ref handle_midi_note(uint8_t channel, uint8_t note) override;
    void  set_fx(DeckRef::Ref deck, FxKind kind, bool on) override;
    bool  on_play_pad(DeckRef::Ref deck, bool reverse) override;
    void  on_record_pad(DeckRef::Ref deck, bool reverse) override;
    void  on_gate_trigger(DeckRef::Ref deck) override;
    bool  gate_out_triggered(DeckRef::Ref deck) override;

    void  cv_mix(DeckRef::Ref deck, float value) override;
    void  cv_size_pos(DeckRef::Ref deck, float value) override;
    void  cv_voct(DeckRef::Ref deck, float value) override;
    void  cv_crossfade(float value) override;

    void render(DisplayModel& m) override;

private:
    NOCOPY(KuskusEngine)

    struct Impl;        // defined in kuskus_engine.cpp (owns the Beads DSP)
    Impl* _p = nullptr; // placement-new'd in the arena at init()
};

};
