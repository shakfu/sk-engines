// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "nocopy.h"

#include "engine/faust/faust_dsp.h" // the cyfaust-generated kernel: ::mydsp

namespace spotykach {

// FAUST-SPIKE (throwaway). A minimal engine that wraps a cyfaust-generated DSP kernel behind IEngine
// so the generated-code SRAM_EXEC cost on the H7 can be read directly: `make ENGINE=faust` links with
// -Wl,--print-memory-usage and prints the SRAM_EXEC region usage. Purpose is measurement, not a
// shippable engine - it is single-voice, mono->stereo, with no sequencer/tape/MIDI.
//
// The kernel (src/engine/faust/voice.dsp -> faust_dsp.h) is a saw -> Moog VCF -> ADSR voice. Its
// compute(count, in, out) is the audio path; params are reached via buildUserInterface (see ParamUI
// in the .cpp). The platform's POS/PITCH/SOS knobs drive cutoff/freq/res; the Play pad gates the
// envelope. CapOwnDisplay only -> everything else is the IEngine no-op default.
class FaustEngine : public IEngine {
public:
    FaustEngine() = default;
    ~FaustEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override { return CapOwnDisplay; }

    void set_param(ParamId id, DeckRef::Ref deck, float value) override;
    bool on_play_pad(DeckRef::Ref deck, bool reverse) override; // gate the voice on/off
    void render(DisplayModel& m) override;

private:
    NOCOPY(FaustEngine)

    ::mydsp _dsp;                       // cyfaust-generated kernel (global namespace)
    // Control zones captured from the kernel via buildUserInterface at init (label -> FAUSTFLOAT*).
    FAUSTFLOAT* _z_freq = nullptr;
    FAUSTFLOAT* _z_cut  = nullptr;
    FAUSTFLOAT* _z_res  = nullptr;
    FAUSTFLOAT* _z_gate = nullptr;
    float _peak = 0.f;                  // for the level-meter render
};

};
