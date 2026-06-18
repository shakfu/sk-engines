// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"

// SKETCH (2026-06): CsoundEngine wraps a Csound 7 instance behind IEngine. It builds ONLY in the
// QSPI firmware target - code in QSPI flash, heap in SDRAM (the Csound port's custom linker script),
// stock Daisy v5.4 bootloader, linked against libcsound.a. It CANNOT link into the SRAM engine
// bundle: ~2 MB of code vs the 186 KB SRAM_EXEC budget, and the spotykach board only boots SRAM.
// See docs/dev/csound.md for the why and the build recipe.
//
// The mapping onto IEngine is small and clean:
//   init()      -> csoundCreate / SetHostAudioIO / options / CompileCSD / Start
//   process()   -> de-interleave in -> spin, csoundPerformKsmps, spout -> de-interleave out
//   set_param() -> csoundSetControlChannel(name, value)   (orchestra reads it with chnget)
// The orchestra (the .csd) defines the synthesis AND the control vocabulary; the platform's knobs
// drive whichever channels the orchestra names.

// Csound's opaque instance type, forward-declared so the contract pulls in no Csound headers
// (csound.h is included only in the .cpp, which exists only in the QSPI build).
typedef struct CSOUND_ CSOUND;

namespace spotykach {

class CsoundEngine : public IEngine {
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
    // Both rings show the output level (peak meter); the Play indicators show running state.
    void render(DisplayModel& m) override;
    // TODO: pads (on_play_pad -> schedule/stop instruments); per-knob ring markers.

private:
    // Create a Csound instance, apply the host-IO options, compile `text` (a CSD document) and start.
    // Leaves _cs valid + _ksmps set on success; destroys the instance and returns false on failure
    // (so a caller can retry with a different orchestra). See init().
    bool try_compile(const char* text, float block_size);

    CSOUND* _cs        = nullptr;   // null => compile/create failed; process() outputs silence
    float   _sr        = 48000.f;
    int     _ksmps     = 0;         // == the platform block size (set via --ksmps in init)
    float   _level     = 0.f;       // output peak meter (written in process(), read in render())
    bool    _patch_loaded = false;  // true => running an SD /csound/patch.csd; false => built-in

    // Cached 0..1 knob values for param() pickup readback, one slot per mapped (ParamId, deck).
    static constexpr int kSlots = 8;     // small fixed map; grow as the control vocabulary grows
    float _cache[kSlots] = {0.f};
};

} // namespace spotykach
