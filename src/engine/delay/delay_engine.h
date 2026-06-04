// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "nocopy.h"

#include <cstddef>

namespace spotykach {

// A tempo-synced stereo delay - the second engine to consume the platform transport (proving the
// shared clock beyond granular). Two independent delay lines (left = deck A, right = deck B), each on
// the platform's direct (no-modifier) knobs: SIZE -> musical division (the delay time locks to the
// transport tempo, stepping through 1/2 .. 1/16T incl. dotted + triplets), POS -> feedback, SOS ->
// wet/dry mix, PITCH -> transpose the taps (+/-1 octave, centre = unity) via a crossfading pitch
// shifter on the wet output. The platform clock is injected read-only via EngineContext (ITransport);
// no recording/tape/sequencer - so capabilities() is just CapOwnDisplay | CapDualDeck.
//
// Buffer note (drives the future EngineBuffers generalization): the delay needs a big chunk of float
// SDRAM but EngineContext only offers the granular-shaped `source` buffers. It treats source[deck] as
// opaque float memory with a fixed safe cap (the 42 s granular loop buffer dwarfs a few seconds of
// delay), so the engine stays granular-free. A real generalization would have EngineBuffers expose a
// typed/byte-sized arena the engine sub-allocates; deferred until more engines need it.
class DelayEngine : public IEngine {
public:
    DelayEngine() = default;
    ~DelayEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override { return CapOwnDisplay | CapDualDeck; }

    void  set_param(ParamId id, DeckRef::Ref deck, float value) override;
    float param(ParamId id, DeckRef::Ref deck) const override;

    void render(DisplayModel& m) override;

private:
    NOCOPY(DelayEngine)

    // One delay line + its smoothed controls. process() runs per sample on the audio path.
    struct Tap {
        float* buf   = nullptr;  // delay memory (borrowed from the SDRAM arena)
        size_t len   = 0;        // usable float capacity
        size_t w     = 0;        // write index
        float  sr    = 48000.f;  // sample rate (delay time is derived from tempo + sr)
        float  min_d = 1.f;      // delay range in samples (set from sample rate / buffer length)
        float  max_d = 1.f;
        // Tempo-synced delay time: SIZE picks a musical division (`div` index into kDivBeats ->
        // `beats` quarter-note beats); target_d = sr*(60/bpm)*beats, recomputed per block by the
        // engine and one-pole smoothed so a tempo change glides.
        int    div   = 0;
        float  beats = 0.25f;
        float  target_d = 1.f;
        // Other controls (normalised 0..1) + per-sample one-pole smoothed working values.
        float  fb = 0.f, mix = 0.f, ratio = 1.f;
        float  s_delay = 1.f, s_fb = 0.f, s_mix = 0.f, s_ratio = 1.f;
        float  peak = 0.f;       // decayed input peak, for the display

        // Crossfading delay-line pitch shifter on the wet output (PITCH knob). Two read heads half a
        // window apart with a raised-cosine crossfade, so wraparound is seamless; bypassed at unity.
        static constexpr size_t kPSWin = 2048;
        float  ps[kPSWin] = {};
        size_t pw = 0;
        float  phase = 0.f;

        void  init(void* mem, float sample_rate, size_t length);
        void  set_div(float norm);  // SIZE 0..1 -> musical-division index + beats
        void  set_target(float bpm); // recompute target_d from bpm (per block)
        float process(float x);     // returns the wet/dry-mixed output
        float pitch(float x);       // pitch-shift one sample by s_ratio
        float readps(float off) const; // fractional read `off` samples behind the pitch-buffer write
    };

    static DeckRef::Ref _safe(DeckRef::Ref d) { return d < DeckRef::Count ? d : DeckRef::A; }

    const ITransport* _transport = nullptr; // platform clock: delay time syncs to its tempo
    Tap   _tap[DeckRef::Count];
    float _param[static_cast<size_t>(ParamId::Count)][DeckRef::Count] = {};
};

};
