#pragma once

#include "engine/tape/faust_kernel_tapefx.h" // the cyfaust-generated kernel: tfx_tapefx::mydsp

#include <cstring>

namespace spotykach {

// Shared tape-FX kernel wrapper: wow/flutter (modulated fractional delay) -> Jiles-Atherton hysteresis
// -> post-FX resonant low-pass. The generated kernel is mono (1 in, 1 out); compute() is allocation-free
// and in-place safe (Faust reads each input sample into a local before writing the output). The ~16 KB
// delay buffer lives inside the object, so the whole struct is placement-new'd into the SDRAM arena.
// Knobs are written as normalized 0..1 values straight into the captured zones (the .dsp does the
// musical scaling). Used by the streaming `tape` engine (one per deck) and the buffer-based `shuttle`
// engine (one per track); the .dsp source + generated kernel live under src/engine/tape/.
struct TapeFx {
    tfx_tapefx::mydsp dsp;
    FAUSTFLOAT* z[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }; // drive, char, wow, rate, cutoff, reso

    struct Cap : UI {
        TapeFx* fx;
        void bind(const char* l, FAUSTFLOAT* zp) {
            if      (std::strcmp(l, "drive")  == 0) fx->z[0] = zp;
            else if (std::strcmp(l, "char")   == 0) fx->z[1] = zp;
            else if (std::strcmp(l, "wow")    == 0) fx->z[2] = zp;
            else if (std::strcmp(l, "rate")   == 0) fx->z[3] = zp;
            else if (std::strcmp(l, "cutoff") == 0) fx->z[4] = zp;
            else if (std::strcmp(l, "reso")   == 0) fx->z[5] = zp;
        }
        void addHorizontalSlider(const char* l, FAUSTFLOAT* zp, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, zp); }
        void addVerticalSlider  (const char* l, FAUSTFLOAT* zp, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, zp); }
    };

    void init(int sr) { dsp.init(sr); Cap ui; ui.fx = this; dsp.buildUserInterface(&ui); }
    void set(int which, float v) { if (z[which]) *z[which] = v; }       // which: 0..5, v in [0,1]
    void process(float* buf, int n) { FAUSTFLOAT* io[1] = { buf }; dsp.compute(n, io, io); } // in-place
};

} // namespace spotykach
