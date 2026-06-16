// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
// GrainflowLib grain-cloud DSP for the graincloud engine (a granular variant). Reads the recorded
// granular Buffer and produces a continuous polyphonic cloud. See gf_cloud.h.
#include "gf_cloud.h"

#include <cmath>
#include <cstring>

using namespace spotykach;

namespace {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float rand01(uint32_t& s) { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) * (1.f / 16777216.f); }

// Hann window LUT (filled once) - the grain envelope, avoiding a per-sample cosf.
constexpr int kHann = 1024;
float  g_hann[kHann + 1];
bool   g_hann_ready = false;
void ensure_hann() {
    if (g_hann_ready) return;
    for (int k = 0; k <= kHann; k++) g_hann[k] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(k) / kHann);
    g_hann_ready = true;
}

// --- GrainflowLib buffer-reader seam over the granular Buffer (T = Buffer) -------------------------
bool cb_update_buffer_info(Buffer* buf, const Grainflow::gf_io_config<float>& io, Grainflow::gf_buffer_info* info) {
    if (!buf || buf->rec_size() < 4) return false;
    if (info) {
        const int frames = static_cast<int>(buf->rec_size());
        info->buffer_frames          = frames;
        info->one_over_buffer_frames = 1.f / static_cast<float>(frames);
        info->sample_rate_adjustment = 1.f;
        info->n_channels             = 2;
        info->samplerate             = io.samplerate;
        info->one_over_samplerate    = 1.0 / io.samplerate;
    }
    return true;
}
bool cb_sample_param_buffer(Buffer*, Grainflow::gf_param*, int) { return false; }
void cb_sample_buffer(Buffer* buf, int channel, float* __restrict samples, const float* positions,
                      const int size, const float, const float) {
    const int ch = channel & 1;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < size; i++) {
        float pos = static_cast<float>(positions[i]);
        if (!std::isfinite(pos)) { samples[i] = 0.f; continue; } // guard: (int)NaN -> OOB in the reader
        buf->read_linear(pos, l, r);
        samples[i] = ch ? r : l;
    }
}
void cb_sample_envelope(Buffer*, const bool, const int, const float,
                        float* __restrict samples, const float* __restrict grain_clock, const int size) {
    for (int i = 0; i < size; i++)
        samples[i] = g_hann[static_cast<int>(clampf(grain_clock[i], 0.f, 1.f) * kHann)];
}
void cb_write_buffer(Buffer*, const int, const float*, const int, const int) {}
void cb_read_buffer (Buffer*, int, float* __restrict, int, const int) {}
void cb_clear_buffer(Buffer*) {}

Grainflow::gf_i_buffer_reader<Buffer, float> make_reader() {
    Grainflow::gf_i_buffer_reader<Buffer, float> r;
    r.update_buffer_info  = cb_update_buffer_info;
    r.sample_param_buffer = cb_sample_param_buffer;
    r.sample_buffer       = cb_sample_buffer;
    r.sample_envelope     = cb_sample_envelope;
    r.write_buffer        = cb_write_buffer;
    r.read_buffer         = cb_read_buffer;
    r.clear_buffer        = cb_clear_buffer;
    return r;
}

} // namespace

GfCloud::GfCloud() : _col(make_reader()) {
    ensure_hann();
    // Wire the io_config pointers to the member arrays (one stream channel; auto_overlap staggers grains).
    _in_gc[0] = _gc; _in_tr[0] = _tr; _in_fm[0] = _fm; _in_am[0] = _am;
    for (int g = 0; g < kMaxGrains; g++) {
        _p_out[g]=_o_out[g]; _p_state[g]=_o_state[g]; _p_prog[g]=_o_prog[g]; _p_play[g]=_o_play[g];
        _p_amp[g]=_o_amp[g]; _p_env[g]=_o_env[g]; _p_bch[g]=_o_bch[g]; _p_sch[g]=_o_sch[g];
    }
    _io.grain_clock=_in_gc; _io.traversal_phasor=_in_tr; _io.fm=_in_fm; _io.am=_in_am;
    _io.grain_clock_chans=1; _io.traversal_phasor_chans=1; _io.fm_chans=1; _io.am_chans=1;
    _io.grain_output=_p_out; _io.grain_state=_p_state; _io.grain_progress=_p_prog; _io.grain_playhead=_p_play;
    _io.grain_amp=_p_amp; _io.grain_envelope=_p_env; _io.grain_buffer_channel=_p_bch; _io.grain_stream_channel=_p_sch;
    _io.block_size=kBlock; _io.samplerate=48000; _io.livemode=false;
    // Per-grain pan-position hash (stable per index, 0..1); spread scales it live in compute_block.
    uint32_t s = 0x1234u;
    for (int g = 0; g < kMaxGrains; g++) _panH[g] = rand01(s);
}

void GfCloud::set_center(float v)    { _center = clampf(v, 0.f, 1.f); }
void GfCloud::set_spray(float v)     { _spray  = clampf(v, 0.f, 1.f); }
void GfCloud::set_duration(float v)  { _dur    = clampf(v, 0.f, 1.f); }
void GfCloud::set_density(float v)   { _density= clampf(v, 0.f, 1.f); }
void GfCloud::set_spread(float v)    { _spread = clampf(v, 0.f, 1.f); }
void GfCloud::set_transpose(float v) { _rate = std::exp2f((clampf(v, 0.f, 1.f) - 0.5f) * 4.f); } // 0.5=unity, +/-2 oct
void GfCloud::set_direction(int sw) {
    // Mode switch sends: top=0, centre=1, down=2 (verified on hardware). Make the CENTRE the natural
    // forward rest position: centre(1)->forward, top(0)->reverse, down(2)->random per grain.
    _dir = (sw == 1) ? 0 : (sw == 0) ? 1 : 2; // _dir: 0 forward, 1 reverse, 2 random
}
void GfCloud::set_glisson(float v)   { _glisson = clampf(v, 0.f, 1.f); }
void GfCloud::set_vibrato(float v)   { _vibrato = clampf(v, 0.f, 1.f); }
void GfCloud::set_pong(bool on)      { _pong = on; }

void GfCloud::init(Buffer* buf, float sample_rate) {
    _buf = buf;
    _sr  = sample_rate;
    _col.samplerate = static_cast<int>(sample_rate);
    _col.set_storage(_grains, kMaxGrains);
    _col.resize(kMaxGrains);
    _col.set_active_grains(kMaxGrains);
    _col.stream_set(Grainflow::gf_stream_set_type::automatic_streams, 1);
    _col.set_buffer(Grainflow::gf_buffers::buffer, _buf, 0);
    _idx = kBlock; // force a recompute on first process()
    _scan_phase = 0.0; _gc_ph = 0.0; // playhead + grain clock start at a known position
}

void GfCloud::compute_block() {
    if (!_buf || _buf->rec_size() < 4) { for (int i = 0; i < kBlock; i++) { _blk_l[i] = _blk_r[i] = 0.f; } return; }

    // Grain duration -> grain-clock period; density -> overlap (active grains). pow() once per block.
    const float dur_s   = 0.008f * std::pow(187.5f, clampf(_dur, 0.f, 1.f)); // 8 ms .. 1.5 s
    const float onset   = 1.f + clampf(_density, 0.f, 1.f) * 79.f;           // grains/sec
    int active = static_cast<int>(onset * dur_s + 0.5f);
    if (active < 2) active = 2; else if (active > kMaxGrains) active = kMaxGrains;
    if (active != _active) { _active = active; _col.set_active_grains(active); }

    using PN = Grainflow::gf_param_name;
    using PT = Grainflow::gf_param_type;
    _col.param_set(0, PN::rate,        PT::base,   clampf(_rate, 0.1f, 4.f));          // transpose (PITCH)
    _col.param_set(0, PN::rate,        PT::random, clampf(_spread, 0.f, 1.f) * 0.5f);  // pitch spray (MOD_AMT)
    _col.param_set(0, PN::delay,       PT::base,   0.f);
    _col.param_set(0, PN::delay,       PT::random, clampf(_spray, 0.f, 1.f) * 1500.f); // position spray (SIZE)
    _col.param_set(0, PN::space,       PT::base,   0.f);
    _col.param_set(0, PN::start_point, PT::base,   0.f);
    _col.param_set(0, PN::stop_point,  PT::base,   1.f);
    _col.param_set(0, PN::amplitude,   PT::base,   1.f);

    // Direction (Mode switch): forward / reverse / random-per-grain (base in (-1,1) -> per-grain coin flip).
    _col.param_set(0, PN::direction,   PT::base,   _dir == 1 ? -1.f : _dir == 2 ? 0.f : 1.f);
    // Glisson (Alt+PITCH): per-grain pitch glide across the grain. Bipolar around the 0.5 = none centre.
    _col.param_set(0, PN::glisson,     PT::base,   (_glisson - 0.5f) * 2.f);
    // Vibrato (Alt+POS): depth in semitones with a fixed ~5 Hz rate; rate must be > 0 for it to engage.
    _col.param_set(0, PN::vibrato_depth, PT::base, _vibrato * 12.f);
    _col.param_set(0, PN::vibrato_rate,  PT::base, _vibrato > 0.001f ? 5.f : 0.f);
    // Loop/read mode (Alt+SOS): pong (fold) vs normal. gf folds when loop_mode.base > 1.1.
    _col.param_set(0, PN::loop_mode,   PT::base,   _pong ? 2.f : 0.f);

    // The grain-clock phasor (sets grain duration) and the PLAYHEAD (traversal) that scans the buffer.
    // The playhead auto-advances at 1x (real-time playthrough), looping, so the sample actually plays;
    // POS offsets/scrubs it; reverse mode runs it backward. Grain transpose (PITCH) is independent of
    // this scan, so pitch-shifting doesn't change the playback speed.
    const double gc_rate   = (1.0 / static_cast<double>(dur_s)) / _sr;
    const int    rec       = static_cast<int>(_buf->rec_size());
    const double scan_rate = (_dir == 1 ? -1.0 : 1.0) / (rec > 0 ? rec : 1); // playhead step per sample
    const float  center    = clampf(_center, 0.f, 1.f);
    double gp = _gc_ph, sp = _scan_phase;
    for (int i = 0; i < kBlock; i++) {
        gp += gc_rate; if (gp >= 1.0) gp -= 1.0;
        sp += scan_rate; sp -= std::floor(sp);          // playhead wraps 0..1
        double t = center + sp; t -= std::floor(t);      // offset by POS, mod 1
        _gc[i] = static_cast<float>(gp);
        _tr[i] = static_cast<float>(t);
        _fm[i] = 0.f;
        _am[i] = 0.f;
    }
    _gc_ph = gp;
    _scan_phase = sp;

    _io.block_size = kBlock;
    _io.samplerate = static_cast<int>(_sr);
    _col.process(_io);

    // Per-grain equal-power pan, position = hash scaled by the spread knob (MOD_AMT). spread 0 = mono centre.
    float pl[kMaxGrains], pr[kMaxGrains];
    for (int g = 0; g < kMaxGrains; g++) {
        const float p = clampf(0.5f + (_panH[g] - 0.5f) * _spread, 0.f, 1.f);
        pl[g] = std::cos(p * 1.5707963f);
        pr[g] = std::sin(p * 1.5707963f);
    }
    const float gain = 1.4f / std::sqrt(static_cast<float>(_active < 1 ? 1 : _active));
    for (int i = 0; i < kBlock; i++) {
        float l = 0.f, r = 0.f;
        for (int g = 0; g < kMaxGrains; g++) {
            const float o = _o_out[g][i];
            l += o * pl[g];
            r += o * pr[g];
        }
        _blk_l[i] = l * gain;
        _blk_r[i] = r * gain;
    }
}

float GfCloud::playhead() const {
    float p = _center + static_cast<float>(_scan_phase);
    p -= std::floor(p);
    return p;
}

void GfCloud::process(float& out0, float& out1) {
    if (_idx >= kBlock) { compute_block(); _idx = 0; }
    out0 = _blk_l[_idx];
    out1 = _blk_r[_idx];
    _idx++;
}

namespace spotykach {
// Per-deck instances in regular RAM (zero-initialized BSS, unlike the SDRAM arena). Constructed at
// startup when RAM is ready, so their member ctors (collection, grains) are safe.
static GfCloud g_gf_cloud[2];
GfCloud* gf_cloud_acquire(int ref) { return &g_gf_cloud[ref & 1]; }
}
