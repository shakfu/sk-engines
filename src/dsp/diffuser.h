// SPDX-License-Identifier: GPL-3.0-only
//
// This file is GPLv3, NOT MIT like the rest of this repository. It is a port of qdelay's `Diffusor`
// (https://github.com/tiagolr/qdelay, (C) tilr, GPLv3; design credited there to TARON's MiniVerb).
// The allpass topology, the L/R coefficient tables, and the size->offset mapping are taken from it, so
// it is a derivative work and inherits qdelay's GPLv3. See src/engine/qdelay/{NOTICE.md,LICENSE}.
#pragma once

#include <cstddef>
#include <cmath>

namespace spotykach {

// A JUCE-free port of qdelay's Diffusor (a TARON MiniVerb-style allpass diffuser): kStages cascaded
// allpass filters per channel smear transients into a dense, reverb-like tail. The original
// (qdelay's src/dsp/Diffusor.h) used std::vector + JUCE's RCFilter; this is plain C++/STL math with
// NO heap in the audio path - each stage's delay line is sub-allocated from a single block of
// caller-provided memory (the SDRAM arena on target), so the platform owns the allocation.
//
// Per-stage delay length = round(coeff * distance), distance = (sr / 343) * 3.75 m (speed of sound).
// The L/R coefficient tables are intentionally detuned to widen the stereo image. At 48 kHz the two
// channels total ~58.6k floats (~234 KB); call capacity_floats(sr) to size the arena slice.
class Diffuser {
public:
    static constexpr int kStages = 8;

    // qdelay's fixed per-stage allpass distance coefficients (descending; L and R detuned for width).
    static constexpr float kCoeffL[kStages] = { 12.11f, 10.49f, 8.51f, 7.81f, 6.21f, 5.36f, 3.17f, 2.21f };
    static constexpr float kCoeffR[kStages] = { 12.08f, 10.47f, 8.49f, 7.77f, 6.23f, 5.33f, 3.71f, 2.12f };

    static float distance(float sr) { return (sr / 343.f) * 3.75f; }

    // Floats BOTH channels need at sample rate `sr` (sum of every stage's buffer size). The engine
    // passes a slice of at least this many floats to init().
    static size_t capacity_floats(float sr)
    {
        const float d = distance(sr);
        size_t n = 0;
        for (int i = 0; i < kStages; i++) n += static_cast<size_t>(kCoeffL[i] * d) + static_cast<size_t>(kCoeffR[i] * d);
        return n;
    }

    // Carve the per-stage delay lines from `mem` (must hold >= capacity_floats(sr) floats). `mem` may
    // be null (host arena exhausted / disabled) - process() then passes the signal through untouched.
    void init(float* mem, float sr)
    {
        _active = (mem != nullptr);
        if (!_active) return;
        const float d = distance(sr);
        float* p = mem;
        for (int i = 0; i < kStages; i++) { const int s = static_cast<int>(kCoeffL[i] * d); _l[i].init(p, s); p += s; }
        for (int i = 0; i < kStages; i++) { const int s = static_cast<int>(kCoeffR[i] * d); _r[i].init(p, s); p += s; }
    }

    // size01 0..1 -> the read-tap offset depth. qdelay maps it to (0.9 - 0.9*size): size up = shorter
    // offset = tighter/earlier diffusion, size down = longer offset = looser, more spread-out smear.
    void set_size(float size01)
    {
        const float depth = 0.9f - 0.9f * (size01 < 0.f ? 0.f : size01 > 1.f ? 1.f : size01);
        for (int i = 0; i < kStages; i++) { _l[i].set_size(depth); _r[i].set_size(depth); }
    }

    void set_smear(float s) { _smear = s; } // allpass feedback coefficient (qdelay default 0.75)

    void clear() { for (int i = 0; i < kStages; i++) { _l[i].clear(); _r[i].clear(); } }

    // Replace l/r with the fully diffused (wet) signal; the caller owns the dry/wet mix.
    inline void process(float& l, float& r)
    {
        if (!_active) return;
        float a = l, b = r;
        for (int i = 0; i < kStages; i++) { a = _l[i].process(a, _smear); b = _r[i].process(b, _smear); }
        l = a; r = b;
    }

private:
    // A single Schroeder allpass over a fixed buffer: out = buf[read] - in*fb; buf[write] = in + out*fb,
    // with a fractional (linearly interpolated) read tap `offset` ahead of the write index.
    struct AllPass {
        float* buf = nullptr;
        int    size = 0;
        float  offset = 0.f;
        int    pos = 0;

        void init(float* mem, int sz) { buf = mem; size = sz < 1 ? 1 : sz; pos = 0; clear(); }
        void set_size(float depth01) { offset = static_cast<float>(static_cast<int>(size * depth01)); }
        void clear() { if (buf) for (int i = 0; i < size; i++) buf[i] = 0.f; pos = 0; }

        inline float process(float in, float fb)
        {
            const float fp  = static_cast<float>(pos) + offset;
            int         ip  = static_cast<int>(std::floor(fp));
            const float frc = fp - static_cast<float>(ip);
            ip %= size;
            const int ip1 = (ip + 1) % size;
            const float out = (buf[ip] + (buf[ip1] - buf[ip]) * frc) - in * fb;
            buf[pos] = in + out * fb;
            pos = (pos + 1) % size;
            return out;
        }
    };

    AllPass _l[kStages];
    AllPass _r[kStages];
    float   _smear  = 0.75f;
    bool    _active = false;
};

};
