// Headless test for the pstretch engine (real-time clean-room PaulStretch ambient time-smear).
// Two layers: (A) the vendored radix-2 FFT (round-trip identity + a known single-bin spectrum), and
// (B) the PstretchEngine through its public IEngine surface (finite/bounded, produces a wet smear, stretch
// and freeze change the output, dry passthrough at MIX=0, param readback, routing).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/pstretch/pstretch_engine.h"
#include "engine/pstretch/fft.h"
#include "engine/arena.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

struct Stereo { std::vector<float> l, r; };

Stereo run(PstretchEngine& e, int blocks, const float* drive, bool& finite) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    const float* in[2] = { il, ir };
    float* out[2] = { ol, orr };
    Stereo s;
    int d = 0;
    for (int b = 0; b < blocks; b++) {
        for (size_t i = 0; i < host::kBlock; i++) { il[i] = ir[i] = drive[d++ % 4096]; }
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) finite = false;
            s.l.push_back(ol[i]); s.r.push_back(orr[i]);
        }
    }
    return s;
}
float peak(const std::vector<float>& v) { float p = 0.f; for (float x : v) p = std::fmax(p, std::fabs(x)); return p; }
float energy(const std::vector<float>& v) { float s = 0.f; for (float x : v) s += std::fabs(x); return s; }
float sad(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.f; size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) s += std::fabs(a[i] - b[i]);
    return s;
}

} // namespace

int main() {
    // --- A. Vendored FFT ---------------------------------------------------------------------------
    {
        std::vector<uint8_t> mem(1 << 16, 0);
        Arena ar(EngineArena{ mem.data(), mem.size() });
        pstretch::FFT fft;
        const int N = 64;
        float* cosb = ar.alloc<float>(N / 2); float* sinb = ar.alloc<float>(N / 2);
        uint16_t* brevb = ar.alloc<uint16_t>(N);
        check(fft.init(N, cosb, sinb, brevb), "fft: init power-of-two");
        check(!pstretch::FFT{}.init(48, cosb, sinb, brevb), "fft: rejects non-power-of-two");

        // Round-trip: forward then inverse reconstructs the input.
        float re[64], im[64], re0[64];
        for (int i = 0; i < N; i++) { re[i] = re0[i] = std::sin(0.3f * i) + 0.5f * std::cos(0.11f * i); im[i] = 0.f; }
        fft.transform(re, im, false);
        fft.transform(re, im, true);
        float err = 0.f; for (int i = 0; i < N; i++) err += std::fabs(re[i] - re0[i]);
        check(err < 1e-3f, "fft: inverse(forward(x)) == x");

        // A pure cosine at bin 5 -> energy concentrated at bins 5 and N-5.
        for (int i = 0; i < N; i++) { re[i] = std::cos(6.28318530718f * 5.f * i / N); im[i] = 0.f; }
        fft.transform(re, im, false);
        auto mag = [&](int k){ return std::sqrt(re[k]*re[k] + im[k]*im[k]); };
        float other = 0.f; for (int k = 0; k < N; k++) if (k != 5 && k != N-5) other = std::fmax(other, mag(k));
        check(mag(5) > 10.f * (other + 1e-6f), "fft: cosine concentrates at its bin");
    }

    // --- B. PstretchEngine through IEngine -------------------------------------------------------------
    host::TimeSource time;
    auto make = [&](PstretchEngine& e) { host::HostArena a; EngineContext c = host::make_context(a, time); e.init(c); };

    // A 220 Hz sine drive table.
    float drive[4096];
    for (int i = 0; i < 4096; i++) drive[i] = 0.5f * std::sin(6.28318530718f * 220.f * i / host::kSampleRate);

    // B1. Produces a finite, bounded, non-silent wet smear (run long enough to clear the startup latency).
    {
        PstretchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);          // DoubleMono: A->L, B->R
        e.set_param(ParamId::Size, DeckRef::A, 0.4f);          // moderate stretch
        e.set_param(ParamId::Mix,  DeckRef::A, 1.f);           // full wet
        e.set_param(ParamId::Mix,  DeckRef::B, 1.f);
        bool fin = true;
        const Stereo s = run(e, 400, drive, fin);              // ~38k samples
        check(fin, "engine: output finite");
        check(peak(s.l) <= 1.05f, "engine: output bounded");
        check(energy(s.l) > 0.f, "engine: produces a wet smear");
    }

    // B2. Dry passthrough at MIX=0 (DoubleMono isolates deck A on the left; ENV open by default).
    {
        PstretchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);
        e.set_param(ParamId::Mix, DeckRef::A, 0.f);            // fully dry
        e.set_param(ParamId::Env, DeckRef::A, 1.f);            // tone fully open (LP passes through)
        bool fin = true; const Stereo s = run(e, 60, drive, fin);
        // Compare the left channel to the drive after the one-pole settles (skip the first samples).
        float err = 0.f; int n = 0;
        for (size_t i = 200; i < s.l.size(); i++) { err += std::fabs(s.l[i] - drive[i % 4096]); n++; }
        check(fin && n > 0 && err / n < 0.02f, "engine: MIX=0 is a clean dry passthrough");
    }

    // B3. Stretch amount changes the output.
    {
        auto render = [&](float sizeN) {
            PstretchEngine e; make(e);
            e.set_config(ConfigId::Route, DeckRef::A, 1);
            e.set_param(ParamId::Mix, DeckRef::A, 1.f);
            e.set_param(ParamId::Size, DeckRef::A, sizeN);
            bool fin = true; return run(e, 400, drive, fin).l;
        };
        check(sad(render(0.2f), render(0.9f)) > 1.f, "engine: stretch amount changes the smear");
    }

    // B4. Freeze (Play pad) keeps producing a drone and is reflected in readback path.
    {
        PstretchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);
        e.set_param(ParamId::Mix, DeckRef::A, 1.f);
        e.set_param(ParamId::Size, DeckRef::A, 0.6f);
        bool fin = true; run(e, 300, drive, fin);              // prime the buffer
        e.on_play_pad(DeckRef::A, /*reverse=*/false);          // freeze ON
        const Stereo frozen = run(e, 300, drive, fin);
        check(fin, "engine: frozen output finite");
        check(energy(frozen.l) > 0.f, "engine: freeze holds an evolving drone (non-silent)");
        e.on_play_pad(DeckRef::A, false);                      // toggles back off (idempotent path)
    }

    // B5. Param readback + routing.
    {
        PstretchEngine e; make(e);
        e.set_param(ParamId::Size, DeckRef::A, 0.7f);
        e.set_param(ParamId::Pos,  DeckRef::A, 0.3f);
        e.set_param(ParamId::Speed, DeckRef::A, 0.8f);
        check(std::fabs(e.param(ParamId::Size, DeckRef::A) - 0.7f) < 1e-4f, "readback: SIZE");
        check(std::fabs(e.param(ParamId::Pos, DeckRef::A) - 0.3f) < 1e-4f, "readback: POS");
        check(std::fabs(e.param(ParamId::Speed, DeckRef::A) - 0.8f) < 1e-4f, "readback: PITCH");
        e.set_config(ConfigId::Route, DeckRef::A, 2);
        check(e.route() == Route::GenerativeStereo, "route: switch position 2 -> GenerativeStereo");
    }

    // B7. Capture/hold (Rev pad): grab the recent ring and loop the stretch through it - keeps producing a
    // drone indefinitely (the loop must not run dry).
    {
        PstretchEngine e; make(e);
        e.set_config(ConfigId::Route, DeckRef::A, 1);
        e.set_param(ParamId::Mix, DeckRef::A, 1.f);
        e.set_param(ParamId::Size, DeckRef::A, 0.6f);
        bool fin = true; run(e, 300, drive, fin);          // fill the ring with input
        e.on_play_pad(DeckRef::A, /*reverse=*/true);       // CAPTURE on
        const Stereo cap = run(e, 700, drive, fin);        // long hold -> the loop must keep going
        check(fin, "capture: output finite");
        check(energy(cap.l) > 0.f, "capture: loops the grabbed span (non-silent, plays through)");
        std::vector<float> tail(cap.l.end() - 4000, cap.l.end());
        check(energy(tail) > 0.f, "capture: still producing after a long hold (loops, no underrun)");
        e.on_play_pad(DeckRef::A, true);                   // back to live (idempotent path)
    }

    if (g_failures == 0) { std::printf("OK: all pstretch checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
