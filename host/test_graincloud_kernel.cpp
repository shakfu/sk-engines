// Kernel integration smoke test for the de-STL'd GrainflowLib port (engine #11). This is NOT the engine
// test - it exercises the vendored kernel directly to prove three things before the engine wrapper is
// built: (1) the de-STL'd gf_grain / gf_grain_collection compile with NO heap (fixed arena-style
// storage, inline phasor, no throw); (2) the arena buffer-reader seam (gf_i_buffer_reader callbacks
// over a plain int16 stereo buffer) drives the grains; (3) the per-stream grain-clock + traversal phasor
// glue produces FINITE stereo output over a long run. It is the prototype of Phases 2-3.
//
// Build: see host/Makefile `test-graincloud-kernel`. Runs headless; asserts finite output.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>

#include "grainflow/gfGrainCollection.h"

using namespace Grainflow;

namespace {

constexpr int   kBlock      = 96;   // platform audio block == gf Internalblock
constexpr int   kFrames     = 1 << 20; // 1,048,576-frame (~21 s) int16 stereo source
constexpr int   kSampleRate = 48000;
constexpr int   kMaxGrains  = 32;

// The recorded source the cloud reads. T (the gf buffer handle) is a pointer to this POD - the de-STL'd
// replacement for GrainflowLib's AudioFile-backed gf_buffer.
struct GcBuffer {
    int16_t* frames; // interleaved L,R
    int      nframes;
    int      channels;
    int      samplerate;
};

GcBuffer g_src;
int16_t  g_src_data[kFrames * 2];

inline float dec(int16_t s) { return static_cast<float>(s) * (1.f / 32767.f); }

// --- gf_i_buffer_reader callbacks (the Phase-2 seam) -------------------------------------------------
// Catmull-Rom cubic read of one channel at fractional frame `pos`.
float read_chan_cubic(const GcBuffer* gb, int channel, float pos) {
    const int   n = gb->nframes;
    int i = static_cast<int>(pos);
    // wrap into [0,n)
    i %= n; if (i < 0) i += n;
    const float t = pos - std::floor(pos);
    auto at = [&](int k) -> float {
        int idx = i + k; idx %= n; if (idx < 0) idx += n;
        return dec(gb->frames[idx * 2 + channel]);
    };
    const float a = at(-1), b = at(0), c = at(1), d = at(2);
    const float c0 = b, c1 = .5f * (c - a),
                c2 = a - 2.5f * b + 2.f * c - .5f * d,
                c3 = .5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

bool cb_update_buffer_info(GcBuffer* buffer, const gf_io_config<float>& io, gf_buffer_info* info) {
    if (buffer == nullptr) return false;            // e.g. envelope_ref_ is null -> use default envelope
    if (info) {
        info->buffer_frames          = buffer->nframes;
        info->one_over_buffer_frames = 1.f / buffer->nframes;
        info->sample_rate_adjustment = 1.f;
        info->n_channels             = buffer->channels;
        info->samplerate             = buffer->samplerate;
        info->one_over_samplerate    = 1.0 / buffer->samplerate;
    }
    return true;
}

bool cb_sample_param_buffer(GcBuffer*, gf_param*, int) { return false; } // no per-grain param buffers

void cb_sample_buffer(GcBuffer* buffer, int channel, float* __restrict samples, const float* positions,
                      const int size, const float, const float) {
    const int ch = (channel < buffer->channels) ? channel : buffer->channels - 1;
    for (int i = 0; i < size; i++) samples[i] = read_chan_cubic(buffer, ch, static_cast<float>(positions[i]));
}

void cb_sample_envelope(GcBuffer*, const bool /*use_default*/, const int, const float,
                        float* __restrict samples, const float* __restrict grain_clock, const int size) {
    // Default Hann window over the grain phase [0,1].
    for (int i = 0; i < size; i++) {
        const float p = grain_clock[i];
        samples[i] = 0.5f - 0.5f * std::cos(6.2831853f * (p < 0.f ? 0.f : (p > 1.f ? 1.f : p)));
    }
}

void cb_write_buffer(GcBuffer*, const int, const float*, const int, const int) {}
void cb_read_buffer (GcBuffer*, int, float* __restrict, int, const int) {}
void cb_clear_buffer(GcBuffer*) {}

gf_i_buffer_reader<GcBuffer, float> make_reader() {
    gf_i_buffer_reader<GcBuffer, float> r;
    r.update_buffer_info  = cb_update_buffer_info;
    r.sample_param_buffer = cb_sample_param_buffer;
    r.sample_buffer       = cb_sample_buffer;
    r.sample_envelope     = cb_sample_envelope;
    r.write_buffer        = cb_write_buffer;
    r.read_buffer         = cb_read_buffer;
    r.clear_buffer        = cb_clear_buffer;
    return r;
}

// --- io_config scratch (the Phase-3 glue): per-stream inputs + per-grain outputs --------------------
struct IoScratch {
    // inputs (1 stream channel each for this smoke test)
    float grain_clock[1][kBlock];
    float traversal [1][kBlock];
    float fm        [1][kBlock];
    float am        [1][kBlock];
    float* in_gc[1]; float* in_tr[1]; float* in_fm[1]; float* in_am[1];
    // outputs (per grain)
    float o_output  [kMaxGrains][kBlock];
    float o_state   [kMaxGrains][kBlock];
    float o_progress[kMaxGrains][kBlock];
    float o_playhead[kMaxGrains][kBlock];
    float o_amp     [kMaxGrains][kBlock];
    float o_env     [kMaxGrains][kBlock];
    float o_bufchan [kMaxGrains][kBlock];
    float o_strchan [kMaxGrains][kBlock];
    float* p_output[kMaxGrains]; float* p_state[kMaxGrains]; float* p_progress[kMaxGrains];
    float* p_playhead[kMaxGrains]; float* p_amp[kMaxGrains]; float* p_env[kMaxGrains];
    float* p_bufchan[kMaxGrains]; float* p_strchan[kMaxGrains];
};
IoScratch g_io;

void wire_io(gf_io_config<float>& io, int n_grains) {
    g_io.in_gc[0] = g_io.grain_clock[0]; g_io.in_tr[0] = g_io.traversal[0];
    g_io.in_fm[0] = g_io.fm[0];          g_io.in_am[0] = g_io.am[0];
    for (int g = 0; g < kMaxGrains; g++) {
        g_io.p_output[g]=g_io.o_output[g]; g_io.p_state[g]=g_io.o_state[g];
        g_io.p_progress[g]=g_io.o_progress[g]; g_io.p_playhead[g]=g_io.o_playhead[g];
        g_io.p_amp[g]=g_io.o_amp[g]; g_io.p_env[g]=g_io.o_env[g];
        g_io.p_bufchan[g]=g_io.o_bufchan[g]; g_io.p_strchan[g]=g_io.o_strchan[g];
    }
    io.grain_clock = g_io.in_gc; io.traversal_phasor = g_io.in_tr; io.fm = g_io.in_fm; io.am = g_io.in_am;
    io.grain_clock_chans = 1; io.traversal_phasor_chans = 1; io.fm_chans = 1; io.am_chans = 1;
    io.grain_output = g_io.p_output; io.grain_state = g_io.p_state; io.grain_progress = g_io.p_progress;
    io.grain_playhead = g_io.p_playhead; io.grain_amp = g_io.p_amp; io.grain_envelope = g_io.p_env;
    io.grain_buffer_channel = g_io.p_bufchan; io.grain_stream_channel = g_io.p_strchan;
    io.block_size = kBlock; io.samplerate = kSampleRate; io.livemode = false;
    (void)n_grains;
}

// Arena-style storage for the grain array (no heap): a raw byte buffer we placement-new the grains into,
// exactly as the engine will do from the SDRAM arena.
alignas(gf_grain<GcBuffer, kBlock, float>) unsigned char g_grain_store[sizeof(gf_grain<GcBuffer, kBlock, float>) * kMaxGrains];

} // namespace

int main() {
    // Fill the source with a quiet test tone + noise so reads are non-trivial and bounded.
    uint32_t s = 0xBEEF;
    for (int i = 0; i < kFrames; i++) {
        const float t = std::sin(2.f * 3.14159265f * 220.f * i / kSampleRate) * 0.4f;
        s = s * 1664525u + 1013904223u;
        const float nse = ((s >> 9) * (1.f / 8388608.f) - 1.f) * 0.05f;
        const int16_t v = static_cast<int16_t>((t + nse) * 32767.f);
        g_src_data[i * 2] = v; g_src_data[i * 2 + 1] = v;
    }
    g_src = { g_src_data, kFrames, 2, kSampleRate };

    const int n_grains = 16;

    // Placement-new the grain array into the byte store (the de-STL no-heap path).
    auto* grains = reinterpret_cast<gf_grain<GcBuffer, kBlock, float>*>(g_grain_store);
    for (int g = 0; g < kMaxGrains; g++) new (&grains[g]) gf_grain<GcBuffer, kBlock, float>();

    gf_grain_collection<GcBuffer, kBlock, float> cloud(make_reader());
    cloud.samplerate = kSampleRate;
    cloud.set_storage(grains, kMaxGrains);
    cloud.resize(n_grains);
    cloud.set_active_grains(n_grains);
    cloud.stream_set(gf_stream_set_type::automatic_streams, 1);
    cloud.set_buffer(gf_buffers::buffer, &g_src, 0); // 0 = all grains

    // Cloud params: small position spray, a window, density via grain-clock rate (below), a touch of
    // per-grain pitch/pan spray via base/random/offset.
    cloud.param_set(0, gf_param_name::start_point, gf_param_type::base, 0.0f);
    cloud.param_set(0, gf_param_name::stop_point,  gf_param_type::base, 1.0f);
    cloud.param_set(0, gf_param_name::transpose,   gf_param_type::base, 0.0f);  // unity
    cloud.param_set(0, gf_param_name::transpose,   gf_param_type::random, 5.0f); // +/- pitch spray (semis)
    cloud.param_set(0, gf_param_name::amplitude,   gf_param_type::base, 1.0f);
    cloud.param_set(0, gf_param_name::space,       gf_param_type::base, 0.0f);

    gf_io_config<float> io;
    wire_io(io, n_grains);

    // Drive: grain_clock is a phasor at ~25 Hz (25 grains/sec/stream); traversal slowly scans the buffer.
    double gc_phase = 0.0, tr_phase = 0.0;
    const double gc_rate = 25.0 / kSampleRate;     // grain density
    const double tr_rate = 0.05 / kSampleRate;     // playhead scan (very slow)

    double peak = 0.0; long nonzero = 0;
    const int kBlocks = 4000;
    for (int b = 0; b < kBlocks; b++) {
        for (int n = 0; n < kBlock; n++) {
            gc_phase += gc_rate; if (gc_phase >= 1.0) gc_phase -= 1.0;
            tr_phase += tr_rate; if (tr_phase >= 1.0) tr_phase -= 1.0;
            g_io.grain_clock[0][n] = static_cast<float>(gc_phase);
            g_io.traversal [0][n] = static_cast<float>(tr_phase);
            g_io.fm[0][n] = 0.f; g_io.am[0][n] = 0.f;
        }
        cloud.process(io);
        // Mixdown: sum per-grain outputs (mono here; panning is Phase 3) and check finiteness.
        for (int n = 0; n < kBlock; n++) {
            float mix = 0.f;
            for (int g = 0; g < n_grains; g++) mix += g_io.o_output[g][n];
            if (!std::isfinite(mix)) { std::printf("FAIL: non-finite output at block %d sample %d\n", b, n); return 1; }
            const double a = std::fabs(mix);
            if (a > peak) peak = a;
            if (a > 1e-6) nonzero++;
        }
    }

    std::printf("graincloud kernel smoke test\n");
    std::printf("  grains=%d  blocks=%d  peak=%.4f  nonzero_samples=%ld\n", n_grains, kBlocks, peak, nonzero);
    // The cloud must actually produce sound (grains fired and read the buffer) and stay bounded.
    assert(nonzero > 0 && "cloud produced silence - grains never fired / buffer never read");
    assert(peak < 50.0 && "cloud output unbounded - check window/amp scaling");
    std::printf("  PASS (finite, audible, bounded)\n");
    return 0;
}
