// faust_capture.h - the generic Faust zone-capture seam, shared by every Faust-backed engine.
//
// A cyfaust-generated kernel keeps its params PRIVATE; they are exposed only through
// buildUserInterface(UI*), which hands out one FAUSTFLOAT* "zone" per slider (with its label, init,
// min, max). CaptureUI walks that once at init() and records, for each binding in a per-engine Bind
// list, the matching zone pointer + its native range. The engine then writes a 0..1 platform value
// through Role::set(), which linear-maps into the slider's range. This is the idiomatic Faust embed
// pattern (the reverb/tape wrappers each had their own copy; this is the shared one).

#pragma once

#include "engine/faust_arch.h"  // ::dsp / ::UI / ::Meta base types + FAUSTFLOAT (global namespace)

#include <cstring>

namespace spotykach {
namespace faustgen {

// A control role bound to up to two Faust zones (one knob can drive two sliders), with the slider's
// native [lo,hi] and default captured at bind time. set(v01) maps the 0..1 platform value into [lo,hi]
// (inverted when the slider's high end is the "off" end). `def` is the slider's own default, normalized
// back to 0..1 so the engine can seed its param cache to match the kernel (no boot-time knob jump).
struct Role {
    FAUSTFLOAT* z[2] = { nullptr, nullptr };
    float lo = 0.f, hi = 1.f, def = 0.f;
    bool  inv = false;
    bool  bound() const { return z[0] != nullptr; }
    float def01() const {
        const float r = (hi > lo) ? (def - lo) / (hi - lo) : 0.f;
        const float c = r < 0.f ? 0.f : (r > 1.f ? 1.f : r);
        return inv ? 1.f - c : c;
    }
    void set(float v) const {
        const float x = inv ? hi - v * (hi - lo) : lo + v * (hi - lo);
        if (z[0]) *z[0] = x;
        if (z[1]) *z[1] = x;
    }
};

// One (box,label) -> (role,slot) binding. `section` null = match the label in any box; non-null = only
// inside that box (Faust reuses labels across boxes, e.g. Dattorro's "Diffusion 1/2"). `role` is an
// opaque index into the caller's Role[] table (the generated engine uses (int)ParamId). `invert` flips
// the knob->slider direction.
struct Bind { const char* section; const char* label; int role; int slot; bool invert; };

// Walks a kernel's buildUserInterface and fills roles[bind.role].z[bind.slot] (+ range/default) from the
// bind list, matching by label and box where required. Generic over the role index.
//
// CaptureDef gates the slider-default capture (Role::def). The generated FaustEngine needs it to seed its
// boot param cache (def01()); the hand-written reverb/tape wrappers don't read def, so they instantiate
// CaptureUI<false> and the `if constexpr` drops the store + the threaded init arg entirely - keeping their
// code byte-for-byte what the old per-engine CaptureUI emitted (reverb is SRAM_EXEC-bound).
template <bool CaptureDef = true>
struct CaptureUI : public UI {
    Role*        roles  = nullptr;
    const Bind*  binds  = nullptr;
    int          nbinds = 0;
    FAUSTFLOAT** level_out = nullptr;   // optional: capture a slider labelled "Level" here (the engine
                                        // then pins output gain to a fixed value); reverb uses it.
    const char*  section = "";

    void openTabBox       (const char* l) override { section = l; }
    void openHorizontalBox(const char* l) override { section = l; }
    void openVerticalBox  (const char* l) override { section = l; }

    void add(const char* label, FAUSTFLOAT* z, float init, float lo, float hi) {
        if (level_out && std::strcmp(label, "Level") == 0) { *level_out = z; return; }
        for (int i = 0; i < nbinds; i++) {
            const Bind& b = binds[i];
            if (std::strcmp(b.label, label) != 0) continue;
            if (b.section && std::strcmp(b.section, section) != 0) continue;
            Role& r = roles[b.role];
            r.z[b.slot] = z; r.lo = lo; r.hi = hi; r.inv = b.invert;
            if constexpr (CaptureDef) r.def = init;
        }
    }
    void addVerticalSlider  (const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, i, mn, mx); }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, i, mn, mx); }
    void addNumEntry        (const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT) override { add(l, z, i, mn, mx); }
};

} // namespace faustgen
} // namespace spotykach
