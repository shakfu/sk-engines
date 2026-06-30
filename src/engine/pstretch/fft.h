// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <cmath>

// A small, self-contained, power-of-two radix-2 (decimation-in-time) complex FFT. Clean-room, MIT - no
// CMSIS-DSP dependency (CMSIS-DSP's sources are not compiled into this firmware's libdaisy.a, and its
// arm_rfft_fast_f32 caps at 4096; a vendored FFT also runs identically on the host test and the device).
//
// The PaulStretch hop rate is low (~23 hops/s at a 4096 window / 48 kHz), so this unoptimized but correct
// transform is cheap. Operates in place on split real/imag arrays the caller owns; the twiddle and
// bit-reversal tables are built once into the injected Arena and shared across voices (the transform
// itself is stateless beyond those read-only tables).
namespace spotykach {
namespace pstretch {

class FFT {
public:
    // Build the twiddle/bit-reversal tables for an N-point transform (N a power of two) into `arena`.
    // The caller provides the table storage: `cosb`/`sinb` of length n/2 and `brevb` of length n. They are
    // kept (not copied), so put them in fast on-chip SRAM - the transform's scattered access to these +
    // re/im dominates its cost, and scattered SDRAM access on the H7 is an order of magnitude slower.
    // Returns false if N is not a positive power of two or any buffer is null.
    bool init(int n, float* cosb, float* sinb, uint16_t* brevb) {
        if (n < 2 || (n & (n - 1)) != 0) return false;
        _n = n;
        _log2n = 0;
        while ((1 << _log2n) < n) _log2n++;
        _cos = cosb;
        _sin = sinb;
        _brev = brevb;
        if (!_cos || !_sin || !_brev) return false;
        const float twopi = 6.28318530717958647692f;
        for (int k = 0; k < n / 2; k++) {
            _cos[k] = std::cos(twopi * static_cast<float>(k) / static_cast<float>(n));
            _sin[k] = std::sin(twopi * static_cast<float>(k) / static_cast<float>(n));
        }
        for (int i = 0; i < n; i++) {
            unsigned r = 0, x = static_cast<unsigned>(i);
            for (int b = 0; b < _log2n; b++) { r = (r << 1) | (x & 1u); x >>= 1; }
            _brev[i] = static_cast<uint16_t>(r);
        }
        return true;
    }

    int size() const { return _n; }

    // In-place transform of the length-N complex signal (re[], im[]). inverse=false -> forward
    // (W = e^-i2pi k/N), inverse=true -> conjugate twiddles + 1/N scaling. DIT, so bit-reverse first.
    void transform(float* re, float* im, bool inverse) const {
        bitrev(re, im);
        const float wsign = inverse ? 1.0f : -1.0f;
        for (int len = 2; len <= _n; len <<= 1) butterfly_stage(re, im, len, wsign);
        if (inverse) scale_inverse(re, im);
    }

    // --- Resumable transform: spread one FFT across several audio blocks ---------------------------
    // A length-N transform = the bit-reversal pass + log2(N) butterfly stages (each n/2 butterflies) +
    // (inverse only) a scaling pass. `Job` tracks progress to the granularity of a single BUTTERFLY, so
    // step(..., maxBf) does at most ~maxBf butterflies and returns true when the whole transform is
    // complete. The PaulStretch worker runs a bounded slice per audio block so a 4096-point FFT never lands
    // in one block (which would overrun it and underrun the audio DMA).
    struct Job { int stage = 0; int idx = 0; };   // stage 0 = need bit-reversal; else butterfly length; idx
                                                  // = next butterfly within the stage (0 .. n/2)

    bool step(float* re, float* im, bool inverse, Job& job, int maxBf) const {
        if (job.stage == 0) {                      // bit-reversal pass (counts as one slice)
            bitrev(re, im);
            job.stage = 2; job.idx = 0;
            return false;
        }
        const float wsign = inverse ? 1.0f : -1.0f;
        const int total = _n >> 1;                 // butterflies per stage (n/2, any stage)
        int done = 0;
        while (job.stage <= _n && done < maxBf) {
            int end = job.idx + (maxBf - done);
            if (end > total) end = total;
            butterfly_range(re, im, job.stage, wsign, job.idx, end);
            done += end - job.idx;
            job.idx = end;
            if (job.idx >= total) { job.stage <<= 1; job.idx = 0; }
        }
        if (job.stage > _n) {
            if (inverse) scale_inverse(re, im);
            return true;
        }
        return false;
    }

private:
    void bitrev(float* re, float* im) const {
        for (int i = 0; i < _n; i++) {
            const int j = _brev[i];
            if (j > i) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
    }
    void butterfly_stage(float* re, float* im, int len, float wsign) const {
        butterfly_range(re, im, len, wsign, 0, _n >> 1);
    }
    // A range [bf0, bf1) of the n/2 DIT butterflies of stage length `len` (W = cos + wsign*i*sin). The
    // butterflies are indexed group-major: butterfly b -> group = b/half, j = b%half (tracked incrementally
    // to avoid a per-butterfly divide), a = group*len + j, partner b = a+half, twiddle index j*(n/len).
    void butterfly_range(float* re, float* im, int len, float wsign, int bf0, int bf1) const {
        const int half = len >> 1;
        const int step = _n / len;                 // twiddle stride for this stage
        int group = bf0 / half;
        int j = bf0 - group * half;
        for (int bf = bf0; bf < bf1; bf++) {
            const int a = group * len + j;
            const int b = a + half;
            const float wr = _cos[j * step];
            const float wi = wsign * _sin[j * step];
            const float br = re[b], bi = im[b];
            const float tr = wr * br - wi * bi;   // t = W * x[b]
            const float ti = wr * bi + wi * br;
            re[b] = re[a] - tr; im[b] = im[a] - ti;
            re[a] += tr;        im[a] += ti;
            if (++j >= half) { j = 0; ++group; }
        }
    }
    void scale_inverse(float* re, float* im) const {
        const float s = 1.0f / static_cast<float>(_n);
        for (int i = 0; i < _n; i++) { re[i] *= s; im[i] *= s; }
    }

    int   _n = 0, _log2n = 0;
    float* _cos = nullptr;   // cos(2pi k/N), k in [0, N/2)
    float* _sin = nullptr;   // sin(2pi k/N)
    uint16_t* _brev = nullptr;
};

} // namespace pstretch
} // namespace spotykach
