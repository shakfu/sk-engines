// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/reverb/reverb_engine.h"
#include "engine/arena.h"

// The generated kernels. Each declares `class mydsp` inside namespace spotykach::rv_<name>; the
// faust_arch.h base types (dsp/UI/Meta, pulled in by these headers) live in the global namespace.
#include "engine/reverb/faust_kernel_dattorro.h"
#include "engine/reverb/faust_kernel_zita.h"

#if defined(SPK_REVERB_GIGAVERB)
#include "engine/gigaverb/_ext_daisy.h" // gigaverb_daisy::wrapper_* (gen~ export C interface)
#include "engine/gen/genlib_arena.h"    // genlib_arena_bind: point gen~'s allocator at our arena slice
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new> // placement new

namespace spotykach {

namespace {

// The six reverb-agnostic knob roles. Each concrete reverb binds these to its own Faust sliders; the
// platform's physical knobs map onto them (see ReverbEngine::set_param). Order matches ReverbEngine::_v[].
enum Knob { K_Mix = 0, K_Decay, K_Damp, K_Tone, K_SizeA, K_SizeB, K_Count };

// A knob role bound to up to two Faust control zones (Dattorro drives its two diffusion sliders from
// one knob). The 0..1 knob is linear-mapped into the slider's native [lo,hi] captured at bind time,
// so each reverb's ranges (Dattorro 0..1, Zita's RT60 seconds, damping Hz, ...) just work.
struct Role {
    FAUSTFLOAT* z[2] = { nullptr, nullptr };
    float lo = 0.f, hi = 1.f;
    bool  inv = false; // map v=1 -> lo instead of hi (for sliders whose high end is the "off" end)
    void set(float v) const {
        const float x = inv ? hi - v * (hi - lo) : lo + v * (hi - lo);
        if (z[0]) *z[0] = x;
        if (z[1]) *z[1] = x;
    }
};

// One (box,label) -> (role,slot) mapping. `section` null = match the label in any box; non-null =
// only inside that box (Dattorro reuses "Diffusion 1/2" in both its Input and Feedback boxes). `invert`
// flips the knob->slider direction: Zita's "Wet/Dry Mix" runs +1=dry..-1=wet, opposite Dattorro's
// "Dry/Wet Mix", so the wet knob must invert to keep "knob up = more wet" across algorithms.
struct Bind { const char* section; const char* label; Knob role; int slot; bool invert; };

// Generic zone-capture UI: walks the kernel's buildUserInterface, fills the Role table from a per-
// reverb Bind list (matching by label, and box where required), and captures "Level" separately so
// the engine can hold output gain fixed.
struct CaptureUI : public UI {
    Role*        roles;
    const Bind*  binds;
    int          nbinds;
    FAUSTFLOAT** level_out; // optional; nullptr if the reverb has no "Level"
    const char*  section = "";

    void openTabBox       (const char* l) override { section = l; }
    void openHorizontalBox(const char* l) override { section = l; }
    void openVerticalBox  (const char* l) override { section = l; }

    void add(const char* label, FAUSTFLOAT* z, float lo, float hi) {
        if (level_out && std::strcmp(label, "Level") == 0) { *level_out = z; return; }
        for (int i = 0; i < nbinds; i++) {
            const Bind& b = binds[i];
            if (std::strcmp(b.label, label) != 0) continue;
            if (b.section && std::strcmp(b.section, section) != 0) continue;
            Role& r = roles[b.role];
            r.z[b.slot] = z; r.lo = lo; r.hi = hi; r.inv = b.invert;
        }
    }
    void addVerticalSlider  (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, mn, mx); }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, mn, mx); }
    void addNumEntry        (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, mn, mx); }
};

} // namespace

// Base wrapper: holds the bound role table + a stable display name. compute()/init() are supplied by
// the per-kernel subclass below (each owns a concrete ::mydsp by value, so the big delay-line state
// lives wherever the object is placed - i.e. in the SDRAM arena).
struct ReverbVoice {
    Role role[K_Count];
    virtual ~ReverbVoice() = default;
    virtual void init(int sample_rate) = 0;
    virtual void compute(FAUSTFLOAT** in, FAUSTFLOAT** out, int n) = 0;
    virtual const char* name() const = 0;
    // virtual so a non-Faust voice (GigaverbVoice) can route knobs through its own param API instead
    // of the Role/zone table; Dattorro/Zita use this default.
    virtual void set_knob(Knob k, float v) { role[k].set(v); }
};

namespace {

// --- Dattorro plate ----------------------------------------------------------------------------
// "Diffusion 1/2" appear in both the Input and Feedback boxes; the section field disambiguates them.
struct DattorroVoice : ReverbVoice {
    rv_dattorro::mydsp dsp;
    void init(int sr) override {
        dsp.init(sr);
        static const Bind kBinds[] = {
            { "Input",    "Prefilter",   K_Tone,  0 },
            { "Input",    "Diffusion 1", K_SizeA, 0 },
            { "Input",    "Diffusion 2", K_SizeA, 1 },
            { "Feedback", "Diffusion 1", K_SizeB, 0 },
            { "Feedback", "Diffusion 2", K_SizeB, 1 },
            { "Feedback", "Decay Rate",  K_Decay, 0 },
            { "Feedback", "Damping",     K_Damp,  0 },
            { nullptr,    "Dry/Wet Mix", K_Mix,   0 }, // -1 dry .. +1 wet (linear-mapped from 0..1)
        };
        FAUSTFLOAT* level = nullptr;
        CaptureUI ui; ui.roles = role; ui.binds = kBinds;
        ui.nbinds = static_cast<int>(sizeof(kBinds) / sizeof(kBinds[0])); ui.level_out = &level;
        dsp.buildUserInterface(&ui);
        if (level) *level = -6.f; // hold output gain fixed (dB)
    }
    void compute(FAUSTFLOAT** in, FAUSTFLOAT** out, int n) override { dsp.compute(n, in, out); }
    const char* name() const override { return "PLATE"; }
};

// --- Zita-rev1 hall ----------------------------------------------------------------------------
struct ZitaVoice : ReverbVoice {
    rv_zita::mydsp dsp;
    void init(int sr) override {
        dsp.init(sr);
        static const Bind kBinds[] = {
            { nullptr, "Wet/Dry Mix", K_Mix,   0, true }, // zita: +1=dry..-1=wet, so invert for "up = wet"
            { nullptr, "Mid RT60",    K_Decay, 0 }, // main decay time
            { nullptr, "HF Damping",  K_Damp,  0 },
            { nullptr, "Low RT60",    K_Tone,  0 }, // low-band decay -> tonal character
            { nullptr, "In Delay",    K_SizeA, 0 }, // pre-delay
            { nullptr, "Eq1 Level",   K_SizeB, 0 }, // tail tone: low-mid peaking EQ (+/-15 dB, mid=flat)
        };
        FAUSTFLOAT* level = nullptr;
        CaptureUI ui; ui.roles = role; ui.binds = kBinds;
        ui.nbinds = static_cast<int>(sizeof(kBinds) / sizeof(kBinds[0])); ui.level_out = &level;
        dsp.buildUserInterface(&ui);
        if (level) *level = 0.f; // unity output (dB); Eq1 Level is a knob (SizeB), Eq2 + EQ freqs stay default
    }
    void compute(FAUSTFLOAT** in, FAUSTFLOAT** out, int n) override { dsp.compute(n, in, out); }
    const char* name() const override { return "HALL"; }
};

#if defined(SPK_REVERB_GIGAVERB)
// --- Gigaverb (gen~) ---------------------------------------------------------------------------
// A third voice whose DSP is the gen~ "gigaverb" export rather than a Faust kernel. gen~ exposes no
// control zones, so this voice ignores the Role/zone table and overrides set_knob to translate the
// six reverb roles into gen~ setparameter() calls by index (the same 8-param map gigaverb_engine.h
// uses standalone; the two params the panel does not drive - 3 early, 6 spread - keep gen~ defaults).
// The gen~ State (12 delay lines, ~1.59 MB) is allocated by wrapper_create() from the genlib bump
// allocator, which ReverbEngine::init points at a dedicated arena slice before this voice is built.
struct GigaverbVoice : ReverbVoice {
    void* _st = nullptr;
    long  _block;
    float _mix = 0.4f; // wet/dry, knob up = wetter (matches _v[K_Mix] default); see compute()
    explicit GigaverbVoice(long block) : _block(block) {}

    // Reverb role -> gen~ parameter index. K_Mix is NOT here: gigaverb has no wet/dry parameter (its
    // `dry` param only adds dry passthrough and cannot null the always-on wet tail), so this voice owns
    // the crossfade in compute() instead - keeping "knob up = wetter" consistent with Plate/Hall.
    static int gen_index(Knob k) {
        switch (k) {
            case K_Decay: return 4; // revtime
            case K_Damp:  return 1; // damping
            case K_Tone:  return 0; // bandwidth
            case K_SizeA: return 5; // roomsize
            case K_SizeB: return 7; // tail
            default:      return -1;
        }
    }
    static constexpr int kDryParam = 2; // gen~ "dry": pinned to 0 so the export is pure wet; mix is ours

    void init(int sr) override {
        _st = gigaverb_daisy::wrapper_create(static_cast<float>(sr), _block);
        if (_st) gigaverb_daisy::wrapper_set_param(_st, kDryParam, 0.f);
    }
    void set_knob(Knob k, float v) override {
        if (k == K_Mix) { _mix = v; return; }
        if (!_st) return;
        const int idx = gen_index(k);
        if (idx < 0) return;
        const float lo = gigaverb_daisy::wrapper_param_min(_st, idx);
        const float hi = gigaverb_daisy::wrapper_param_max(_st, idx);
        gigaverb_daisy::wrapper_set_param(_st, idx, lo + v * (hi - lo));
    }
    void compute(FAUSTFLOAT** in, FAUSTFLOAT** out, int n) override {
        if (!_st) return;
        gigaverb_daisy::wrapper_perform(_st, in, 2, out, 2, n); // out = pure wet (dry pinned to 0)
        const float w = _mix, d = 1.f - _mix; // crossfade wet against the dry input
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < n; i++) out[c][i] = w * out[c][i] + d * in[c][i];
    }
    const char* name() const override { return "GVERB"; }
};
#endif // SPK_REVERB_GIGAVERB

// ParamId (physical knob) -> reverb role. Returns K_Count for ids this engine does not map.
Knob role_for(ParamId id) {
    switch (id) {
        case ParamId::Mix:    return K_Mix;   // SOS
        case ParamId::Speed:  return K_Decay; // PITCH (biggest knob) -> Decay, the main character control
        case ParamId::Env:    return K_Damp;  // ENV
        case ParamId::Pos:    return K_Tone;  // POS   -> Tone
        case ParamId::Size:   return K_SizeA; // SIZE
        case ParamId::ModAmp: return K_SizeB; // MOD_AMT
        default:              return K_Count;
    }
}

} // namespace

void ReverbEngine::init(const EngineContext& ctx)
{
    Arena ar(ctx.arena);
    const int sr = static_cast<int>(ctx.sample_rate);
    // One full set of voices PER DECK so each deck's selected reverb has independent delay-line state
    // (DoubleMono runs both at once). Dattorro ~126 KB + Zita ~937 KB (+ gigaverb ~2 MB) per deck.
    for (int d = 0; d < DeckRef::Count; d++) {
        if (void* m = ar.alloc<uint8_t>(sizeof(DattorroVoice), alignof(DattorroVoice))) _rv[d][0] = new (m) DattorroVoice();
        if (void* m = ar.alloc<uint8_t>(sizeof(ZitaVoice),     alignof(ZitaVoice)))     _rv[d][1] = new (m) ZitaVoice();

#if defined(SPK_REVERB_GIGAVERB)
        // gen~ uses its own global bump allocator (resets to offset 0 on bind), so each gigaverb instance
        // needs a disjoint slab. The genlib data objects are allocated per-call (no global name registry),
        // so two instances are independent. Bind THIS deck's slab right before its voices' init() so
        // GigaverbVoice::init -> wrapper_create lands in it. ~1.59 MB measured; 2 MB leaves margin.
        static constexpr size_t kGigaverbArenaBytes = 2u * 1024u * 1024u;
        void* gv_slab = ar.alloc<uint8_t>(kGigaverbArenaBytes, 32);
        if (gv_slab) {
            if (void* m = ar.alloc<uint8_t>(sizeof(GigaverbVoice), alignof(GigaverbVoice)))
                _rv[d][2] = new (m) GigaverbVoice(static_cast<long>(ctx.block_size));
            genlib_arena_bind(gv_slab, kGigaverbArenaBytes);
        }
#endif

        for (int v = 0; v < kReverbCount; v++) if (_rv[d][v]) _rv[d][v]->init(sr);
        _active[d] = 0;
        apply_all_knobs(static_cast<DeckRef::Ref>(d));
        _peak[d] = 0.f;
    }
    _route = Route::Stereo;
}

void ReverbEngine::apply_all_knobs(DeckRef::Ref deck)
{
    ReverbVoice* rv = _rv[deck][_active[deck]];
    if (!rv) return;
    for (int k = 0; k < K_Count; k++) rv->set_knob(static_cast<Knob>(k), _v[deck][k]);
}

void ReverbEngine::process(const float* const* in, float** out, size_t size)
{
    if (!out || !out[0] || !out[1]) return;

    if (_route == Route::DoubleMono) {
        // Two independent MONO reverbs: in[d] -> deck d's selected voice -> out[d]. The Faust voices are
        // stereo, so each is fed mono (input fanned to both channels) with one output kept; the discarded
        // channel lands in a throwaway scratch. compute() is allocation-free -> ISR-safe.
        static constexpr size_t kMaxBlock = 128; // platform block is 96
        float scratch[kMaxBlock];
        const int n = static_cast<int>(size < kMaxBlock ? size : kMaxBlock);
        for (int d = 0; d < DeckRef::Count; d++) {
            ReverbVoice* rv  = _rv[d][_active[d]];
            const float* din = in ? in[d] : nullptr;
            if (!rv || !din) { std::memset(out[d], 0, size * sizeof(float)); _peak[d] = 0.f; continue; }
            FAUSTFLOAT* ins[2]  = { const_cast<float*>(din), const_cast<float*>(din) };
            FAUSTFLOAT* outs[2] = { out[d], scratch };
            rv->compute(ins, outs, n);
            float peak = 0.f;
            for (int i = 0; i < n; i++) peak = std::fmax(peak, std::fabs(out[d][i]));
            _peak[d] = peak;
        }
        return;
    }

    // Stereo / GenerativeStereo: ONE stereo voice (deck A's selection) reverberates the stereo pair.
    ReverbVoice* rv = _rv[DeckRef::A][_active[DeckRef::A]];
    if (!rv || !in || !in[0] || !in[1]) {
        std::memset(out[0], 0, size * sizeof(float));
        std::memset(out[1], 0, size * sizeof(float));
        _peak[0] = _peak[1] = 0.f;
        return;
    }
    FAUSTFLOAT* ins[2]  = { const_cast<float*>(in[0]), const_cast<float*>(in[1]) };
    FAUSTFLOAT* outs[2] = { out[0], out[1] };
    rv->compute(ins, outs, static_cast<int>(size));
    float peak = 0.f;
    for (size_t i = 0; i < size; i++) {
        peak = std::fmax(peak, std::fabs(out[0][i]));
        peak = std::fmax(peak, std::fabs(out[1][i]));
    }
    _peak[0] = _peak[1] = peak;
}

// Knobs and the mode switch act per deck. In a stereo route only deck A's voice is heard (deck B's
// strip updates an idle voice); in DoubleMono each deck drives its own side.
void ReverbEngine::set_param(ParamId id, DeckRef::Ref deck, float v)
{
    v = std::clamp(v, 0.f, 1.f);
    const Knob k = role_for(id);
    if (k == K_Count || deck >= DeckRef::Count) return;
    _v[deck][k] = v;
    if (ReverbVoice* rv = _rv[deck][_active[deck]]) rv->set_knob(k, v);
}

float ReverbEngine::param(ParamId id, DeckRef::Ref deck) const
{
    const Knob k = role_for(id);
    if (k == K_Count || deck >= DeckRef::Count) return 0.f;
    return _v[deck][k];
}

// ConfigId::Route sets the process() topology; ConfigId::Mode (the Reel/Slice/Drift switch) selects the
// voice per deck. Both decks' switches are tracked even in a stereo route (deck B's just isn't heard
// until DoubleMono). Route wire values: 0=Stereo, 1=DoubleMono, 2=GenerativeStereo (see core.ui.cpp).
bool ReverbEngine::set_config(ConfigId id, DeckRef::Ref deck, int value)
{
    if (id == ConfigId::Route) {
        const Route r = value == 2 ? Route::GenerativeStereo : value == 1 ? Route::DoubleMono : Route::Stereo;
        if (r == _route) return false;
        _route = r;
        return true;
    }
    if (id != ConfigId::Mode || deck >= DeckRef::Count) return false;
    const int idx = std::clamp(value, 0, kReverbCount - 1);
    if (idx == _active[deck] || !_rv[deck][idx]) return false;
    _active[deck] = idx;
    apply_all_knobs(deck); // re-seed this deck's now-active reverb from its cached knob values
    return true;
}

void ReverbEngine::render(DisplayModel& m)
{
    m.clear();
    // Plate = cool blue, Hall = violet, Gigaverb = mint.
#if defined(SPK_REVERB_GIGAVERB)
    const uint32_t kColor[kReverbCount] = { 0x00aaffu, 0x8800ffu, 0x00ff88u };
#else
    const uint32_t kColor[kReverbCount] = { 0x00aaffu, 0x8800ffu };
#endif
    // The 3 mode LEDs (left/center/right) mirror the platform convention: they show the ROUTE.
    DisplayModel::Indicator* mode_led[3] = { &m.mode_left, &m.mode_center, &m.mode_right };
    const int route_led = (_route == Route::DoubleMono) ? 0 : (_route == Route::GenerativeStereo) ? 2 : 1;
    *mode_led[route_led] = { 0xffffffu, 0.8f };

    // Per-deck ring level + play colour in each deck's selected-voice colour. In a stereo route both
    // sides show deck A's (single) voice; in DoubleMono each shows its own.
    const bool dual = (_route == Route::DoubleMono);
    for (int d = 0; d < DeckRef::Count; d++) {
        const uint32_t color = kColor[_active[dual ? d : DeckRef::A]];
        const float level = _peak[d] > 1.f ? 1.f : _peak[d];
        if (level > 1e-4f) {
            m.ring[d].set_hex_color(color);
            m.ring[d].set_segment(0.f, level * 0.999f);
        }
        m.ring[d].set_updated();
        m.play[d] = { color, 1.f };
    }
}

};
