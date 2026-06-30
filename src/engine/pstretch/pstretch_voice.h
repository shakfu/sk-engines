// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include "engine/pstretch/fft.h"

#include <cstdint>
#include <cstddef>
#include <cmath>

// pstretch::Stretcher - one mono real-time PaulStretch voice. Clean-room reimplementation of the
// PaulStretch algorithm (Nasca Octavian Paul), written from the published description, NOT derived from
// any GPL source: a large smooth analysis/synthesis window, per-grain FFT with the phases RANDOMIZED
// (magnitudes kept), 50%-overlap-add back to time. The phase randomization turns transients into a
// diffuse spectral wash; advancing the read head slowly (or freezing it) stretches/smears time.
//
// Real-time twist: instead of stretching a finished file, the voice keeps a multi-second ring of live
// input and the analysis head advances at (hop / stretch) input samples per output hop, so for large
// stretch it crawls through (or freezes on) recent audio -> an evolving ambient drone of the recent past.
// PITCH resamples the grain (octave-ish shift). Output lags input by the window + read lag (inherent to
// real-time stretching); the engine mixes it against the dry signal.
namespace spotykach {
namespace pstretch {

// Read-only state shared across voices (built once by the engine): the FFT tables, the window, and the
// overlap normalization. The per-hop FFT scratch (re/im) is NOT here - it is per-voice, because the
// pipelined worker spreads each hop across many blocks, so the two voices' transforms overlap in time and
// would corrupt a shared scratch.
struct Shared {
    const FFT*   fft     = nullptr;
    const float* window  = nullptr;   // length n: (1 - x^2)^1.25, x in [-1,1]
    const float* invnorm = nullptr;   // length n/2: 1 / (w[i]^2 + w[i+n/2]^2), the 50%-overlap COLA gain
    // cos/sin lookup for the phase smear (the angle is random, so a coarse LUT is inaudible and replaces
    // ~2048 software transcendentals per hop - the spike that overran a block - with table reads).
    const float* lutc    = nullptr;   // cos(2pi i/L), i in [0, L)
    const float* luts    = nullptr;   // sin(2pi i/L)
    int          lutmask = 0;          // L-1 (L a power of two)
    float        lutscale = 0.f;       // L / 2pi: radians -> table index
    int          n       = 0;          // window / FFT size (power of two)
    int          hop     = 0;          // n/2
};

class Stretcher {
public:
    // Per-voice buffers (engine-owned): the input `inring` (length `ringlen`, power of two, in SDRAM); the
    // FFT scratch `re`/`im` (length n) and overlap accumulator `ola` (length n) and the output FIFO `fifo`
    // (length `fifocap` >= 2*hop) - all in fast on-chip SRAM.
    void init(float sample_rate, const Shared* sh, float* inring, uint32_t ringlen,
              float* re, float* im, float* ola, float* fifo, int fifocap, uint32_t seed) {
        _sr = sample_rate > 0.f ? sample_rate : 48000.f;
        _sh = sh;
        _inring = inring; _ringlen = ringlen; _ringmask = ringlen - 1;
        _re = re; _im = im; _ola = ola; _fifo = fifo; _fcap = fifocap;
        _rng = seed ? seed : 0x1234567u;
        _wpos = 0; _rpos = 0.f;
        _frd = 0; _fwr = 0; _fcount = 0;
        _wstate = W_IDLE; _wcur = 0; _wjob = FFT::Job{};
        for (int i = 0; i < sh->n; i++) _ola[i] = 0.f;
        for (uint32_t i = 0; i < ringlen; i++) _inring[i] = 0.f;
    }

    void set_stretch(float factor)  { _stretch = factor < 1.f ? 1.f : factor; }   // >= 1x
    void set_pitch(float ratio)     { _pitch = ratio < 0.0625f ? 0.0625f : (ratio > 16.f ? 16.f : ratio); }
    void set_diffusion(float d)     { _diffusion = d < 0.f ? 0.f : (d > 1.f ? 1.f : d); }  // 0=clean..1=wash
    void set_freeze(bool f)         { _freeze = f; }

    // Latched capture/hold: on a rising edge, snapshot the most-recent (ringlen - 2*window) samples and
    // stop writing live input, so the read head loops *through* that frozen span (the stretch traverses
    // the captured phrase). Off resumes live smear. Positions are the ring's free-running sample space;
    // indexing masks with _ringmask.
    void set_capture(bool on) {
        if (on && !_capture) {
            const float n = static_cast<float>(_sh->n);
            float len = static_cast<float>(_ringlen) - 2.f * n;     // span to loop, leaving a grain margin
            const float avail = static_cast<float>(_wpos);          // samples written so far
            if (len > avail) len = avail;
            if (len < n) len = n;                                   // need at least one grain
            _cap_len = len;
            _cap_end = static_cast<float>(_wpos);
            _cap_start = _cap_end - _cap_len;
            if (_cap_start < 0.f) _cap_start = 0.f;
            _rpos = _cap_start;                                     // play through from the start of the grab
        }
        _capture = on;
    }

    // The audio block is split into three engine-driven steps so a whole FFT never lands in one block:
    //   1. write_input() - capture the live input into the ring (skipped in capture/hold).
    //   2. work(budget)  - advance the pipelined analysis/synthesis worker (the engine shares a small
    //                      per-block budget across both voices); fills the output FIFO ahead of playback.
    //   3. drain()       - pull the block's output samples from the FIFO (0 on underrun during start-up).
    void write_input(const float* in, size_t nframes) {
        if (_capture) return;
        for (size_t i = 0; i < nframes; i++) { _inring[_wpos & _ringmask] = in[i]; _wpos++; }
    }

    // Advance the pipelined analysis/synthesis worker by up to `budget` work units (ticks). Each tick does
    // a bounded slice (<= kChunk samples/bins, or kChunk FFT butterflies) so a whole 4096-FFT never lands
    // in one block. The engine shares a small per-block budget across the two voices. Returns ticks used.
    int work(int budget) {
        const int n = _sh->n, half = _sh->hop;
        int used = 0;
        while (used < budget) {
            switch (_wstate) {
                case W_IDLE:
                    if (fifo_free() < half) return used;             // a hop already buffered ahead - idle
                    start_hop(); _wcur = 0; _wstate = W_EXTRACT;
                    continue;                                         // free transition (not a tick)
                case W_EXTRACT: {
                    const int e = imin(_wcur + kChunk, n);
                    extract_range(_wcur, e); _wcur = e;
                    if (_wcur >= n) { _wjob = FFT::Job{}; _wstate = W_FWD; }
                    break;
                }
                case W_FWD:
                    if (_sh->fft->step(_re, _im, false, _wjob, kChunk)) { _im[0] = 0.f; _wcur = 1; _wstate = W_SMEAR; }
                    break;                                            // DC bin -> real before smear
                case W_SMEAR: {
                    const int e = imin(_wcur + kChunk, half);
                    smear_range(_wcur, e); _wcur = e;
                    if (_wcur >= half) { _im[half] = 0.f; _wjob = FFT::Job{}; _wstate = W_INV; }  // Nyquist real
                    break;
                }
                case W_INV:
                    if (_sh->fft->step(_re, _im, true, _wjob, kChunk)) { _wcur = 0; _wstate = W_OLA_ADD; }
                    break;
                case W_OLA_ADD: {
                    const int e = imin(_wcur + kChunk, n);
                    ola_add_range(_wcur, e); _wcur = e;
                    if (_wcur >= n) { _wcur = 0; _wstate = W_OLA_SCROLL; }
                    break;
                }
                case W_OLA_SCROLL: {
                    const int e = imin(_wcur + kChunk, n);
                    ola_scroll_range(_wcur, e); _wcur = e;
                    if (_wcur >= n) { advance_read(); _wstate = W_IDLE; }
                    break;
                }
            }
            used++;
        }
        return used;
    }

    void drain(float* out, size_t nframes) {
        for (size_t i = 0; i < nframes; i++) out[i] = (_fcount > 0) ? fifo_pop() : 0.f;
    }

private:
    static constexpr int kChunk = 512;   // work granularity: samples/bins (or FFT butterflies) per tick
    enum WState { W_IDLE, W_EXTRACT, W_FWD, W_SMEAR, W_INV, W_OLA_ADD, W_OLA_SCROLL };

    static int imin(int a, int b) { return a < b ? a : b; }
    int   fifo_free() const { return _fcap - _fcount; }
    void  fifo_push(float v) { _fifo[_fwr] = v; if (++_fwr >= _fcap) _fwr = 0; _fcount++; }
    float fifo_pop() { const float v = _fifo[_frd]; if (++_frd >= _fcap) _frd = 0; _fcount--; return v; }

    // Position the read head for the hop about to begin.
    void start_hop() {
        if (_capture) return;   // CAPTURE: head loops the frozen span (wrap handled in advance_read)
        // LIVE: keep the head inside the written, not-yet-overwritten span. When it falls too far behind
        // (large stretch) it holds at the trailing edge and the advancing write head drags it forward at
        // real time -> a continuous smear of the recent past. (The 2*n margin covers the few blocks the
        // chunked extract spans before the write head could overwrite the grain.)
        const int n = _sh->n;
        const float maxlag = static_cast<float>(_ringlen) - 2.f * static_cast<float>(n);
        float hi = static_cast<float>(_wpos) - static_cast<float>(n);   // newest fully-written grain start
        float lo = static_cast<float>(_wpos) - maxlag;                  // oldest still-valid grain start
        if (hi < 0.f) hi = 0.f;
        if (lo < 0.f) lo = 0.f;
        if (_rpos > hi) _rpos = hi;
        if (_rpos < lo) _rpos = lo;
    }

    // Windowed grain at the read head, pitch-resampled (linear interp). pitch>1 reads faster -> higher pitch.
    void extract_range(int s, int e) {
        const float* win = _sh->window;
        for (int i = s; i < e; i++) {
            const float src = _rpos + static_cast<float>(i) * _pitch;
            const int32_t i0 = static_cast<int32_t>(src);
            const float fr = src - static_cast<float>(i0);
            const float s0 = _inring[static_cast<uint32_t>(i0) & _ringmask];
            const float s1 = _inring[static_cast<uint32_t>(i0 + 1) & _ringmask];
            _re[i] = (s0 + (s1 - s0) * fr) * win[i];
            _im[i] = 0.f;
        }
    }

    // Smear bins [s, e): ROTATE each by phi = diffusion*random-offset (X' = X*e^{i*phi}); preserves |X|, no
    // atan2/sqrt, cos/sin from the LUT (random phase -> table quantization inaudible). diffusion=0 leaves
    // the grain (clean window resynthesis); diffusion=1 -> uniformly random phase = the full PaulStretch
    // wash. Bin k and its conjugate n-k are set together so the inverse transform stays real.
    void smear_range(int s, int e) {
        const int n = _sh->n;
        float* re = _re; float* im = _im;
        const float* lc = _sh->lutc; const float* ls = _sh->luts;
        const int mask = _sh->lutmask; const float scale = _sh->lutscale;
        for (int k = s; k < e; k++) {
            const int idx = static_cast<int>(_diffusion * rand_offset() * scale) & mask;
            const float c = lc[idx], si = ls[idx];
            const float rr = re[k] * c - im[k] * si;
            const float ii = re[k] * si + im[k] * c;
            re[k] = rr;       im[k] = ii;
            re[n - k] = rr;   im[n - k] = -ii;             // conjugate partner
        }
    }

    // Overlap-add the inverse transform into the accumulator over [s, e); emit the complete leading-half
    // samples (COLA-normalized) to the output FIFO as they are finalized.
    void ola_add_range(int s, int e) {
        const int half = _sh->hop;
        const float* win = _sh->window;
        for (int i = s; i < e; i++) {
            _ola[i] += _re[i] * win[i];
            if (i < half) fifo_push(_ola[i] * _sh->invnorm[i]);
        }
    }

    // Scroll the accumulator left by one hop over [s, e), zeroing the freed tail.
    void ola_scroll_range(int s, int e) {
        const int n = _sh->n, half = _sh->hop;
        for (int i = s; i < e; i++) _ola[i] = (i < n - half) ? _ola[i + half] : 0.f;
    }

    // Advance the read head by (output hop / stretch) input samples. Freeze holds it -> static drone.
    void advance_read() {
        if (_freeze) return;
        _rpos += static_cast<float>(_sh->hop) / _stretch;
        if (_capture) {   // loop within the captured span
            while (_rpos >= _cap_end) _rpos -= _cap_len;
            if (_rpos < _cap_start) _rpos = _cap_start;
        }
    }

    uint32_t rnd() { _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5; return _rng; }
    // uniform offset in [-pi, pi)
    float rand_offset() { return static_cast<float>(rnd()) * (6.28318530717958647692f / 4294967296.0f) - 3.14159265358979323846f; }

    float    _sr = 48000.f;
    const Shared* _sh = nullptr;
    float*   _inring = nullptr; uint32_t _ringlen = 0, _ringmask = 0;   // input ring (SDRAM)
    float*   _re = nullptr; float* _im = nullptr;                       // per-voice FFT scratch (SRAM)
    float*   _ola = nullptr;                                            // overlap-add accumulator (SRAM)
    float*   _fifo = nullptr; int _fcap = 0, _frd = 0, _fwr = 0, _fcount = 0;   // output FIFO (SRAM)

    WState   _wstate = W_IDLE;   // pipelined worker state
    int      _wcur = 0;          // cursor within the current chunked phase (extract/smear/ola)
    FFT::Job _wjob;              // resumable-FFT progress (stage + butterfly index)
    uint32_t _wpos = 0;          // write head (input), free-running mod ringlen
    float    _rpos = 0.f;        // analysis read head (input samples)

    float    _stretch = 8.f, _pitch = 1.f, _diffusion = 1.f;
    bool     _freeze = false;
    bool     _capture = false;           // capture/hold: read head loops a frozen span instead of live
    float    _cap_start = 0.f, _cap_end = 0.f, _cap_len = 0.f;
    uint32_t _rng = 0x1234567u;
};

} // namespace pstretch
} // namespace spotykach
