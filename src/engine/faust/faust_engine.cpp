// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/faust/faust_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace spotykach;

namespace {

// Captures the kernel's control-zone pointers by label during buildUserInterface. Faust keeps the
// param fields private and only hands out their addresses here, so this is the supported way to bind
// them. Bound once at init; the engine then writes params straight through the captured pointers.
struct ParamUI : public UI {
    FAUSTFLOAT** freq; FAUSTFLOAT** cut; FAUSTFLOAT** res; FAUSTFLOAT** gate;

    void bind(const char* label, FAUSTFLOAT* zone) {
        if      (std::strcmp(label, "freq")   == 0) *freq = zone;
        else if (std::strcmp(label, "cutoff") == 0) *cut  = zone;
        else if (std::strcmp(label, "res")    == 0) *res  = zone;
        else if (std::strcmp(label, "gate")   == 0) *gate = zone;
    }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, z); }
    void addVerticalSlider  (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, z); }
    void addNumEntry        (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, z); }
    void addButton          (const char* l, FAUSTFLOAT* z) override { bind(l, z); }
    void addCheckButton     (const char* l, FAUSTFLOAT* z) override { bind(l, z); }
};

} // namespace

void FaustEngine::init(const EngineContext& ctx)
{
    _dsp.init(static_cast<int>(ctx.sample_rate));

    ParamUI ui;
    ui.freq = &_z_freq; ui.cut = &_z_cut; ui.res = &_z_res; ui.gate = &_z_gate;
    _dsp.buildUserInterface(&ui); // capture the four control zones by label

    if (_z_gate) *_z_gate = 1.f;  // gate open at boot so the spike sounds when flashed (a drone)
    _peak = 0.f;
}

void FaustEngine::process(const float* const* /*in*/, float** out, size_t size)
{
    // The kernel has 0 inputs and 2 outputs; FAUSTFLOAT == float, so out maps straight to its
    // outputs[]. compute() is allocation-free (all state is fixed-size members) -> ISR-safe.
    _dsp.compute(static_cast<int>(size), nullptr, out);

    float peak = 0.f;
    for (size_t i = 0; i < size; i++) peak = std::fmax(peak, std::fabs(out[0][i]));
    _peak = peak;
}

// POS/PITCH/SOS knobs (Pos/Speed/Mix) and MOD_AMT (ModAmp) drive the kernel's controls. Any deck's
// knob writes the single voice (this spike is mono). Values are written through the captured zones.
void FaustEngine::set_param(ParamId id, DeckRef::Ref /*deck*/, float v)
{
    v = std::clamp(v, 0.f, 1.f);
    switch (id) {
        case ParamId::Speed:  if (_z_freq) *_z_freq = 40.f * std::exp2(v * 6.0f); break; // ~40..2560 Hz
        case ParamId::Mix:    if (_z_cut)  *_z_cut  = v; break;                          // cutoff (dsp scales *12 kHz)
        case ParamId::ModAmp: if (_z_res)  *_z_res  = v; break;                          // resonance
        default: break;
    }
}

// Play pad toggles the envelope gate (a crude mute/unmute for the drone). Return false: no buffer to
// be "empty", so suppress the platform's empty-flash.
bool FaustEngine::on_play_pad(DeckRef::Ref /*deck*/, bool /*reverse*/)
{
    if (_z_gate) *_z_gate = (*_z_gate > 0.5f) ? 0.f : 1.f;
    return false;
}

void FaustEngine::render(DisplayModel& m)
{
    m.clear();
    const float level = _peak > 1.f ? 1.f : _peak;
    for (int r = 0; r < 2; r++) {
        if (level > 1e-4f) {
            m.ring[r].set_hex_color(0xffaa00);
            m.ring[r].set_segment(0.f, level * 0.999f);
        }
        m.ring[r].set_updated();
        m.play[r] = { (_z_gate && *_z_gate > 0.5f) ? 0x00ff00u : 0x202020u, 1.f };
    }
}
