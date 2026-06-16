// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/graincloud/graincloud_engine.h"

#include "engine/arena.h"

#include "grainflow/gfGrainCollection.h"

#include <new>
#include <cmath>
#include <algorithm>
#include <cstring>

using namespace spotykach;

namespace {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float softclip(float x) {
    if (x < -1.5f) return -1.f;
    if (x >  1.5f) return  1.f;
    return x - (4.f / 27.f) * x * x * x;
}

// Hann window lookup (filled once in setup) - replaces a per-sample cosf in the grain envelope, which
// at N grains x 96 samples x 2 decks was a real chunk of the audio block.
constexpr int kHannLut = 1024;
float g_hann_lut[kHannLut + 1];

// --- The recorded source the cloud reads ------------------------------------------------------------
// Self-contained (no granular Buffer dep): float stereo frames in the SDRAM arena, a simple record
// state with a short raised-cosine fade-in, and a Catmull-Rom cubic read at fractional frame positions
// (the math mirrors granular's buffer.cpp). It is also the byte source for the SD save/load port.
struct Frame { float l, r; };

constexpr size_t kRecFadeFrames = 192; // ~4 ms fade-in on record start

struct RecBuffer {
    Frame*  data = nullptr;
    size_t  cap  = 0;     // capacity in frames
    size_t  len  = 0;     // recorded length in frames
    size_t  wpos = 0;     // write head while recording
    size_t  fade = 0;     // fade-in counter
    bool    recording = false;

    void init(Frame* mem, size_t capacity) { data = mem; cap = capacity; len = 0; wpos = 0; }
    bool empty() const { return len == 0; }

    void clear() {
        if (data && cap) std::memset(data, 0, cap * sizeof(Frame));
        len = 0; wpos = 0; recording = false;
    }

    void start_record() { if (!cap) return; recording = true; fade = 0; wpos = 0; len = 0; }
    void stop_record()  { if (recording) { recording = false; len = wpos; } }

    void write(float in0, float in1) {
        if (!recording || !cap) return;
        float g = 1.f;
        if (fade < kRecFadeFrames) { g = static_cast<float>(fade) / kRecFadeFrames; fade++; }
        data[wpos].l = in0 * g;
        data[wpos].r = in1 * g;
        wpos++;
        if (wpos >= cap) { recording = false; len = cap; } // ran to the end
        else             { len = wpos; }
    }

    // Cubic read of one channel at fractional frame `pos`, wrapping within [0,len).
    float read_ch(int ch, double pos) const {
        if (len < 4 || !data) return 0.f;
        // CRITICAL guard: GrainflowLib can hand us a non-finite position; static_cast<int>(NaN/inf) is
        // undefined (yields a garbage index) -> out-of-bounds SDRAM read -> hard fault. Reject it.
        if (!std::isfinite(pos)) return 0.f;
        const int n = static_cast<int>(len);
        double p = std::fmod(pos, static_cast<double>(n));
        if (p < 0) p += n;
        int i = static_cast<int>(p);
        if (i < 0 || i >= n) i = 0;                 // belt-and-braces against fp edge cases
        const float t = static_cast<float>(p - i);
        auto S = [&](int k) -> float {
            int idx = i + k; idx %= n; if (idx < 0) idx += n;
            const Frame& f = data[idx]; return ch == 0 ? f.l : f.r;
        };
        const float a = S(-1), b = S(0), c = S(1), d = S(2);
        const float c0 = b, c1 = .5f * (c - a),
                    c2 = a - 2.5f * b + 2.f * c - .5f * d,
                    c3 = .5f * (d - a) + 1.5f * (b - c);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
};

// --- GrainflowLib buffer-reader seam (T = RecBuffer) ------------------------------------------------
// The de-STL'd kernel calls these function pointers; they bind the cloud to our arena RecBuffer instead
// of GrainflowLib's dropped AudioFile reader. Param/delay/rate/window buffers are absent (return false
// -> the grain uses its own base/random/offset values); the envelope is the built-in Hann.
bool cb_update_buffer_info(RecBuffer* buf, const Grainflow::gf_io_config<float>& io,
                           Grainflow::gf_buffer_info* info) {
    if (buf == nullptr || buf->len < 4) return false; // empty/absent -> invalid (grain stays silent)
    if (info) {
        info->buffer_frames          = static_cast<int>(buf->len);
        info->one_over_buffer_frames = 1.f / static_cast<float>(buf->len);
        info->sample_rate_adjustment = 1.f;
        info->n_channels             = 2;
        info->samplerate             = io.samplerate;
        info->one_over_samplerate    = 1.0 / io.samplerate;
    }
    return true;
}
bool cb_sample_param_buffer(RecBuffer*, Grainflow::gf_param*, int) { return false; }
void cb_sample_buffer(RecBuffer* buf, int channel, float* __restrict samples, const float* positions,
                      const int size, const float, const float) {
    const int ch = channel & 1;
    for (int i = 0; i < size; i++) samples[i] = buf->read_ch(ch, static_cast<double>(positions[i]));
}
void cb_sample_envelope(RecBuffer*, const bool, const int, const float,
                        float* __restrict samples, const float* __restrict grain_clock, const int size) {
    for (int i = 0; i < size; i++) {
        const float p = clampf(grain_clock[i], 0.f, 1.f);
        samples[i] = g_hann_lut[static_cast<int>(p * kHannLut)]; // Hann via LUT (was per-sample cosf)
    }
}
void cb_write_buffer(RecBuffer*, const int, const float*, const int, const int) {}
void cb_read_buffer (RecBuffer*, int, float* __restrict, int, const int) {}
void cb_clear_buffer(RecBuffer*) {}

Grainflow::gf_i_buffer_reader<RecBuffer, float> make_reader() {
    Grainflow::gf_i_buffer_reader<RecBuffer, float> r;
    r.update_buffer_info  = cb_update_buffer_info;
    r.sample_param_buffer = cb_sample_param_buffer;
    r.sample_buffer       = cb_sample_buffer;
    r.sample_envelope     = cb_sample_envelope;
    r.write_buffer        = cb_write_buffer;
    r.read_buffer         = cb_read_buffer;
    r.clear_buffer        = cb_clear_buffer;
    return r;
}

constexpr int kBlock      = 96;  // platform block == gf Internalblock
// Grains per deck. Conservative: the Phase-0 benchmark measured only bare scattered reads, but the full
// GrainflowLib per-grain machinery adds an exp2f (pitch) AND a cos (envelope) PER SAMPLE per grain plus
// the scheduling/param-sampling overhead - far heavier. 4/deck (8 total) keeps the audio block in budget;
// raise it only after an on-device METER run shows headroom (see docs/dev/graincloud-impl.md).
constexpr int kMaxGrains  = 8;
constexpr int kStreams    = 4;   // spatial streams (decorrelated grain clocks)

using Grain      = Grainflow::gf_grain<RecBuffer, kBlock, float>;
using Collection = Grainflow::gf_grain_collection<RecBuffer, kBlock, float>;

// Per-block scratch for the gf io_config: per-stream inputs + per-grain outputs. Shared by both decks
// (processed sequentially), lives in the arena. Sizes are fixed at the max grain/stream counts.
struct IoScratch {
    float gc[kStreams][kBlock], tr[kStreams][kBlock], fm[kStreams][kBlock], am[kStreams][kBlock];
    float o_out[kMaxGrains][kBlock], o_state[kMaxGrains][kBlock], o_prog[kMaxGrains][kBlock],
          o_play[kMaxGrains][kBlock], o_amp[kMaxGrains][kBlock], o_env[kMaxGrains][kBlock],
          o_bch[kMaxGrains][kBlock], o_sch[kMaxGrains][kBlock];
    float* in_gc[kStreams]; float* in_tr[kStreams]; float* in_fm[kStreams]; float* in_am[kStreams];
    float* p_out[kMaxGrains]; float* p_state[kMaxGrains]; float* p_prog[kMaxGrains];
    float* p_play[kMaxGrains]; float* p_amp[kMaxGrains]; float* p_env[kMaxGrains];
    float* p_bch[kMaxGrains]; float* p_sch[kMaxGrains];
    Grainflow::gf_io_config<float> io;

    void wire() {
        for (int s = 0; s < kStreams; s++) { in_gc[s]=gc[s]; in_tr[s]=tr[s]; in_fm[s]=fm[s]; in_am[s]=am[s]; }
        for (int g = 0; g < kMaxGrains; g++) {
            p_out[g]=o_out[g]; p_state[g]=o_state[g]; p_prog[g]=o_prog[g]; p_play[g]=o_play[g];
            p_amp[g]=o_amp[g]; p_env[g]=o_env[g]; p_bch[g]=o_bch[g]; p_sch[g]=o_sch[g];
        }
        io.grain_clock=in_gc; io.traversal_phasor=in_tr; io.fm=in_fm; io.am=in_am;
        // One shared grain-clock channel: auto_overlap evenly phase-staggers the active grains off it,
        // so onsets are spaced duration/active apart (pan is applied engine-side in the mixdown).
        io.grain_clock_chans=1; io.traversal_phasor_chans=1; io.fm_chans=1; io.am_chans=1;
        io.grain_output=p_out; io.grain_state=p_state; io.grain_progress=p_prog; io.grain_playhead=p_play;
        io.grain_amp=p_amp; io.grain_envelope=p_env; io.grain_buffer_channel=p_bch; io.grain_stream_channel=p_sch;
        io.samplerate = 48000; io.livemode = false; io.block_size = kBlock;
    }
};

constexpr int kDirModes = 3; // grain direction on the Mode switch: 0 forward, 1 reverse, 2 random

// DIAGNOSTIC toggle: pre-load a test tone into both decks at init so the cloud plays at boot without
// recording. Set false to restore normal (empty-at-boot) behaviour. See setup().
constexpr bool kSelfTestTone = false;

} // namespace

// All GrainflowLib types stay inside Impl (PIMPL; see the header note).
struct GraincloudEngine::Impl {
    static constexpr uint8_t kFlashFrames = 6;

    struct Deck {
        Collection* col = nullptr; // placement-new'd in arena
        Grain*      grains = nullptr;
        RecBuffer   rec;

        // knob caches (normalized 0..1)
        float pos_n = 0.3f, spray_n = 0.25f, pitch_n = 0.5f, dur_n = 0.5f,
              density_n = 0.3f, rand_n = 0.3f, mix_n = 1.f;
        int   dir_mode = 0; // 0 forward, 1 reverse, 2 random (from the Reel/Slice/Drift mode switch)
        float cv_semis = 0.f;

        // per-grain equal-power pan
        float panL[kMaxGrains], panR[kMaxGrains];

        // phasor state (per stream)
        double gc_ph[kStreams], tr_ph[kStreams];
        uint32_t rng = 0x1234567u;

        int   active = kMaxGrains; // cached overlap (active grain count) = round(onset_hz * duration)
        float level = 0.f;   // metering
        uint8_t flash = 0;
    };

    ITransport* transport = nullptr;
    float       sr = 48000.f;
    IoScratch*  io = nullptr;
    Deck        deck[DeckRef::Count];
    float       crossfade = 0.5f;
    float       param_cache[static_cast<size_t>(ParamId::Count)][DeckRef::Count] = {};

    static DeckRef::Ref safe(DeckRef::Ref d) { return d < DeckRef::Count ? d : DeckRef::A; }
    static float rand01(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) * (1.f / 16777216.f);
    }
    static float pitch_to_note(float n) { return 24.f + clampf(n, 0.f, 1.f) * 60.f; }

    void setup(const EngineContext& ctx, Arena& ar) {
        transport = ctx.transport;
        sr = ctx.sample_rate;

        for (int k = 0; k <= kHannLut; k++)
            g_hann_lut[k] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(k) / kHannLut);

        io = ar.alloc<IoScratch>(1);
        // CRITICAL: the SDRAM arena is NOT zero-initialized on target (DSY_SDRAM_BSS is not cleared by
        // startup - granular/shuttle memset their allocations for the same reason). The per-grain output
        // arrays must start at 0: GrainflowLib SKIPS sample_buffer when the source buffer is empty/absent
        // (and for disabled grains), so it never writes grain_output - the mixdown would then sum
        // uninitialized garbage. At boot the record buffers ARE empty, so without this every grain emits
        // garbage -> full-scale/NaN noise on every sample. (The host arena is zero-filled, which is why
        // the host test passed.) Zero first, THEN wire the pointers.
        if (io) { std::memset(io, 0, sizeof(IoScratch)); io->wire(); }

        // ~20 s stereo record buffer per deck (float frames), 32 KB-aligned for SDRAM coherence.
        const size_t rec_frames = static_cast<size_t>(20.f * sr);
        for (int d = 0; d < DeckRef::Count; d++) {
            Deck& dk = deck[d];
            Frame* mem = ar.alloc<Frame>(rec_frames, 32768);
            if (mem) { std::memset(mem, 0, rec_frames * sizeof(Frame)); dk.rec.init(mem, rec_frames); }

            // DIAGNOSTIC (temporary): pre-load a 0.5 s 220 Hz tone into BOTH decks so the cloud sounds at
            // BOOT with no recording needed. Splits "is the cloud DSP/output path working?" from "is
            // recording/input working?". Set kSelfTestTone=false (below) to disable. Remove once resolved.
            if (kSelfTestTone && mem) {
                const size_t tlen = static_cast<size_t>(0.5f * sr);
                for (size_t i = 0; i < tlen && i < rec_frames; i++) {
                    const float s = 0.4f * std::sin(2.f * 3.14159265f * 220.f * i / sr);
                    mem[i].l = s; mem[i].r = s;
                }
                dk.rec.len = tlen < rec_frames ? tlen : rec_frames;
            }

            dk.grains = ar.alloc<Grain>(kMaxGrains);
            if (dk.grains) for (int g = 0; g < kMaxGrains; g++) new (&dk.grains[g]) Grain();

            void* cm = ar.alloc<uint8_t>(sizeof(Collection), alignof(Collection));
            dk.col = new (cm) Collection(make_reader());
            dk.col->samplerate = static_cast<int>(sr);
            dk.col->set_storage(dk.grains, kMaxGrains);
            dk.col->resize(kMaxGrains);
            dk.col->stream_set(Grainflow::gf_stream_set_type::automatic_streams, 1);
            dk.col->set_buffer(Grainflow::gf_buffers::buffer, &dk.rec, 0);

            dk.rng = 0xC0FFEEu + 0x9E3779B9u * static_cast<uint32_t>(d);
            for (int s = 0; s < kStreams; s++) {
                dk.gc_ph[s] = static_cast<double>(s) / kStreams; // decorrelate streams
                dk.tr_ph[s] = 0.0;
            }
            compute_pans(static_cast<DeckRef::Ref>(d));
            apply_params(static_cast<DeckRef::Ref>(d));
        }

        using PI = ParamId;
        auto P = [](PI id) { return static_cast<size_t>(id); };
        for (int d = 0; d < DeckRef::Count; d++) {
            const Deck& dk = deck[d];
            param_cache[P(PI::Pos)][d]      = dk.pos_n;
            param_cache[P(PI::Size)][d]     = dk.spray_n;
            param_cache[P(PI::Speed)][d]    = dk.pitch_n;
            param_cache[P(PI::Env)][d]      = dk.dur_n;
            param_cache[P(PI::ModSpeed)][d] = dk.density_n;
            param_cache[P(PI::ModAmp)][d]   = dk.rand_n;
            param_cache[P(PI::Mix)][d]      = dk.mix_n;
        }
    }

    void compute_pans(DeckRef::Ref dref) {
        Deck& dk = deck[safe(dref)];
        uint32_t s = dk.rng ^ 0xABCDu;
        for (int g = 0; g < kMaxGrains; g++) {
            // Per-grain random pan position (stable per index, NOT ordered by index) so any active subset
            // of grains spreads across the field - set_active_grains enables the low indices, and an
            // index-ordered pan would cluster a small cloud on one side. rand_n widens the spread.
            const float h = rand01(s);                       // uniform 0..1, this grain's place in the field
            float p = clampf(0.5f + (h - 0.5f) * dk.rand_n, 0.f, 1.f);
            dk.panL[g] = std::cos(p * 1.5707963f);
            dk.panR[g] = std::sin(p * 1.5707963f);
        }
    }

    // Push the knob caches into the GrainflowLib param model for the whole deck (target 0 = all grains).
    void apply_params(DeckRef::Ref dref) {
        Deck& dk = deck[safe(dref)];
        Collection* c = dk.col;
        if (!c) return;
        using PN = Grainflow::gf_param_name;
        using PT = Grainflow::gf_param_type;

        // Pitch: PITCH knob -> +/-24 semitones, plus V/Oct / MIDI transposition. transform_params turns
        // transpose(base) into rate, so we feed semitones.
        const float semis = (dk.pitch_n - 0.5f) * 48.f + dk.cv_semis;
        c->param_set(0, PN::transpose, PT::base,   semis);
        c->param_set(0, PN::transpose, PT::random, dk.rand_n * 7.f); // pitch spray (semis)

        // Position spray: delay(random) in ms offsets each grain's birth point back from the playhead.
        c->param_set(0, PN::delay, PT::base,   0.f);
        c->param_set(0, PN::delay, PT::random, dk.spray_n * 1500.f); // up to 1.5 s of scatter

        // No space: a grain spans its whole clock period (= the duration), Hann-windowed. Grain LENGTH
        // is set by the grain-clock period (see duration_s / process_deck), not by carving silence here.
        c->param_set(0, PN::space, PT::base, 0.f);

        // Read window over the whole (recorded) buffer.
        c->param_set(0, PN::start_point, PT::base, 0.f);
        c->param_set(0, PN::stop_point,  PT::base, 1.f);
        c->param_set(0, PN::amplitude,   PT::base, 1.f);

        // Direction from the Reel/Slice/Drift mode switch (set_config(Mode)): forward / reverse / random.
        // (gf direction.base: >=1 forward, <=-1 reverse, in-between = random per grain.)
        float dir = 1.f;
        switch (dk.dir_mode) {
            case 1:  dir = -1.f; break; // reverse
            case 2:  dir =  0.f; break; // random per grain
            default: dir =  1.f; break; // forward
        }
        c->param_set(0, PN::direction, PT::base, dir);

        update_overlap(dref); // duration (here) feeds the derived overlap
    }

    // ENV -> grain DURATION in seconds (8 ms .. 1.5 s, exponential). The grain-clock period == duration,
    // so each grain's Hann window is this long, independent of density.
    float duration_s(const Deck& dk) const { return 0.008f * std::pow(187.5f, clampf(dk.dur_n, 0.f, 1.f)); }
    // MODFREQ -> onset rate (grains/sec) = the texture density.
    float onset_hz(const Deck& dk) const { return 1.f + clampf(dk.density_n, 0.f, 1.f) * 79.f; }

    // Overlap = onset_hz * duration = the number of grains that must be live at once for the requested
    // onset rate at the requested length. Capped at kMaxGrains (beyond that the onset rate saturates).
    // With one shared clock + auto_overlap, `active` grains staggered by 1/active give onsets every
    // duration/active s, so onset rate = active/duration = onset_hz exactly (until the cap).
    void update_overlap(DeckRef::Ref dref) {
        Deck& dk = deck[safe(dref)];
        if (!dk.col) return;
        int a = static_cast<int>(onset_hz(dk) * duration_s(dk) + 0.5f);
        // Minimum overlap of 2: a single grain whose Hann fades fully in/out every period is a harsh
        // tremolo/buzz. >=2 evenly-staggered Hann grains sum to a near-constant envelope -> smooth cloud.
        if (a < 2) a = 2; else if (a > kMaxGrains) a = kMaxGrains;
        if (a != dk.active) { dk.active = a; dk.col->set_active_grains(a); }
    }

    void process_deck(DeckRef::Ref dref, const float* in_l, const float* in_r,
                      float* wet_l, float* wet_r, size_t n) {
        Deck& dk = deck[safe(dref)];
        if (!dk.col || !io) { for (size_t i = 0; i < n; i++) { wet_l[i] = wet_r[i] = 0.f; } return; }

        // Record the live input (independent of playback).
        if (dk.rec.recording) for (size_t i = 0; i < n; i++) dk.rec.write(in_l ? in_l[i] : 0.f, in_r ? in_r[i] : 0.f);

        // Nothing recorded yet -> nothing to scatter. Skip the whole GrainflowLib pass: this keeps boot
        // (both buffers empty) nearly free, so the cloud only spends CPU once a deck actually has audio.
        if (dk.rec.len < 4) { for (size_t i = 0; i < n; i++) { wet_l[i] = wet_r[i] = 0.f; } return; }

        // One grain-clock phasor whose PERIOD is the grain duration (f_clock = 1/duration). Traversal is
        // the cloud centre (POS); spray comes from delay(random). auto_overlap staggers the active grains.
        const double gc_rate = (1.0 / static_cast<double>(duration_s(dk))) / sr;
        double gp = dk.gc_ph[0];
        for (size_t i = 0; i < n; i++) {
            gp += gc_rate; if (gp >= 1.0) gp -= 1.0;
            io->gc[0][i] = static_cast<float>(gp);
            io->tr[0][i] = clampf(dk.pos_n, 0.f, 1.f);
            io->fm[0][i] = 0.f;
            io->am[0][i] = 0.f;
        }
        dk.gc_ph[0] = gp;

        io->io.block_size = static_cast<int>(n);
        io->io.samplerate = static_cast<int>(sr);
        dk.col->process(io->io);

        // Mixdown: sum per-grain mono outputs with per-grain equal-power pan into stereo. Normalize by
        // the active (overlapping) grain count so density (overlap) does not change loudness.
        const float gain = 1.4f / std::sqrt(static_cast<float>(dk.active < 1 ? 1 : dk.active));
        float lvl = 0.f;
        for (size_t i = 0; i < n; i++) {
            float l = 0.f, r = 0.f;
            for (int g = 0; g < kMaxGrains; g++) {
                const float o = io->o_out[g][i];
                l += o * dk.panL[g];
                r += o * dk.panR[g];
            }
            l *= gain; r *= gain;
            wet_l[i] = l; wet_r[i] = r;
            const float a = std::fabs(l) + std::fabs(r);
            if (a > lvl) lvl = a;
        }
        dk.level += 0.2f * (0.5f * lvl - dk.level);
        if (lvl > 1e-4f && dk.flash == 0) dk.flash = kFlashFrames;
    }

    void process(const float* const* in, float** out, size_t size) {
        const float* il = in ? in[0] : nullptr;
        const float* ir = in ? in[1] : nullptr;
        float wetA_l[kBlock], wetA_r[kBlock], wetB_l[kBlock], wetB_r[kBlock];

        size_t off = 0;
        while (off < size) {
            size_t n = size - off; if (n > static_cast<size_t>(kBlock)) n = kBlock;
            const float* al = il ? il + off : nullptr;
            const float* ar = ir ? ir + off : nullptr;

            process_deck(DeckRef::A, al, ar, wetA_l, wetA_r, n);
            process_deck(DeckRef::B, al, ar, wetB_l, wetB_r, n);

            const float gA = 1.f - crossfade, gB = crossfade;
            const float mixA = deck[0].mix_n, mixB = deck[1].mix_n;
            for (size_t i = 0; i < n; i++) {
                const float dl = al ? al[i] : 0.f, dr = ar ? ar[i] : 0.f;
                const float aL = dl * (1.f - mixA) + wetA_l[i] * mixA;
                const float aR = dr * (1.f - mixA) + wetA_r[i] * mixA;
                const float bL = dl * (1.f - mixB) + wetB_l[i] * mixB;
                const float bR = dr * (1.f - mixB) + wetB_r[i] * mixB;
                const float oL = aL * gA + bL * gB, oR = aR * gA + bR * gB;
                // Defensive: never let a non-finite slip to the codec (harsh noise / speaker risk).
                out[0][off + i] = std::isfinite(oL) ? softclip(oL) : 0.f;
                out[1][off + i] = std::isfinite(oR) ? softclip(oR) : 0.f;
            }
            off += n;
        }
        for (int d = 0; d < DeckRef::Count; d++) if (deck[d].flash > 0) deck[d].flash--;
    }

    // --- params ---
    void set_param(ParamId id, DeckRef::Ref dref, float v) {
        const auto d = safe(dref);
        param_cache[static_cast<size_t>(id)][d] = v;
        Deck& dk = deck[d];
        bool repan = false;
        switch (id) {
            case ParamId::Pos:    dk.pos_n = v; break; // used live in process (no apply needed)
            case ParamId::Size:   dk.spray_n = v; apply_params(d); break;
            case ParamId::Speed:  dk.pitch_n = v; apply_params(d); break;
            case ParamId::Env:    dk.dur_n = v; apply_params(d); break;
            case ParamId::ModAmp: dk.rand_n = v; repan = true; apply_params(d); break;
            case ParamId::Mix:    dk.mix_n = v; break;
            case ParamId::Crossfade: crossfade = clampf(v, 0.f, 1.f); break;
            default: break;
        }
        if (repan) compute_pans(d);
    }

    float param(ParamId id, DeckRef::Ref d) const { return param_cache[static_cast<size_t>(id)][safe(d)]; }

    void set_mod_speed(DeckRef::Ref dref, float v) {
        const auto d = safe(dref);
        deck[d].density_n = clampf(v, 0.f, 1.f);
        param_cache[static_cast<size_t>(ParamId::ModSpeed)][d] = v;
        update_overlap(d); // density -> derived overlap (active grain count)
    }

    // The Reel/Slice/Drift mode switch selects grain direction (forward / reverse / random).
    bool set_config(ConfigId id, DeckRef::Ref dref, int value) {
        if (id != ConfigId::Mode) return false;
        const auto d = safe(dref);
        const int m = value < 0 ? 0 : (value >= kDirModes ? kDirModes - 1 : value);
        if (deck[d].dir_mode == m) return false;
        deck[d].dir_mode = m; apply_params(d);
        return true;
    }

    DeckRef::Ref handle_midi_note(uint8_t channel, uint8_t note) {
        const DeckRef::Ref d = (channel & 1) ? DeckRef::B : DeckRef::A;
        deck[d].pitch_n = clampf((static_cast<float>(note) - 24.f) / 60.f, 0.f, 1.f);
        param_cache[static_cast<size_t>(ParamId::Speed)][d] = deck[d].pitch_n;
        apply_params(d);
        return d;
    }

    void cv_voct(DeckRef::Ref dref, float value) { deck[safe(dref)].cv_semis = value; apply_params(safe(dref)); }

    void cv_crossfade(float value) { crossfade = clampf(value, 0.f, 1.f); }

    // --- record / clear pads ---
    void on_record_pad(DeckRef::Ref dref, bool /*reverse*/) {
        // The platform calls this for Alt+Play (reverse=false) AND Alt+Rev (reverse=true). graincloud
        // records the live input either way (no internal source), so toggle on both - the old
        // `if (reverse) return` made Alt+Rev a no-op, which read as "the Rev pad doesn't record".
        Deck& dk = deck[safe(dref)];
        if (dk.rec.recording) dk.rec.stop_record(); else dk.rec.start_record();
    }
    void clear_buffer(DeckRef::Ref dref) { deck[safe(dref)].rec.clear(); }

    // --- storage port ---
    bool     audio_is_empty(DeckRef::Ref d) { return deck[safe(d)].rec.empty(); }
    uint8_t* audio_data(DeckRef::Ref d) { return reinterpret_cast<uint8_t*>(deck[safe(d)].rec.data); }
    size_t   audio_recorded_bytes(DeckRef::Ref d) { return deck[safe(d)].rec.len * sizeof(Frame); }
    size_t   audio_capacity_bytes(DeckRef::Ref d) { return deck[safe(d)].rec.cap * sizeof(Frame); }
    void     audio_apply_loaded(DeckRef::Ref d, size_t frames) {
        Deck& dk = deck[safe(d)];
        dk.rec.len = frames < dk.rec.cap ? frames : dk.rec.cap;
    }

    void render(DisplayModel& m) {
        m.clear();
        static const uint32_t kCol[2] = { 0x00e0a0, 0xa060ff }; // A teal, B violet
        for (int d = 0; d < DeckRef::Count; d++) {
            Deck& dk = deck[d];
            const uint32_t col = kCol[d];
            float level = dk.level * 1.5f; if (level > 1.f) level = 1.f;
            m.ring[d].set_hex_color(col);
            if (level > 1e-3f) m.ring[d].set_segment(0.f, level * 0.999f);
            m.ring[d].set_point_hex_color(0xffffff);
            int pled = static_cast<int>(clampf(dk.pos_n, 0.f, 1.f) * 31.f + 0.5f);
            m.ring[d].set_point(static_cast<uint8_t>(std::min(std::max(pled, 0), 31)), 1.f);
            m.ring[d].set_updated();
            const bool rec = dk.rec.recording;
            m.play[d] = { rec ? 0xff2020u : col, (rec || dk.flash > 0) ? 1.f : 0.12f };
        }
    }
};

// --- GraincloudEngine: thin forwarders to Impl (in the SDRAM arena) -----------------------------
void GraincloudEngine::init(const EngineContext& ctx)
{
    Arena ar(ctx.arena);
    void* mem = ar.alloc<uint8_t>(sizeof(Impl), alignof(Impl));
    _p = new (mem) Impl();
    _p->setup(ctx, ar);
}

void GraincloudEngine::process(const float* const* in, float** out, size_t size)
{
    if (_p) _p->process(in, out, size);
}

void  GraincloudEngine::set_param(ParamId id, DeckRef::Ref d, float v) { if (_p) _p->set_param(id, d, v); }
float GraincloudEngine::param(ParamId id, DeckRef::Ref d) const        { return _p ? _p->param(id, d) : 0.f; }
void  GraincloudEngine::set_mod_speed(DeckRef::Ref d, float v, bool)   { if (_p) _p->set_mod_speed(d, v); }
bool  GraincloudEngine::set_config(ConfigId id, DeckRef::Ref d, int v) { return _p ? _p->set_config(id, d, v) : false; }

DeckRef::Ref GraincloudEngine::handle_midi_note(uint8_t ch, uint8_t note)
{
    return _p ? _p->handle_midi_note(ch, note) : DeckRef::Count;
}
void GraincloudEngine::cv_voct(DeckRef::Ref d, float v)        { if (_p) _p->cv_voct(d, v); }
void GraincloudEngine::on_record_pad(DeckRef::Ref d, bool rev) { if (_p) _p->on_record_pad(d, rev); }
void GraincloudEngine::clear_buffer(DeckRef::Ref d)            { if (_p) _p->clear_buffer(d); }

bool     GraincloudEngine::audio_is_empty(DeckRef::Ref d)        { return _p ? _p->audio_is_empty(d) : true; }
uint8_t* GraincloudEngine::audio_data(DeckRef::Ref d)            { return _p ? _p->audio_data(d) : nullptr; }
size_t   GraincloudEngine::audio_recorded_bytes(DeckRef::Ref d)  { return _p ? _p->audio_recorded_bytes(d) : 0; }
size_t   GraincloudEngine::audio_capacity_bytes(DeckRef::Ref d)  { return _p ? _p->audio_capacity_bytes(d) : 0; }
void     GraincloudEngine::audio_apply_loaded(DeckRef::Ref d, size_t f) { if (_p) _p->audio_apply_loaded(d, f); }

void GraincloudEngine::render(DisplayModel& m) { if (_p) _p->render(m); }
