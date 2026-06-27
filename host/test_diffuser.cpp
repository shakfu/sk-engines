// Headless test for the JUCE-free Diffuser (port of qdelay's allpass Diffusor). Exercised standalone
// over caller-provided memory - no engine, no platform. Checks:
//   1. An impulse is smeared into a decaying tail (energy spread past the input sample), finite/bounded.
//   2. clear() returns the diffuser to its initial state (identical response on a re-run).
//   3. set_size changes the response (the read-tap offset depth is live).
//   4. The detuned L/R coefficient tables decorrelate identical mono input (stereo width).
//   5. A null memory block degrades to a clean passthrough (no crash, signal untouched).
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/diffuser.h"

using namespace spotykach;

namespace {
int g_failures = 0;
void check(bool cond, const char* msg) { if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; } }

constexpr float kSr = 48000.f;
constexpr int   kN  = 8000; // a few thousand samples to capture the diffuse tail

bool  finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
float maxabs(const std::vector<float>& v) { float m = 0.f; for (float x : v) m = std::fmax(m, std::fabs(x)); return m; }
float energy(const std::vector<float>& v, int from) { float s = 0.f; for (int i = from; i < (int)v.size(); i++) s += v[i] * v[i]; return s; }
float sad(const std::vector<float>& a, const std::vector<float>& b) { float s = 0.f; for (size_t i = 0; i < a.size(); i++) s += std::fabs(a[i] - b[i]); return s; }

// Drive `d` with a unit impulse on both channels; capture L and R.
void run_impulse(Diffuser& d, std::vector<float>& l, std::vector<float>& r) {
    l.assign(kN, 0.f); r.assign(kN, 0.f);
    for (int i = 0; i < kN; i++) {
        float a = (i == 0) ? 1.f : 0.f, b = (i == 0) ? 1.f : 0.f;
        d.process(a, b);
        l[i] = a; r[i] = b;
    }
}
} // namespace

int main() {
    std::vector<float> mem(Diffuser::capacity_floats(kSr), 0.f);
    std::printf("diffuser: capacity=%zu floats (~%.0f KB) @ %.0f Hz\n",
                mem.size(), mem.size() * 4.f / 1024.f, kSr);

    // --- 1. Impulse -> smeared tail ---------------------------------------------------------------
    std::vector<float> l, r;
    {
        Diffuser d; d.init(mem.data(), kSr); d.set_size(0.5f);
        run_impulse(d, l, r);
        const float tail = energy(l, 64); // energy well after the impulse
        std::printf("impulse: maxabs=%.3f tail-energy(from 64)=%.4f\n", maxabs(l), tail);
        check(finite(l) && finite(r), "diffuser output finite");
        check(maxabs(l) < 4.0f, "diffuser output stays bounded");
        check(tail > 1e-3f, "an impulse is smeared into a decaying tail");
    }

    // --- 2. clear() restores the initial state ----------------------------------------------------
    {
        Diffuser d; d.init(mem.data(), kSr); d.set_size(0.5f);
        std::vector<float> a, b; run_impulse(d, a, b);
        d.clear();
        std::vector<float> c, e; run_impulse(d, c, e);
        std::printf("clear: SAD(run1,run2-after-clear)=%.6f\n", sad(a, c));
        check(sad(a, c) < 1e-4f, "clear() returns the diffuser to its initial state");
    }

    // --- 3. set_size changes the response ---------------------------------------------------------
    {
        Diffuser tight; tight.init(mem.data(), kSr); tight.set_size(0.9f);
        std::vector<float> a, b; run_impulse(tight, a, b);
        Diffuser loose; loose.init(mem.data(), kSr); loose.set_size(0.1f);
        std::vector<float> c, e; run_impulse(loose, c, e);
        std::printf("size: SAD(tight,loose)=%.2f\n", sad(a, c));
        check(sad(a, c) > 1.0f, "set_size changes the diffusion response");
    }

    // --- 4. Detuned L/R decorrelate identical mono input ------------------------------------------
    {
        std::printf("stereo: SAD(L,R)=%.2f\n", sad(l, r));
        check(sad(l, r) > 1.0f, "the L/R coefficient detune decorrelates the channels (width)");
    }

    // --- 5. Null memory -> clean passthrough ------------------------------------------------------
    {
        Diffuser d; d.init(nullptr, kSr); d.set_size(0.5f);
        float a = 0.37f, b = -0.42f;
        d.process(a, b);
        std::printf("null-mem: L=%.3f R=%.3f (expect 0.370/-0.420)\n", a, b);
        check(a == 0.37f && b == -0.42f, "null memory degrades to a clean passthrough");
    }

    if (g_failures == 0) { std::printf("OK: all diffuser checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
