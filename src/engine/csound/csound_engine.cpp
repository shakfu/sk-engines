// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
//
// SKETCH - QSPI-target only. Compiles against libcsound.a (csound.h on the include path) in the
// BOOT_QSPI build; not part of the SRAM engine bundle. See csound_engine.h / docs/dev/csound.md.

#include "engine/csound/csound_engine.h"
#include "engine/csound/csound_patch.h"   // select_orchestra(): SD /csound/patch.csd, built-in fallback

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "csound.h"   // provided by the QSPI build's -I.../Daisy/include/csound

namespace spotykach {

// Arms the SDRAM bump pool for Csound's allocations (csound_alloc.cpp). Called after _hw.Init()
// (so SDRAM is live), before csoundCreate. No-op-safe if the --wrap allocator isn't linked.
void csound_heap_arm() noexcept;

// ---------------------------------------------------------------------------------------------
// Built-in fallback orchestra. The engine prefers an SD patch (/csound/patch.csd via select_orchestra
// in csound_patch.h) and uses this when there is no card/file, the file isn't a CSD, or it fails to
// compile - so a card-less unit always makes sound. The control channels it reads with chnget are the
// engine's param vocabulary - keep them in sync with channel_for() below; an SD patch is free to name
// the same channels (speedA/mixA/sizeA/envA + B) to be driven by the same knobs.
//
// Instruments are triggered with schedule() IN the orchestra, NOT a score `i` event (a score event
// did not fire on-target). Oscillators are table-less (vco2) to dodge the ftgen-at-init silence.
// ---------------------------------------------------------------------------------------------
static const char* kOrchestra = R"csd(
<CsoundSynthesizer>
<CsInstruments>
sr     = 48000
0dbfs  = 1
nchnls = 2

instr 1
  kspeed chnget "speedA"
  kmix   chnget "mixA"
  ksize  chnget "sizeA"
  kfreq  = 110 + kspeed * 770
  kcut   = 1500 + ksize * 6000   ; default bright; undriven channels read 0
  kamp   = 0.15 + kmix * 0.85    ; floor so there's always sound (Mix=0 on boot)
  asig   vco2 0.3, kfreq         ; verified table-less oscillator
  asig   tone asig, kcut         ; core one-pole lowpass (Size -> brightness)
  outs   asig * kamp, asig * kamp
endin

schedule(1, 0, 100000)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
)csd";

// Map a platform ParamId (+ deck) to an orchestra control-channel name. Returns the channel and a
// cache slot, or nullptr for params this orchestra ignores. The 'A'/'B' suffix lets one orchestra
// carry both decks; a single-voice orchestra can ignore the deck.
static const char* channel_for(ParamId id, DeckRef::Ref d, int& slot)
{
    const int base = (d == DeckRef::A) ? 0 : 4;   // 4 mapped params per deck -> kSlots=8
    switch (id) {
        case ParamId::Speed: slot = base + 0; return (d == DeckRef::A) ? "speedA" : "speedB";
        case ParamId::Mix:   slot = base + 1; return (d == DeckRef::A) ? "mixA"   : "mixB";
        case ParamId::Size:  slot = base + 2; return (d == DeckRef::A) ? "sizeA"  : "sizeB";
        case ParamId::Env:   slot = base + 3; return (d == DeckRef::A) ? "envA"   : "envB";
        default:             slot = -1;       return nullptr;
    }
}

// Scratch buffer size for an SD-loaded orchestra. A CSD patch is a few KB; 64 KB is generous head-
// room. It is malloc'd from the (now armed) SDRAM pool, used only during compile, then freed.
static constexpr int kPatchMax = 64 * 1024;

bool CsoundEngine::try_compile(const char* text, float block_size)
{
    _cs = csoundCreate(nullptr, nullptr);
    if (!_cs) return false;

    csoundSetHostAudioIO(_cs);                 // host owns I/O; we feed spin / drain spout
    csoundSetOption(_cs, "-n");                 // no Csound-managed audio device
    csoundSetOption(_cs, "-dm0");               // no displays, no messages

    // Tie Csound's k-cycle to the platform block so process() performs exactly once per block.
    // The QSPI build should use a block of >=128 (256 is proven): at ksmps=32 Csound's per-cycle
    // overhead overruns the CPU.
    char opt[24];
    std::snprintf(opt, sizeof(opt), "--ksmps=%d", static_cast<int>(block_size));
    csoundSetOption(_cs, opt);

    if (csoundCompileCSD(_cs, text, 1, 0) != 0) {
        csoundDestroy(_cs);                    // free the partial instance (the pool reclaims it now)
        _cs = nullptr;                         // failed -> caller may retry, else process() is silent
        return false;
    }
    csoundStart(_cs);
    _ksmps = csoundGetKsmps(_cs);              // == block size; guards the process() copy
    return true;
}

void CsoundEngine::init(const EngineContext& ctx)
{
    _sr = ctx.sample_rate;

    // Route Csound's allocations (MBs at create/compile) to the SDRAM pool in csound_alloc.cpp: the
    // platform's default heap stays in SRAM (global ctors malloc before _hw.Init() powers up the FMC,
    // so a heap in SDRAM would fault there). Arm AFTER _hw.Init() (we're past it) and before
    // csoundCreate. ctx.arena is unused - Csound's heap comes from this pool.
    csound_heap_arm();

    // Orchestra source: an SD patch (/csound/patch.csd) if present + valid, else the built-in
    // kOrchestra. The scratch is carved from the armed SDRAM pool and freed right after compile -
    // csoundCompileCSD parses the text in-call and does not retain it, and free() reclaims now (#2).
    char* buf = static_cast<char*>(std::malloc(kPatchMax));
    bool  from_sd = false;
    const char* orc = select_orchestra(ctx.stream, buf, buf ? kPatchMax : 0, kOrchestra, &from_sd);

    bool ok = try_compile(orc, ctx.block_size);
    if (!ok && from_sd) {                      // a bad SD patch must not cost audio: use the built-in
        from_sd = false;
        ok = try_compile(kOrchestra, ctx.block_size);
    }
    if (buf) std::free(buf);
    _patch_loaded = ok && from_sd;             // _cs is null when !ok -> process() outputs silence
}

void CsoundEngine::prepare()
{
    // Main-loop housekeeping. TODO: drain Csound's message queue here if we ever enable messages,
    // and service SD-driven orchestra (re)loads. No-op while the orchestra is compiled-in.
}

void CsoundEngine::process(const float* const* in, float** out, size_t size)
{
    if (!_cs || _ksmps <= 0) {                 // create/compile failed -> output silence
        for (size_t i = 0; i < size; i++) { out[0][i] = 0.f; out[1][i] = 0.f; }
        return;
    }

    const size_t n = (size < static_cast<size_t>(_ksmps)) ? size : static_cast<size_t>(_ksmps);
    MYFLT*       spin  = csoundGetSpin(_cs);    // ksmps*nchnls, interleaved
    const MYFLT* spout = csoundGetSpout(_cs);

    const float* il = in ? in[0] : nullptr;
    const float* ir = in ? in[1] : nullptr;
    for (size_t i = 0; i < n; i++) {            // de-interleaved in -> interleaved spin
        spin[i * 2]     = il ? static_cast<MYFLT>(il[i]) : 0;
        spin[i * 2 + 1] = ir ? static_cast<MYFLT>(ir[i]) : 0;
    }

    csoundPerformKsmps(_cs);                    // one k-cycle: consumes spin, fills spout

    float peak = 0.f;
    for (size_t i = 0; i < n; i++) {            // interleaved spout -> de-interleaved out
        const float l = static_cast<float>(spout[i * 2]);
        const float r = static_cast<float>(spout[i * 2 + 1]);
        out[0][i] = l;
        out[1][i] = r;
        const float a = (l < 0 ? -l : l), b = (r < 0 ? -r : r);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    // Peak meter for render(): fast attack, slow decay. Single-float write, read in the main loop.
    _level = (peak > _level) ? peak : _level * 0.90f;
}

Capabilities CsoundEngine::capabilities() const
{
    // Minimal for the sketch: just the audio path + params. Add CapAux (SD-slot .csd selection),
    // CapOwnDisplay (render the running instrument), etc. as the engine grows.
    return 0;
}

void CsoundEngine::set_param(ParamId id, DeckRef::Ref d, float v)
{
    if (!_cs) return;
    int slot = -1;
    const char* chan = channel_for(id, d, slot);
    if (!chan) return;
    // NOTE: called from the main loop while process() performs in the audio ISR. A control-channel
    // write racing a chnget read is a benign single-value race (Csound's intended host->orchestra
    // path); no torn structure. Cache for param() pickup readback.
    csoundSetControlChannel(_cs, chan, static_cast<MYFLT>(v));
    if (slot >= 0 && slot < kSlots) _cache[slot] = v;
}

float CsoundEngine::param(ParamId id, DeckRef::Ref d) const
{
    int slot = -1;
    channel_for(id, d, slot);
    return (slot >= 0 && slot < kSlots) ? _cache[slot] : 0.f;
}

void CsoundEngine::render(DisplayModel& m)
{
    m.clear();

    const bool  running = (_cs != nullptr);
    float       lvl     = _level;
    if (lvl > 1.f) lvl = 1.f;

    // Output level meter on both rings: green base, amber past -4 dBish, red near clip.
    const uint32_t col = (lvl > 0.85f) ? 0xff2000u : (lvl > 0.60f) ? 0xffa000u : 0x00ff00u;
    for (int i = 0; i < 2; i++) {
        m.play[i] = { running ? 0x00ff00u : 0x000000u, running ? 1.f : 0.f };

        m.ring[i].set_hex_color(0x0a0a0a);          // faint full base ring
        m.ring[i].set_segment(0.f, 0.999f);
        if (running && lvl > 0.02f) {               // bright arc proportional to level
            m.ring[i].set_hex_color(col);
            m.ring[i].set_segment(0.f, lvl);
        }
        m.ring[i].set_updated();
    }

    // Centre mode LED doubles as a patch-source tell: cyan = running an SD /csound/patch.csd,
    // white = the compiled-in built-in orchestra.
    m.mode_center = { _patch_loaded ? 0x00c0ffu : 0xffffffu, 0.5f };
}

} // namespace spotykach
