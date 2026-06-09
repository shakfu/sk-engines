// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/reverb/reverb_engine.h"
#include "engine/arena.h"

// The generated kernels. Each declares `class mydsp` inside namespace spotykach::rv_<name>; the
// faust_arch.h base types (dsp/UI/Meta, pulled in by these headers) live in the global namespace.
#include "engine/reverb/faust_kernel_dattorro.h"
#include "engine/reverb/faust_kernel_zita.h"

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
    void set(float v) const {
        const float x = lo + v * (hi - lo);
        if (z[0]) *z[0] = x;
        if (z[1]) *z[1] = x;
    }
};

// One (box,label) -> (role,slot) mapping. `section` null = match the label in any box; non-null =
// only inside that box (Dattorro reuses "Diffusion 1/2" in both its Input and Feedback boxes).
struct Bind { const char* section; const char* label; Knob role; int slot; };

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
            r.z[b.slot] = z; r.lo = lo; r.hi = hi;
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
    void set_knob(Knob k, float v) { role[k].set(v); }
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
            { nullptr, "Wet/Dry Mix", K_Mix,   0 },
            { nullptr, "Mid RT60",    K_Decay, 0 }, // main decay time
            { nullptr, "HF Damping",  K_Damp,  0 },
            { nullptr, "Low RT60",    K_Tone,  0 }, // low-band decay -> tonal character
            { nullptr, "In Delay",    K_SizeA, 0 }, // pre-delay
            { nullptr, "LF X",        K_SizeB, 0 }, // low/mid crossover frequency
        };
        FAUSTFLOAT* level = nullptr;
        CaptureUI ui; ui.roles = role; ui.binds = kBinds;
        ui.nbinds = static_cast<int>(sizeof(kBinds) / sizeof(kBinds[0])); ui.level_out = &level;
        dsp.buildUserInterface(&ui);
        if (level) *level = 0.f; // unity output (dB); EQ sections keep their flat defaults
    }
    void compute(FAUSTFLOAT** in, FAUSTFLOAT** out, int n) override { dsp.compute(n, in, out); }
    const char* name() const override { return "HALL"; }
};

// ParamId (physical knob) -> reverb role. Returns K_Count for ids this engine does not map.
Knob role_for(ParamId id) {
    switch (id) {
        case ParamId::Mix:    return K_Mix;   // SOS
        case ParamId::Pos:    return K_Decay; // POS
        case ParamId::Env:    return K_Damp;  // ENV
        case ParamId::Speed:  return K_Tone;  // PITCH
        case ParamId::Size:   return K_SizeA; // SIZE
        case ParamId::ModAmp: return K_SizeB; // MOD_AMT
        default:              return K_Count;
    }
}

} // namespace

void ReverbEngine::init(const EngineContext& ctx)
{
    Arena ar(ctx.arena);
    // Construct every reverb up front in the SDRAM arena (Dattorro ~126 KB, Zita ~937 KB of state).
    if (void* m = ar.alloc<uint8_t>(sizeof(DattorroVoice), alignof(DattorroVoice))) _rv[0] = new (m) DattorroVoice();
    if (void* m = ar.alloc<uint8_t>(sizeof(ZitaVoice),     alignof(ZitaVoice)))     _rv[1] = new (m) ZitaVoice();

    const int sr = static_cast<int>(ctx.sample_rate);
    for (auto* rv : _rv) if (rv) rv->init(sr);

    _active = 0;
    apply_all_knobs(); // seed the active reverb from the cached defaults
    _peak = 0.f;
}

void ReverbEngine::apply_all_knobs()
{
    ReverbVoice* rv = _rv[_active];
    if (!rv) return;
    for (int k = 0; k < K_Count; k++) rv->set_knob(static_cast<Knob>(k), _v[k]);
}

void ReverbEngine::process(const float* const* in, float** out, size_t size)
{
    ReverbVoice* rv = _rv[_active];
    if (!rv || !in || !in[0] || !in[1]) {
        if (out && out[0]) std::memset(out[0], 0, size * sizeof(float));
        if (out && out[1]) std::memset(out[1], 0, size * sizeof(float));
        return;
    }
    // FAUSTFLOAT == float; compute() treats inputs as const (RESTRICT), so the const_cast is safe.
    // compute() is allocation-free -> ISR-safe.
    FAUSTFLOAT* ins[2]  = { const_cast<float*>(in[0]), const_cast<float*>(in[1]) };
    FAUSTFLOAT* outs[2] = { out[0], out[1] };
    rv->compute(ins, outs, static_cast<int>(size));

    float peak = 0.f;
    for (size_t i = 0; i < size; i++) {
        peak = std::fmax(peak, std::fabs(out[0][i]));
        peak = std::fmax(peak, std::fabs(out[1][i]));
    }
    _peak = peak;
}

void ReverbEngine::set_param(ParamId id, DeckRef::Ref /*deck*/, float v)
{
    v = std::clamp(v, 0.f, 1.f);
    if (id == ParamId::Aux) {
        _v_aux = v;
        const int idx = std::min(std::max(static_cast<int>(v * (kReverbCount - 1) + 0.5f), 0), kReverbCount - 1);
        if (idx != _active && _rv[idx]) { _active = idx; apply_all_knobs(); }
        return;
    }
    const Knob k = role_for(id);
    if (k == K_Count) return;
    _v[k] = v;
    if (ReverbVoice* rv = _rv[_active]) rv->set_knob(k, v);
}

float ReverbEngine::param(ParamId id, DeckRef::Ref /*deck*/) const
{
    if (id == ParamId::Aux) return _v_aux;
    const Knob k = role_for(id);
    return (k == K_Count) ? 0.f : _v[k];
}

void ReverbEngine::set_aux_active(DeckRef::Ref /*deck*/, bool active) { _aux_held = active; }

void ReverbEngine::render(DisplayModel& m)
{
    m.clear();
    // Plate = cool blue, Hall = violet; the play pads carry the active-reverb colour.
    const uint32_t kColor[kReverbCount] = { 0x00aaffu, 0x8800ffu };
    const uint32_t color = kColor[_active];

    const float level = _peak > 1.f ? 1.f : _peak;
    for (int r = 0; r < 2; r++) {
        if (_aux_held) {
            // While Alt+PITCH is held, show the selector: one dot per reverb, the active one lit.
            m.ring[r].set_hex_color(color);
            for (int i = 0; i < kReverbCount; i++) {
                const float pos = (kReverbCount > 1) ? static_cast<float>(i) / (kReverbCount - 1) : 0.f;
                m.ring[r].add_point(pos, i == _active ? 1.f : 0.15f, true);
            }
        } else if (level > 1e-4f) {
            m.ring[r].set_hex_color(color);
            m.ring[r].set_segment(0.f, level * 0.999f);
        }
        m.ring[r].set_updated();
        m.play[r] = { color, 1.f };
    }
}

};
