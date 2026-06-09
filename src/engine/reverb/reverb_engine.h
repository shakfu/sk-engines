// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "nocopy.h"

namespace spotykach {

struct ReverbVoice; // defined in reverb_engine.cpp (wraps one generated kernel + its knob mapping)

// ReverbEngine - a multi-algorithm reverb whose DSP is Faust-generated. Each algorithm is a
// cyfaust-generated kernel (src/engine/reverb/<name>.dsp -> faust_kernel_<name>.h, one per namespace
// rv_<name>); Alt+PITCH (ParamId::Aux, CapAux) selects which one is live. Currently: a Dattorro plate
// and a Zita-rev1 hall.
// Stereo in -> stereo out; the platform's live audio is reverberated and wet/dry is a knob.
//
// Memory: a reverb kernel is hundreds of KB of comb/allpass/FDN delay-line state (Dattorro ~126 KB,
// Zita ~937 KB) - far too big for SRAM. Every kernel is placement-new'd into the injected SDRAM arena
// at init() (the pattern ResoEngine uses for Rings); only pointers live in this object. All kernels
// are constructed up front and the active one's compute() is called per block, so switching is a
// pointer flip with no audio-thread allocation. (Combining them into one Faust process would run every
// algorithm every sample - Faust has no branch elision - so separate kernels are the CPU-cheap design.)
//
// Knob map (reverb-agnostic roles; the 0..1 knob is linear-mapped into each slider's native range):
//   SOS   (Mix)    -> Mix    (wet/dry)        POS   (Pos)   -> Decay (tail length)
//   ENV   (Env)    -> Damp   (HF damping)     PITCH (Speed) -> Tone  (plate: prefilter / hall: low RT60)
//   SIZE  (Size)   -> SizeA  (plate: input diffusion / hall: pre-delay)
//   MODAMT(ModAmp) -> SizeB  (plate: tank diffusion  / hall: LF crossover)
//   Alt+PITCH (Aux) -> reverb algorithm select
class ReverbEngine : public IEngine {
public:
    static constexpr int kReverbCount = 2; // dattorro, zita (keep in sync with RV_NAMES / init())

    ReverbEngine() = default;
    ~ReverbEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override { return CapOwnDisplay | CapAux; }

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;
    void  set_aux_active(DeckRef::Ref deck, bool active) override;
    void  render(DisplayModel& m) override;

private:
    NOCOPY(ReverbEngine)

    ReverbVoice* _rv[kReverbCount] = { nullptr, nullptr }; // each placement-new'd in the SDRAM arena
    int          _active = 0;     // index into _rv

    void apply_all_knobs();      // push the six cached knob values into the active reverb

    // Cached normalised (0..1) knob values, returned by param() so the platform's MValue pickup tracks.
    // Indexed by the six knob roles (see kKnobCount in the .cpp); kept here as a fixed array.
    float _v[6]   = { 0.4f, 0.7f, 0.5f, 0.6f, 0.5f, 0.5f }; // Mix,Decay,Damp,Tone,SizeA,SizeB
    float _v_aux  = 0.f;        // raw Alt+PITCH selector value (0..1), readback for the MValue pickup
    bool  _aux_held = false;    // platform: Alt+PITCH selector currently held -> draw the selector
    float _peak   = 0.f;        // output peak for the ring level-meter render
};

};
