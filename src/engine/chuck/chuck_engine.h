// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"

// SKETCH (2026-06, M2): ChuckEngine wraps a ChucK runtime (the language/VM/UGens) behind IEngine,
// modelled directly on CsoundEngine. Like Csound, it builds ONLY in the QSPI firmware target - the
// ChucK core links ~1.1 MB of .text (far over the 186 KB SRAM_EXEC budget), so code lives in QSPI
// flash and the VM heap in SDRAM (alt_qspi.lds + the chuck_alloc.cpp --wrap pool); it cannot link
// into the SRAM engine bundle. See docs/dev/chuck-impl.md for the roadmap and the build recipe.
//
// The mapping onto IEngine is small and clean (the same shape as Csound):
//   init()      -> new ChucK / setParam(SR/INPUT/OUTPUT_CHANNELS) / init() / compileCode(program)
//   process()   -> interleave in -> ck->run(in,out,n) (SAMPLE=float) -> de-interleave out
//   set_param() -> globals()->setGlobalFloat(name, value)   (the .ck program reads the global)
//   render()    -> output-level meter on the rings
// The ChucK program (the .ck text) defines the synthesis AND the control vocabulary; the platform's
// knobs drive whichever globals the program declares (speedA/mixA/sizeA/... - see channel_for()).
//
// SCOPE: this is the M2 skeleton - the built-in program reading the panel globals. The SD .ck patch
// bank + Alt+PITCH live swap (M3) and MIDI-in (M4) are not wired yet (mirrors how CsoundEngine grew).

// ChucK's host class, forward-declared so the contract pulls in no ChucK headers (chuck.h is included
// only in the .cpp, which exists only in the QSPI build).
class ChucK;

namespace spotykach {

class ChuckEngine : public IEngine {
public:
    // --- required audio lifecycle ---------------------------------------------------------------
    void init(const EngineContext& ctx) override;
    void prepare() override;
    void process(const float* const* in, float** out, size_t size) override;

    // --- control --------------------------------------------------------------------------------
    Capabilities capabilities() const override;
    void         set_param(ParamId id, DeckRef::Ref d, float v) override;
    float        param(ParamId id, DeckRef::Ref d) const override;

    // --- panel feedback -------------------------------------------------------------------------
    // Rings show the output level meter; Play LEDs show running state; the centre mode LED shows the
    // program source (white = the built-in; cyan reserved for an SD slot once M3 lands).
    void render(DisplayModel& m) override;

private:
    ChucK* _ck    = nullptr;        // the live ChucK instance (single-threaded; no live swap yet)
    bool   _ready = false;          // true only after init()+compileCode() succeed: gate run() / "running"
    float  _sr    = 48000.f;
    float  _block = 256.f;          // platform block size; == run() numFrames per process() call
    int    _in_ch  = 2;
    int    _out_ch = 2;
    float  _level = 0.f;            // output peak meter (written in process(), read in render())

    // Interleaved scratch buffers for ck->run(): ChucK exchanges interleaved float[numFrames*chans].
    // Allocated from the SDRAM pool at init() (NOT held in the engine object), so they cost the
    // SRAM-tight platform nothing - the same reason CsoundEngine keeps its spin/spout in libcsound's
    // SDRAM heap rather than in SRAM. Sized for the max block (256) x 2 channels; process() clamps.
    static constexpr int kMaxBlock = 256;
    float* _inbuf  = nullptr;   // [kMaxBlock * _in_ch], from the SDRAM pool
    float* _outbuf = nullptr;   // [kMaxBlock * _out_ch], from the SDRAM pool

    // Cached 0..1 knob values for param() pickup readback, per (ParamId, deck). 8 slots per deck.
    static constexpr int kSlots = 16;
    float _cache[kSlots] = {0.f};
};

} // namespace spotykach
