// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/iengine.h"
#include "engine/chuck/chuck_patch.h"        // kMaxChuckSlots (bank size) + the SD .ck selector
#include "engine/csound/csound_reload.h"     // ReloadGate: lock-free VM<->ISR handoff for live swap

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
// SCOPE: M3 adds the SD `.ck` patch bank + Alt+PITCH live swap (chuck_patch.h + the ReloadGate). The
// built-in kProgram is slot 0 of the bank and the always-available fallback. MIDI-in (M4) is the next
// step (mirrors how CsoundEngine grew). ChucK's live swap differs from Csound's: there is ONE
// persistent VM (`_ck`) - a reload is removeAllShreds() + compileCode(newText) on it, not a new
// instance. The ReloadGate still gates the VM out of the audio path during that mutation, because
// removeAllShreds/compileCode are processed inside run() and (under __DISABLE_THREADS__) the message
// queues are not lock-protected against a concurrent run(). See chuck_engine.cpp do_reload.

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

    // Alt+PITCH (CapAux) held on deck A: the patch selector. set_param(Aux) previews; this drives the
    // selector display + (on the held->release edge) commits a live recompile in prepare().
    void         set_aux_active(DeckRef::Ref d, bool active) override;

    // --- panel feedback -------------------------------------------------------------------------
    // Rings show the output level meter (or, while Alt is held, the patch selector); Play LEDs show
    // running state; the centre mode LED shows the program source (cyan = an SD slot, white = built-in).
    void render(DisplayModel& m) override;

private:
    // Build the session's ChucK VM with `text` compiled into it (new ChucK / setParam / init / start /
    // compileCode). Returns the instance, or nullptr on failure (compile error / throw - the partial VM
    // is deleted). Called once at init(); a reload reuses this VM (reset_vm + recompile), not a rebuild.
    ChucK* build_vm(const char* text);
    // Reset the live VM between patches: CK_MSG_CLEARVM via Chuck_VM::process_msg (SYNCHRONOUS) - removes
    // all shreds, clears the user type system (the per-compileCode accumulation), and clears globals.
    // This is what reclaims the old patch's memory so swapping is leak-free (removeAllShreds alone does
    // NOT free the compiled types/code). The canonical ChucK reset (cf. chuck-max's `reset`). Caller gates.
    void reset_vm();
    // Program text for an availability index (built-in or an SD slot). SD reads go into `buf`.
    const char* program_for(int avail_index, char* buf, bool* from_sd);
    // Re-probe /chuck/<n>.ck existence and rebuild the [built-in, present slots...] selectable list.
    void rescan_bank();
    // Gated swap for availability index `target` (main loop only): take the live VM, reset_vm() it (clear
    // the old patch), compileCode the target, reseed, publish. Falls back to the built-in if an SD patch
    // fails to compile. The reset_vm() clear is what makes sustained patch-swapping leak-free.
    void do_reload(int target);
    // Replay cached knob values into the freshly compiled program's globals (post-reload pickup).
    void reseed_globals();

    ReloadGate   _gate;             // owns the live VM pointer (handoff: main loop <-> audio ISR)
    ChucK* _ck    = nullptr;        // the persistent ChucK VM (one instance for the session)
    IStreamDeck* _stream = nullptr; // SD service, kept for bank rescans / patch reloads
    float  _sr    = 48000.f;
    float  _block = 256.f;          // platform block size; == run() numFrames per process() call
    int    _in_ch  = 2;
    int    _out_ch = 2;
    float  _peak_l = 0.f, _peak_r = 0.f;  // per-channel output peak (fast attack, slow decay)
    float  _rms_l  = 0.f, _rms_r  = 0.f;  // per-channel output RMS (smoothed) - the loudness meter
    bool   _patch_loaded = false;   // true => running an SD slot; false => the built-in

    // --- CPU-overrun safeguard (mute-and-wait) --------------------------------------------------
    // A patch whose ck->run() can't finish within the audio block would let the (highest-priority) audio
    // ISR saturate the CPU and starve the main loop, freezing the knobs + Alt+PITCH selector with no way
    // out. process() times run() via the DWT cycle counter; if it overruns the block for kOverrunBlocks
    // consecutive blocks, _panic latches: process() then outputs silence (skips run()), which frees the
    // CPU so the main loop + selector stay alive and the user can Alt+PITCH to another patch. The panic
    // is mute-and-wait (no auto-fallback to the built-in); do_reload() clears it on the next swap.
    bool     _panic       = false;  // latched: current patch muted for overrunning real-time
    uint32_t _overrun_n   = 0;      // consecutive overrunning blocks
    uint32_t _block_cycles = 0;     // run() budget in CPU cycles (est. at init, refined to the true
                                    // block period at runtime via the min inter-block gap)
    uint32_t _last_entry  = 0;      // DWT->CYCCNT at the previous process() entry
    static constexpr uint32_t kOverrunBlocks = 16;  // ~85 ms of sustained overrun before muting

    // Interleaved scratch buffers for ck->run(): ChucK exchanges interleaved float[numFrames*chans].
    // Allocated from the SDRAM pool at init() (NOT held in the engine object), so they cost the
    // SRAM-tight platform nothing - the same reason CsoundEngine keeps its spin/spout in libcsound's
    // SDRAM heap rather than in SRAM. Sized for the max block (256) x 2 channels; process() clamps.
    static constexpr int kMaxBlock = 256;
    float* _inbuf  = nullptr;   // [kMaxBlock * _in_ch], from the SDRAM pool
    float* _outbuf = nullptr;   // [kMaxBlock * _out_ch], from the SDRAM pool

    // --- patch bank / Alt+PITCH selection -------------------------------------------------------
    static constexpr int kMaxAvail = kMaxChuckSlots + 1;   // the built-in plus every present slot
    bool _present[kMaxChuckSlots] = {false};
    int  _avail_slot[kMaxAvail] = {-1};   // [0] = -1 (built-in); then the present slot numbers
    int  _avail_n   = 1;                  // count of selectable programs (>= 1: the built-in)
    int  _sel       = 0;                  // committed availability index (what's compiled)
    int  _sel_preview = 0;                // Alt+PITCH preview index (shown while held)
    bool _aux_held  = false;              // deck A Alt held -> draw the selector
    bool _rescan_pending = false;         // refresh the bank on the next prepare() (set on Alt press)
    bool _reload_pending = false;         // commit a patch change on the next prepare()
    int  _reload_target  = 0;             // availability index to compile on the pending reload

    // Boot auto-load of the first SD patch once the card mounts (~1 s after boot), independent of the
    // Alt selector. One-shot; the throttle keeps the rescan off every main-loop iteration.
    bool     _boot_loaded = false;
    uint16_t _probe_div   = 0;

    // Cached 0..1 knob values for param() pickup readback + post-reload reseed, per (ParamId, deck).
    static constexpr int kSlots = 16;     // 8 per deck
    float _cache[kSlots] = {0.f};

#ifdef METER
    // On-panel meter state (METER build only). _shreds is the live concurrent-shred count, sampled in
    // process() (right after run(), on the VM's own thread, so no race with the shreduler) and read by
    // render(); _meter_div throttles that (vector-building) poll off the per-block hot path.
    int      _shreds    = 0;
    uint16_t _meter_div = 0;
#endif
};

} // namespace spotykach
