// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
//
// SKETCH - QSPI-target only. Compiles against libchuck.a (chuck.h on the include path, the shim
// sysroot + ck_prelude.h force-included) in the BOOT_QSPI build; not part of the SRAM engine bundle.
// See chuck_engine.h / docs/dev/chuck-impl.md.

#include "engine/chuck/chuck_engine.h"

// NOTE: we deliberately do NOT pull in the shim's ck_prelude.h here. That force-include exists so
// ChucK's *source* (compiled into libchuck.a) finds the POSIX functions it calls without including
// their headers; the engine TU only needs the ChucK *headers* to parse, and it already includes the
// Daisy stack (via iengine.h), whose newlib unistd.h declares usleep/getcwd/... - pulling in the
// prelude on top of that just yields conflicting redeclarations (usleep(unsigned) vs useconds_t). The
// ChucK feature defines (__DISABLE_THREADS__, __PLATFORM_LINUX__, ...) come from the Makefile chuck
// branch and MUST match the set libchuck.a was built with (ABI: they drop members from ChucK classes).
#include "chuck.h"            // provided by the QSPI build's -Ithirdparty/chuck/src/core
#include "chuck_globals.h"    // Chuck_Globals_Manager: setGlobalFloat (host -> .ck program)

#include <cstdlib>            // malloc/free (routed to the SDRAM pool by chuck_alloc.cpp's --wrap)

// Bring-up bisection (build: make engine-chuck CHUCKLVL=N). Splits ChucK init into stages so the
// first stage that fails to boot (panel goes solid-white instead of rendering) is the culprit:
//   0 = skip all (= NOCHUCK)         1 = new ChucK() + setParam only
//   2 = + ChucK::init() (loads built-in types/UGens)   3 = + compileCode() (the recursive parser) = full
#ifndef CHUCK_RUNTIME_LEVEL
#define CHUCK_RUNTIME_LEVEL 3
#endif
#ifdef CHUCK_SKIP_RUNTIME            // NOCHUCK=1 is the level-0 alias
#undef  CHUCK_RUNTIME_LEVEL
#define CHUCK_RUNTIME_LEVEL 0
#endif

namespace spotykach {

// Arms the SDRAM pool for ChucK's allocations (chuck_alloc.cpp). Called after _hw.Init() (so SDRAM is
// live), before `new ChucK()`. No-op-safe if the --wrap allocator isn't linked.
void chuck_heap_arm() noexcept;

// ---------------------------------------------------------------------------------------------
// Built-in fallback program. The engine compiles this when there is no SD .ck (everything, for now -
// the SD bank is M3). It mirrors Csound's kOrchestra: a single drone voice reading the panel globals,
// so a card-less unit always sounds. The globals it declares are the engine's param vocabulary - keep
// them in sync with channel_for() below; an SD program names the same globals to be driven by the same
// knobs. Globals default to 0 until the host writes them (set_param -> setGlobalFloat).
//
// SawOsc -> LPF -> dac drone: Speed sweeps the pitch, Size opens the filter, Mix sets the level (with
// a floor so there is always sound at Mix=0 on boot). A tight control loop (10::ms) repolls globals.
// ---------------------------------------------------------------------------------------------
static const char* kProgram = R"chuck(
global float speedA;
global float mixA;
global float sizeA;

SawOsc s => LPF f => dac;
while( true )
{
    110.0 + speedA * 770.0    => s.freq;     // Speed -> pitch (110..880 Hz)
    1500.0 + sizeA * 6000.0   => f.freq;     // Size  -> brightness (undriven => 1500 Hz)
    (0.15 + mixA * 0.85) * 0.3 => s.gain;    // Mix   -> level, with a floor so boot is audible
    10::ms => now;
}
)chuck";

// Map a platform ParamId (+ deck) to a ChucK global name + a cache slot, or nullptr for params this
// engine ignores. The 'A'/'B' suffix lets one program carry both decks. Eight slots per deck
// (kSlots=16). A program reads whichever of these globals it declares; undeclared writes are dropped
// by ChucK harmlessly. Mirrors CsoundEngine::channel_for so the two engines share a knob vocabulary.
static const char* global_for(ParamId id, DeckRef::Ref d, int& slot)
{
    const bool A = (d == DeckRef::A);
    const int  base = A ? 0 : 8;
    switch (id) {
        case ParamId::Speed:    slot = base + 0; return A ? "speedA"  : "speedB";
        case ParamId::Mix:      slot = base + 1; return A ? "mixA"    : "mixB";
        case ParamId::Size:     slot = base + 2; return A ? "sizeA"   : "sizeB";
        case ParamId::Env:      slot = base + 3; return A ? "envA"    : "envB";
        case ParamId::Feedback: slot = base + 4; return A ? "fbA"     : "fbB";
        case ParamId::ModSpeed: slot = base + 5; return A ? "modspA"  : "modspB";
        case ParamId::ModAmp:   slot = base + 6; return A ? "modampA" : "modampB";
        default:                slot = -1;       return nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------
void ChuckEngine::init(const EngineContext& ctx)
{
    _sr    = ctx.sample_rate;
    _block = ctx.block_size;
    if (_block > kMaxBlock) _block = kMaxBlock;   // run() scratch is sized for kMaxBlock frames

    // Route ChucK's allocations (the VM/UGen graph, MBs at create/compile) to the SDRAM pool in
    // chuck_alloc.cpp: the platform's default heap stays in SRAM (global ctors malloc before
    // _hw.Init() powers the FMC, so a heap in SDRAM would fault there). Arm AFTER _hw.Init() (we are
    // past it) and before `new ChucK()`. ctx.arena is unused - ChucK's heap comes from this pool.
    chuck_heap_arm();

    // Interleaved run() scratch, from the SDRAM pool (the platform heap stays in SRAM). Allocated once
    // here, never freed (the engine lives for the whole session).
    _inbuf  = static_cast<float*>(std::malloc(sizeof(float) * kMaxBlock * _in_ch));
    _outbuf = static_cast<float*>(std::malloc(sizeof(float) * kMaxBlock * _out_ch));

#if CHUCK_RUNTIME_LEVEL >= 1
    _ck = new ChucK();
    _ck->setParam(CHUCK_PARAM_SAMPLE_RATE,     static_cast<int>(_sr));
    _ck->setParam(CHUCK_PARAM_INPUT_CHANNELS,  _in_ch);
    _ck->setParam(CHUCK_PARAM_OUTPUT_CHANNELS, _out_ch);
    _ck->setParam(CHUCK_PARAM_OTF_ENABLE,      0);   // no on-the-fly server (bare metal: no sockets)
    _ck->setParam(CHUCK_PARAM_CHUGIN_ENABLE,   0);   // no dynamically-loaded UGen plugins (no dlopen)
#endif
#if CHUCK_RUNTIME_LEVEL >= 2
    _ck->init();                                     // builds the type system + registers built-in UGens
#endif
#if CHUCK_RUNTIME_LEVEL >= 3
    // Spork the built-in program. count=1, immediate=FALSE: queued, shreduled on the next VM step
    // (the first run()), matching the audio-thread compute model.
    _ck->compileCode(kProgram, "", 1);
    _ready = true;                                   // only now is run() safe to call from the ISR
#endif
}

void ChuckEngine::prepare()
{
    // Main-loop housekeeping (off the audio ISR). Nothing yet: the SD patch bank + live recompile
    // (M3) and the MIDI drain (M4) will live here, gated against the ISR. The built-in program runs
    // entirely inside process()'s run() calls.
}

void ChuckEngine::process(const float* const* in, float** out, size_t size)
{
    if (!_ready || !_inbuf || !_outbuf) {         // not fully initialised (or alloc fail) -> silence
        for (size_t i = 0; i < size; i++) { out[0][i] = 0.f; out[1][i] = 0.f; }
        _level *= 0.90f;
        return;
    }

    const size_t n = (size < static_cast<size_t>(kMaxBlock)) ? size : static_cast<size_t>(kMaxBlock);

    // De-interleaved platform in -> ChucK's interleaved input buffer (numFrames * _in_ch).
    const float* il = in ? in[0] : nullptr;
    const float* ir = in ? in[1] : nullptr;
    for (size_t i = 0; i < n; i++) {
        _inbuf[i * 2]     = il ? il[i] : 0.f;
        _inbuf[i * 2 + 1] = ir ? ir[i] : 0.f;
    }

    // One VM compute call: consumes _inbuf, advances every shred sample-accurately across n frames,
    // fills _outbuf interleaved (numFrames * _out_ch). SAMPLE == float, so no double marshalling.
    _ck->run(_inbuf, _outbuf, static_cast<long>(n));

    float peak = 0.f;
    for (size_t i = 0; i < n; i++) {              // interleaved _outbuf -> de-interleaved out
        const float l = _outbuf[i * 2];
        const float r = _outbuf[i * 2 + 1];
        out[0][i] = l;
        out[1][i] = r;
        const float a = (l < 0 ? -l : l), b = (r < 0 ? -r : r);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    // Peak meter for render(): fast attack, slow decay. Single-float write, read in the main loop.
    _level = (peak > _level) ? peak : _level * 0.90f;
}

Capabilities ChuckEngine::capabilities() const
{
    // CapOwnDisplay: the engine draws the rings (the level meter). CapAux (the patch selector) is
    // added with the SD bank in M3.
    return CapOwnDisplay;
}

void ChuckEngine::set_param(ParamId id, DeckRef::Ref d, float v)
{
    int slot = -1;
    const char* name = global_for(id, d, slot);
    if (slot >= 0 && slot < kSlots) _cache[slot] = v;
    if (!_ck || !name) return;
    // Called from the main loop while process() runs in the audio ISR. With __DISABLE_THREADS__ the
    // globals queue is drained on the audio thread, so this enqueues a write that the next run()
    // applies - the intended host -> .ck-program path (mirrors Csound's setControlChannel).
    _ck->globals()->setGlobalFloat(name, v);
}

float ChuckEngine::param(ParamId id, DeckRef::Ref d) const
{
    int slot = -1;
    global_for(id, d, slot);
    return (slot >= 0 && slot < kSlots) ? _cache[slot] : 0.f;
}

void ChuckEngine::render(DisplayModel& m)
{
    m.clear();
    const bool running = _ready;

    // Output level meter on both rings (green base, amber past ~-4 dB, red near clip). Same look as
    // CsoundEngine so the panel feedback is consistent across the two embedded engines.
    float lvl = _level;
    if (lvl > 1.f) lvl = 1.f;
    const uint32_t col = (lvl > 0.85f) ? 0xff2000u : (lvl > 0.60f) ? 0xffa000u : 0x00ff00u;
    for (int i = 0; i < 2; i++) {
        m.play[i] = { running ? 0x00ff00u : 0x000000u, running ? 1.f : 0.f };
        m.ring[i].set_hex_color(0x0a0a0a);              // faint full base ring
        m.ring[i].set_segment(0.f, 0.999f);
        if (running && lvl > 0.02f) {                   // bright arc proportional to level
            m.ring[i].set_hex_color(col);
            m.ring[i].set_segment(0.f, lvl);
        }
        m.ring[i].set_updated();
    }
    // Centre mode LED: white = the built-in program (the only source until the M3 SD bank lands).
    m.mode_center = { 0xffffffu, 0.5f };
}

} // namespace spotykach
