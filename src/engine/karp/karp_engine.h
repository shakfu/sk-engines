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

// karp - a dual resonator / pluck voice (engine #1 in docs/engine-ideas.md), built on the Mutable
// Instruments *Rings* DSP (thirdparty/rings) - the gold-standard physical-modelling resonator (modal
// bodies, sympathetic strings, plucked strings, FM). Each deck wraps one rings::Part (mono).
//
// The reel/slice/drift mode switch (ConfigId::Mode, per deck) selects how the resonator is excited;
// Alt+PITCH (ParamId::Aux, CapAux) selects the Rings model - two orthogonal axes:
//   Reel  - the resonator is fed CONTINUOUSLY (live input + internal noise): a sympathetic body / drone.
//   Slice - discrete PLUCKS: each trigger (pad / gate / MIDI / arp) strums Rings' internal exciter.
//   Drift - a scatter cloud: an internal scheduler auto-strums at randomized intervals/notes.
//
// Knob map (per deck): PITCH = note, SIZE = damping (decay), POS = position, ENV = brightness,
// MOD_AMT = structure, SOS = dry/wet, MODFREQ = Drift density / Slice arp rate, Alt+PITCH = model.
//
// PIMPL: all Rings/stmlib types live in karp_engine.cpp. The header must stay free of them because the
// composition root (app.cpp, via engine_select.h) includes it, and stmlib.h declares a global
// `namespace impl` that collides with app.cpp's `impl` instance. The Impl object (and the two ~108 KB
// Parts + 64 KB reverb buffers it owns) is placement-new'd in the injected SDRAM arena at init().
class KarpEngine : public IEngine {
public:
    KarpEngine() = default;
    ~KarpEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override {
        return CapOwnDisplay | CapDualDeck | CapAux | CapTransport;
    }

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;
    void  set_mod_speed(DeckRef::Ref deck, float value, bool sync) override;
    void  set_aux_active(DeckRef::Ref deck, bool active) override;
    bool  set_config(ConfigId id, DeckRef::Ref deck, int value) override;

    DeckRef::Ref handle_midi_note(uint8_t channel, uint8_t note) override;
    void  cv_voct(DeckRef::Ref deck, float value) override;
    void  on_gate_trigger(DeckRef::Ref deck) override;
    bool  on_play_pad(DeckRef::Ref deck, bool reverse) override;
    void  on_seq_trigger(DeckRef::Ref deck) override;

    void render(DisplayModel& m) override;

private:
    NOCOPY(KarpEngine)

    struct Impl;        // defined in karp_engine.cpp (owns the Rings DSP)
    Impl* _p = nullptr; // placement-new'd in the arena at init()
};

};
