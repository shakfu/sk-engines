// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "engine/pstretch/pstretch_voice.h"
#include "nocopy.h"

#include <cstddef>
#include <cstdint>

namespace spotykach {

// pstretch - a real-time, clean-room PaulStretch ambient time-smear. Each deck (A/B) runs one mono
// Stretcher over its input channel (A = left, B = right, like the delay engine); the two are blended by
// the crossfader and placed by the routing switch. PaulStretch turns audio into a diffuse, evolving
// spectral wash by FFT-ing large overlapping windows and randomizing the phases; here the analysis head
// crawls through (or freezes on) a multi-second ring of live input, so big stretch settings smear the
// recent past into an ambient drone.
//
// Per-deck control map:
//   SIZE  -> STRETCH amount (1x .. 64x, exponential) - how slowly the read head crawls.
//   POS   -> DIFFUSION / smear (0 = clean window resynthesis .. 1 = full phase-randomized wash).
//   PITCH -> pitch shift of the grain (+/- 1 octave).
//   ENV   -> output tone (one-pole low-pass; ambient softening).
//   MIX   -> dry/wet.
//   Play pad -> FREEZE the read head (infinite evolving drone on the current spot).
//   Rev pad  -> CAPTURE/hold (latched): grab the recent input and loop the stretch *through* it, so big
//               SIZE traverses the captured phrase (the classic PaulStretch drone). Tap again for live.
//   Mix fader (Crossfade) -> A/B blend.   Routing switch -> stereo topology.   (Mode switch unused.)
//
// Self-contained: a vendored radix-2 FFT (engine/pstretch/fft.h), no CMSIS-DSP. The per-voice rings
// (~0.5 MB each) and the shared FFT/window scratch are sub-allocated from the injected SDRAM arena.
class PstretchEngine : public IEngine {
public:
    PstretchEngine() = default;
    ~PstretchEngine() override = default;

    void init(const EngineContext& ctx) override;
    void prepare() override {}
    void process(const float* const* in, float** out, size_t size) override;

    Capabilities capabilities() const override { return CapOwnDisplay | CapDualDeck; }

    void  set_param(ParamId id, DeckRef::Ref d, float v) override;
    float param(ParamId id, DeckRef::Ref d) const override;
    bool  set_config(ConfigId id, DeckRef::Ref, int value) override;
    Route route() const override { return _route; }

    bool  on_play_pad(DeckRef::Ref d, bool reverse) override;   // toggle freeze

    void  render(DisplayModel& m) override;

private:
    NOCOPY(PstretchEngine)

    static constexpr int      kWindow     = 4096;        // FFT / analysis window (~85 ms - smoother wash)
    static constexpr int      kHop        = kWindow / 2; // 50% overlap
    static constexpr uint32_t kRing       = 1u << 18;    // 262144 = ~5.46 s (live history / capture span)
    static constexpr size_t   kMaxFrames  = 128;
    static constexpr float    kCenterGain = 0.70710678f;
    // Pipelined-worker budget: max work TICKS (kChunk samples/bins or kChunk FFT butterflies each) the two
    // voices may do PER BLOCK, shared. A 4096 hop is ~126 ticks; its output lasts ~21 blocks, so two voices
    // need ~12 ticks/block - budget a little above that, low enough that one block never overruns. Meter-tuned.
    static constexpr int      kWorkBudget = 14;

    void _apply(DeckRef::Ref d);         // push cached params into a voice
    void _roll_random_pans();

    pstretch::FFT    _fft;
    pstretch::Shared _shared;
    pstretch::Stretcher _voice[2];

    // The FFT working set lives in on-chip SRAM (engine .bss), NOT the SDRAM arena: the transform's
    // scattered access to these (~128 KB total) dominates its cost, and scattered SDRAM access on the H7
    // is ~10x slower. Only the big per-voice input ring (~1 MB each) stays in the SDRAM arena - it is read
    // roughly sequentially, which SDRAM tolerates.
    float    _cos[kWindow / 2], _sin[kWindow / 2];   // FFT twiddle tables (shared, read-only)
    uint16_t _brev[kWindow];                          // FFT bit-reversal table (shared, read-only)
    float    _window[kWindow], _invnorm[kHop];        // window + 50%-overlap COLA gain (shared, read-only)
    static constexpr int kLut = 1024;                  // cos/sin LUT size for the phase smear (power of two)
    float    _lutc[kLut], _luts[kLut];                 // shared cos/sin table
    float    _re[2][kWindow], _im[2][kWindow];         // per-deck FFT scratch (the pipeline interleaves voices)
    float    _ola[2][kWindow];                         // per-deck overlap-add accumulator
    float    _fifo[2][2 * kHop];                        // per-deck output FIFO (holds up to 2 hops ahead)

    // Cached control values (0..1) for readback + the derived per-voice settings.
    float _stretch_n[2] = { 0.5f, 0.5f };   // SIZE
    float _diffuse_n[2] = { 1.0f, 1.0f };   // POS
    float _pitch_n[2]   = { 0.5f, 0.5f };   // PITCH (0.5 = unity)
    float _tone[2]      = { 1.f, 1.f };     // ENV -> one-pole LP coef
    float _lp[2]        = { 0.f, 0.f };     // tone filter state
    float _wet[2]       = { 1.f, 1.f };     // MIX (dry/wet)
    bool  _frozen[2]    = { false, false };
    bool  _captured[2]  = { false, false }; // Rev pad: capture/hold (loop the stretch through a grabbed span)

    Route _route = Route::Stereo;
    float _xfade = 0.5f, _gA = 1.f, _gB = 1.f;
    float _rndL[2] = { kCenterGain, kCenterGain };
    float _rndR[2] = { kCenterGain, kCenterGain };
    uint32_t _rng = 0x9e3779b9u;
};

} // namespace spotykach
