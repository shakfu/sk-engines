// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstddef>
#include "core/engine_context.h"

namespace spotykach {

// IEngine is the contract between the fixed hardware/UI platform and a swappable DSP engine.
// A "firmware variant" is a different IEngine implementation behind the same platform.
//
// Phase 2 (current) makes only the audio/lifecycle subset active: the audio path now flows
// app -> IEngine -> engine -> Core. The UI/storage still reach the concrete engine's Core
// directly (escape hatch) until the interaction layer is migrated.
//
// Phase 3+ target (NOT yet active - kept here as the design contract): the platform drives
// the engine entirely through declared parameters/bindings/capabilities plus events and a
// display model, with no direct Core access:
//
//   virtual void  processCV(float* l, float* r, size_t n)   = 0; // DAC, block-level
//   virtual void  onControl(ControlId, float norm)          = 0;
//   virtual void  onGesture(const Gesture&)                 = 0;
//   virtual void  onCV(CvId, float v)                       = 0;
//   virtual void  onTransport(const TransportEvent&)        = 0;
//   virtual void  onMidi(const MidiMessage&)                = 0;
//   virtual const ParamTable&    parameters()  const        = 0;
//   virtual const BindingTable&  bindings()    const        = 0; // (Control,Modifier)->ParamId
//   virtual Capabilities         capabilities() const       = 0; // {Recording,TapeStorage,...}
//   virtual float param(ParamId) const                      = 0;
//   virtual void  set_param(ParamId, float)                 = 0; // engine resolves mode-dep.
//   virtual void  render(DisplayModel&) const               = 0;
class IEngine {
public:
    virtual ~IEngine() = default;

    // Allocate/initialise from the injected platform context (buffers, sample rate, clock).
    virtual void init(const EngineContext& ctx) = 0;

    // Non-real-time, main-loop housekeeping.
    virtual void prepare() = 0;

    // Real-time audio block (audio ISR). Keep per-sample work inside non-virtual engine code.
    virtual void process(const float* const* in, float** out, size_t size) = 0;
};

};
