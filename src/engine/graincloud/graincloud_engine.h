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

// graincloud - a polyphonic grain CLOUD (engine #11 in docs/engine-ideas.md), built on a de-STL'd port
// of GrainflowLib (thirdparty/grainflow). It is the granular the looper *isn't*: dozens of independent
// grains, each with its own pitch, buffer position, pan, duration and direction, scattered over a
// recorded buffer - a synthesizer-style cloud rather than a tape-loop scanner.
//
// Two decks = two clouds (A/B), blended by the crossfader. Each deck records the live input into its own
// SDRAM buffer (Rev pad), exposes that buffer to the SD save/load port (CapTapeStorage), and scatters a
// fixed pool of grains over it. Knob map (per deck, from the idea-doc):
//   POS = cloud centre position   SIZE = position spray   PITCH = grain transpose (+CV/MIDI)
//   ENV = grain duration/window   MODFREQ = grain density  MOD_AMT = pitch+pan spray   SOS = dry/wet
//   Alt+PITCH (Aux) = direction / glisson / quantize cycle
//
// PIMPL: all GrainflowLib template types live in graincloud_engine.cpp; the header stays free of them
// (the composition root includes it). The Impl - the two grain collections, their arena grain arrays,
// the record buffers, and the shared per-block io scratch - is placement-new'd in the injected SDRAM
// arena at init(). See docs/dev/graincloud-impl.md.
class GraincloudEngine : public IEngine {
public:
    GraincloudEngine() = default;
    ~GraincloudEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override {
        // CapTapeStorage (SD loop save/load) is back ON. We deliberately do NOT advertise CapAux: on a
        // storage engine the platform uses Alt+PITCH to select the loop slot, which collided with the
        // Aux selector and crashed. Grain direction now lives on the Reel/Slice/Drift mode switch
        // (set_config(Mode)) instead, leaving Alt+PITCH free for the slot picker.
        return CapRecording | CapTapeStorage | CapDualDeck | CapOwnDisplay | CapTransport;
    }

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;
    void  set_mod_speed(DeckRef::Ref deck, float value, bool sync) override;
    bool  set_config(ConfigId id, DeckRef::Ref deck, int value) override; // Mode switch -> grain direction

    DeckRef::Ref handle_midi_note(uint8_t channel, uint8_t note) override;
    void  cv_voct(DeckRef::Ref deck, float value) override;

    // Record/clear pads.
    void  on_record_pad(DeckRef::Ref deck, bool reverse) override;
    void  clear_buffer(DeckRef::Ref deck) override;

    // Storage audio port (CapTapeStorage): expose the recorded buffer to the SD save/load path.
    bool     audio_is_empty(DeckRef::Ref deck) override;
    uint8_t* audio_data(DeckRef::Ref deck) override;
    size_t   audio_recorded_bytes(DeckRef::Ref deck) override;
    size_t   audio_capacity_bytes(DeckRef::Ref deck) override;
    void     audio_apply_loaded(DeckRef::Ref deck, size_t frames) override;

    void render(DisplayModel& m) override;

private:
    NOCOPY(GraincloudEngine)

    struct Impl;        // defined in graincloud_engine.cpp (owns the GrainflowLib DSP)
    Impl* _p = nullptr; // placement-new'd in the arena at init()
};

};
