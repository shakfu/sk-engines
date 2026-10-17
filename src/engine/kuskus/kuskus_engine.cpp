// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/kuskus/kuskus_engine.h"
#include "engine/indicators.h"
#include "engine/arena.h"

#include "beads/dsp/processor.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>

using namespace spotykach;

namespace {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Beads' pot laws (beads/cv_reader.h, PotChannel::Process), applied to the platform's 0..1 knob.
// The attenuverter law has a 3% dead band at noon and reaches +/-1 at the ends.
inline float attenuverter_law(float v) {
    v -= 0.5f;
    return (2.f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.f);
}

// PITCH: +/-24 semitones with flat detents at 0, 1, 3, 4, 7, 12 and 24.
inline float detune_law(float v) {
    float detune = 2.f * clampf(v, 0.f, 1.f) - 1.f;
    const float sign = detune < 0.f ? -1.f : 1.f;
    detune = detune * sign * 6.f;
    const int   i = std::min(static_cast<int>(detune), 6);
    float       f = detune - static_cast<float>(i);
    static const float kIntervals[8] = { 0.f, 1.f, 3.f, 4.f, 7.f, 12.f, 24.f, 24.f };
    f = std::max((f - 0.33333333f) * 1.5f, 0.f);
    return stmlib::Crossfade(kIntervals[i], kIntervals[i + 1], stmlib::SmoothStep(f)) * sign;
}

} // namespace

struct KuskusEngine::Impl {
    // Beads runs its control path at 4 kHz (kBlockRate in beads.cc): 12 frames at 48 kHz. Every
    // interpolator, one-pole and random-walk constant inside Processor assumes that block size.
    static constexpr size_t   kSub        = 12;
    static constexpr size_t   kMaxBlock   = 96 * 4;
    static constexpr size_t   kBankBytes  = 262144;   // DelayLine bank_size
    static constexpr int      kBanks      = 3;
    static constexpr size_t   kReverbWords = 32768;
    static constexpr uint32_t kSeedDelay  = 120;      // 2.5 ms, as beads.cc delays the SEED jack
    static constexpr uint32_t kSeedHigh   = 96;       // 2 ms gate per seed event
    static constexpr uint32_t kTapHigh    = 960;      // 20 ms SEED "press" per Play A tap
    static constexpr uint32_t kPatchedFor = 4 * 48000; // seed jack reads as patched for 4 s after an edge
    static constexpr float    kFadeStep   = 1.f / 96.f;

    // Media in Beads' own enum order (Processor::Media), with its UI palette (ui.cc).
    static constexpr uint32_t kMediaColor[4] = { 0x800030, 0xff9000, 0x00ffc0, 0xc0c0d0 };

    // Media changes and mono changes run Processor::Configure, which zero-fills 768 KB. It runs on
    // the main loop (prepare) while the audio path outputs silence.
    enum class Cfg : uint8_t { Running, FadeOut, Muted };

    beads::Processor proc;
    const ITimeSource* time = nullptr;

    // Deck A: the Beads knobs. Deck B: attenuverters and the mix-section extras.
    float pitch_k = 0.5f, time_k = 0.5f, size_k = 0.5f, shape_k = 0.5f, density_k = 0.3f;
    float feedback_k = 0.f, drywet_k = 0.5f;
    float pitch_att = 0.5f, time_att = 0.5f, size_att = 0.5f, shape_att = 0.5f;
    float gain_k = 0.5f, reverb_k = 0.2f, level_k = 0.5f;

    // CV, all additive to the knob base (no jack detection: Beads' has_cv is always false here).
    float cv_semis = 0.f, cv_size = 0.f, cv_time = 0.f, cv_shape = 0.f, cv_density = 0.f, cv_drywet = 0.f;
    float midi_semis = 0.f;

    float param_cache[static_cast<size_t>(ParamId::Count)][DeckRef::Count] = {};

    // Panel state.
    int   media_sw = 0;          // deck A mode switch position
    bool  vintage_sw = false;    // deck B mode switch at Drift: Vintage Digital
    Route _route = Route::Stereo;
    std::atomic<bool> freeze { false };
    std::atomic<bool> latched { true };     // Beads boots latched (ui.cc)
    std::atomic<bool> held { false };       // Flux A held = SEED held
    std::atomic<bool> clock_follow { false };
    std::atomic<uint32_t> seed_events { 0 };
    std::atomic<uint32_t> taps { 0 };

    // Media/mono reconfiguration handshake (main loop <-> audio).
    std::atomic<Cfg> cfg { Cfg::Running };
    beads::Processor::Media want_media = beads::Processor::MEDIA_DIGITAL;
    bool  want_mono = false;
    float fade = 0.f;

    // Seed gate stream (audio thread only).
    uint32_t seed_delay = 0, seed_high = 0, tap_high = 0, since_edge = kPatchedFor;
    stmlib::GateFlags prev_seed = stmlib::GATE_FLAG_LOW;

    // Display state, written by the audio thread.
    float out_level = 0.f, in_level = 0.f, reserve = 1.f;
    std::atomic<uint32_t> seed_flash { 0 };
    std::atomic<uint32_t> gate_out_edges { 0 };
    bool  gate_prev = false;

    static DeckRef::Ref safe(DeckRef::Ref d) { return d < DeckRef::Count ? d : DeckRef::A; }

    void setup(const EngineContext& ctx, Arena& ar) {
        time = ctx.time;
        uint8_t* banks = ar.alloc<uint8_t>(kBankBytes * kBanks, 32);
        uint16_t* reverb = ar.alloc<uint16_t>(kReverbWords, 32);
        uint8_t* bank_ptrs[kBanks];
        for (int i = 0; i < kBanks; i++) bank_ptrs[i] = banks + i * kBankBytes;
        proc.Init(bank_ptrs, reverb);   // also Configure(MEDIA_DIGITAL, false)

        if (ctx.transport) {
            ctx.transport->set_on_tick([this](const TransportTick& t) {
                if (t.quarter && clock_follow.load(std::memory_order_relaxed)) seed();
            });
        }

        using P = ParamId;
        auto set = [this](P id, DeckRef::Ref d, float v) { param_cache[static_cast<size_t>(id)][d] = v; };
        set(P::Speed, DeckRef::A, pitch_k);      set(P::Speed, DeckRef::B, pitch_att);
        set(P::Pos,   DeckRef::A, time_k);       set(P::Pos,   DeckRef::B, time_att);
        set(P::Size,  DeckRef::A, size_k);       set(P::Size,  DeckRef::B, size_att);
        set(P::Env,   DeckRef::A, shape_k);      set(P::Env,   DeckRef::B, shape_att);
        set(P::ModSpeed, DeckRef::A, density_k); set(P::ModSpeed, DeckRef::B, gain_k);
        set(P::ModAmp, DeckRef::A, feedback_k);  set(P::ModAmp, DeckRef::B, reverb_k);
        set(P::Mix,   DeckRef::A, drywet_k);     set(P::Mix,   DeckRef::B, level_k);
        set(P::Crossfade, DeckRef::A, drywet_k);
    }

    beads::Processor::Media media_for_switches() const {
        if (vintage_sw) return beads::Processor::MEDIA_VINTAGE_DIGITAL;
        switch (media_sw) {
            case 1:  return beads::Processor::MEDIA_CLEAN_TAPE;
            case 2:  return beads::Processor::MEDIA_DIRTY_TAPE;
            default: return beads::Processor::MEDIA_DIGITAL;
        }
    }

    // Main loop: queue a Configure if the switches no longer match the running processor.
    void request_configure() {
        want_media = media_for_switches();
        want_mono  = _route == Route::DoubleMono;
        if (cfg.load() == Cfg::Running &&
            (want_media != proc.media() || want_mono != proc.mono())) {
            cfg.store(Cfg::FadeOut);
        }
    }

    void prepare() {
        if (cfg.load() != Cfg::Muted) return;
        proc.Configure(want_media, want_mono);
        cfg.store(Cfg::Running);
        // The switches may have moved again while Configure ran.
        request_configure();
    }

    void seed() {
        seed_events.fetch_add(1, std::memory_order_relaxed);
        seed_flash.store(6, std::memory_order_relaxed);
    }

    static float pick_peak(float peak, float x) { x = x < 0.f ? -x : x; return x > peak ? x : peak; }

    void process(const float* const* in, float** out, size_t size) {
        const Cfg state = cfg.load();
        if (state == Cfg::Muted || size > kMaxBlock) {
            std::fill(out[0], out[0] + size, 0.f);
            std::fill(out[1], out[1] + size, 0.f);
            fade = 0.f;
            return;
        }
        const float fade_target = state == Cfg::FadeOut ? 0.f : 1.f;

        // Per-sample gate streams for the block: SEED jack (delayed 2.5 ms) and FREEZE.
        if (seed_events.exchange(0, std::memory_order_relaxed)) {
            seed_delay = kSeedDelay;
            since_edge = 0;
        }
        if (taps.exchange(0, std::memory_order_relaxed)) tap_high = kTapHigh;

        stmlib::GateFlags seed_flags[kMaxBlock];
        bool              freeze_flags[kMaxBlock];
        const bool        frz = freeze.load(std::memory_order_relaxed);
        for (size_t i = 0; i < size; i++) {
            if (seed_delay && --seed_delay == 0) seed_high = kSeedHigh;
            const bool high = seed_high > 0;
            if (seed_high) seed_high--;
            seed_flags[i] = prev_seed = stmlib::ExtractGateFlags(prev_seed, high);
            freeze_flags[i] = frz;
        }
        // Beads only passes the SEED stream when the jack is patched; here "patched" means an edge
        // arrived within the divider's 4 s resynchronisation window.
        const bool patched = since_edge < kPatchedFor;
        since_edge = std::min<uint32_t>(since_edge + static_cast<uint32_t>(size), kPatchedFor);

        beads::Parameters p;
        p.time  = { clampf(time_k + cv_time, 0.f, 1.f),   attenuverter_law(time_att),  0.f, false };
        p.size  = { clampf(size_k + cv_size, 0.f, 1.f),   attenuverter_law(size_att),  0.f, false };
        p.shape = { clampf(shape_k + cv_shape, 0.f, 1.f), attenuverter_law(shape_att), 0.f, false };
        p.pitch = { detune_law(pitch_k) + cv_semis + midi_semis, attenuverter_law(pitch_att), 0.f, false };
        p.monophonic_mode = false;

        beads::ModulatedParameter density;
        density.base = clampf(density_k, 0.f, 1.f);
        density.cv   = cv_density;

        beads::Processor::MixParameters mix;
        mix.feedback      = clampf(feedback_k, 0.f, 1.f);
        mix.dry_wet       = clampf(drywet_k + cv_drywet, 0.f, 1.f);
        mix.reverb_amount = clampf(reverb_k, 0.f, 1.f);
        mix.output_gate   = _route == Route::GenerativeStereo;

        // MODFREQ B: input gain over -12..+12 dB, unity at noon. SOS B: output level, unity at noon.
        const float in_gain  = std::pow(4.f, 2.f * clampf(gain_k, 0.f, 1.f) - 1.f);
        const float out_gain = 2.f * clampf(level_k, 0.f, 1.f);

        float in_peak = 0.f, out_peak = 0.f;
        bool gate_rise = false;
        for (size_t off = 0; off < size; off += kSub) {
            const size_t n = std::min(kSub, size - off);
            beads::FloatFrame fin[kSub], fout[kSub];
            for (size_t i = 0; i < n; i++) {
                fin[i].l = in[0][off + i] * in_gain;
                fin[i].r = in[1][off + i] * in_gain;
                in_peak = pick_peak(pick_peak(in_peak, fin[i].l), fin[i].r);
            }

            beads::SeedButtonState button = latched.load(std::memory_order_relaxed)
                ? beads::SEED_BUTTON_STATE_LOCKED
                : ((held.load(std::memory_order_relaxed) || tap_high) ? beads::SEED_BUTTON_STATE_ON
                                                                       : beads::SEED_BUTTON_STATE_OFF);
            tap_high = tap_high > n ? tap_high - static_cast<uint32_t>(n) : 0;

            proc.Process(button, patched ? &seed_flags[off] : nullptr, &freeze_flags[off],
                         density, p, mix, fin, fout, n);

            for (size_t i = 0; i < n; i++) {
                fade += clampf(fade_target - fade, -kFadeStep, kFadeStep);
                float l = fout[i].l, r = fout[i].r;
                if (mix.output_gate) {
                    // Beads writes -0.8 for a gate on R (its output stage inverts); emit +0.8 here.
                    const bool g = r != 0.f;
                    gate_rise |= g && !gate_prev;
                    gate_prev = g;
                    r = g ? 0.8f : 0.f;
                    l *= out_gain * fade;
                } else {
                    l *= out_gain * fade;
                    r *= out_gain * fade;
                }
                out[0][off + i] = l;
                out[1][off + i] = r;
                out_peak = pick_peak(out_peak, l);
                if (!mix.output_gate) out_peak = pick_peak(out_peak, r);
            }
        }
        if (gate_rise) gate_out_edges.fetch_add(1, std::memory_order_relaxed);

        out_level += 0.3f * (out_peak - out_level);
        in_level  += 0.3f * (in_peak - in_level);
        reserve = proc.overwrite_reserve();
        if (state == Cfg::FadeOut && fade <= 0.f) cfg.store(Cfg::Muted);
    }

    void set_param(ParamId id, DeckRef::Ref deckr, float v) {
        const auto d = safe(deckr);
        param_cache[static_cast<size_t>(id)][d] = v;
        const bool a = d == DeckRef::A;
        switch (id) {
            case ParamId::Speed:  (a ? pitch_k    : pitch_att) = v; break;
            case ParamId::Pos:    (a ? time_k     : time_att)  = v; break;
            case ParamId::Size:   (a ? size_k     : size_att)  = v; break;
            case ParamId::Env:    (a ? shape_k    : shape_att) = v; break;
            case ParamId::ModAmp: (a ? feedback_k : reverb_k)  = v; break;
            case ParamId::Mix:    (a ? drywet_k   : level_k)   = v; break;
            // The crossfader and SOS A both write dry/wet; the last one moved wins.
            case ParamId::Crossfade:
                drywet_k = v;
                param_cache[static_cast<size_t>(ParamId::Mix)][DeckRef::A] = v;
                break;
            default: break;
        }
    }

    void set_mod_speed(DeckRef::Ref deckr, float v) {
        const auto d = safe(deckr);
        param_cache[static_cast<size_t>(ParamId::ModSpeed)][d] = v;
        (d == DeckRef::A ? density_k : gain_k) = v;
    }

    bool set_config(ConfigId id, DeckRef::Ref deckr, int value) {
        if (id == ConfigId::Route) {
            const Route r = value == 2 ? Route::GenerativeStereo
                          : value == 1 ? Route::DoubleMono
                                       : Route::Stereo;
            if (r != _route) { _route = r; request_configure(); }
            return false;
        }
        if (id == ConfigId::Mode) {
            const auto d = safe(deckr);
            bool changed = false;
            if (d == DeckRef::A) {
                changed = media_sw != value;
                media_sw = value;
            } else {
                const bool v = value == 2;
                changed = vintage_sw != v;
                vintage_sw = v;
            }
            if (changed) request_configure();
            return changed;
        }
        return false;
    }

    float param(ParamId id, DeckRef::Ref d) const { return param_cache[static_cast<size_t>(id)][safe(d)]; }

    DeckRef::Ref handle_midi_note(uint8_t, uint8_t note) {
        midi_semis = static_cast<float>(note) - 60.f;
        seed();
        return DeckRef::A;
    }

    // Play A taps SEED (and unlatches it, as Beads' button does). Play B toggles FREEZE.
    // Rev A toggles transport clock-follow.
    bool on_play_pad(DeckRef::Ref d, bool reverse) {
        if (safe(d) == DeckRef::A) {
            if (reverse) {
                clock_follow.store(!clock_follow.load());
            } else {
                latched.store(false);
                taps.fetch_add(1, std::memory_order_relaxed);
                seed_flash.store(6, std::memory_order_relaxed);
            }
        } else if (!reverse) {
            freeze.store(!freeze.load());
        }
        return false;
    }

    // Alt + Play A toggles the SEED latch.
    void on_record_pad(DeckRef::Ref d, bool reverse) {
        if (safe(d) == DeckRef::A && !reverse) latched.store(!latched.load());
    }

    // Flux A held = SEED held. The platform reports both edges for the fx pads, not for Play.
    void set_fx(DeckRef::Ref d, FxKind kind, bool on) {
        if (safe(d) != DeckRef::A || kind != FxKind::Flux) return;
        if (on) latched.store(false);
        held.store(on);
    }

    void on_gate_trigger(DeckRef::Ref d) {
        if (safe(d) == DeckRef::A) seed();
        else freeze.store(!freeze.load());
    }

    bool gate_out_triggered(DeckRef::Ref d) {
        if (safe(d) != DeckRef::A) return false;
        uint32_t n = gate_out_edges.load(std::memory_order_relaxed);
        while (n && !gate_out_edges.compare_exchange_weak(n, n - 1)) {}
        return n != 0;
    }

    void cv_voct(DeckRef::Ref d, float v)     { if (safe(d) == DeckRef::A) cv_semis = v; }
    void cv_size_pos(DeckRef::Ref d, float v) { (safe(d) == DeckRef::A ? cv_size : cv_time) = v; }
    void cv_mix(DeckRef::Ref d, float v)      { (safe(d) == DeckRef::A ? cv_shape : cv_density) = v; }
    void cv_crossfade(float v)                { cv_drywet = v; }

    void render(DisplayModel& m) {
        m.clear();
        const uint32_t now = time ? time->now_ms() : 0;
        const uint32_t media_col = kMediaColor[media_for_switches()];

        // Ring A: output level in the media colour (dimmed as the frozen buffer nears overwrite),
        // with the TIME position as a dot.
        ring::level(m.ring[0], out_level * 1.5f, media_col, 0.3f + 0.7f * reserve);
        ring::playhead(m.ring[0], clampf(time_k + cv_time, 0.f, 0.999f));
        m.ring[0].set_updated();

        // Ring B: input level; a full dim ring while frozen (the input no longer reaches the buffer).
        if (freeze.load()) ring::window(m.ring[1], 0.f, 1.f, pal::kCyan, 0.35f);
        ring::level(m.ring[1], in_level * 1.5f, pal::kWhite);
        m.ring[1].set_updated();

        const uint32_t flash = seed_flash.load(std::memory_order_relaxed);
        if (flash) seed_flash.store(flash - 1, std::memory_order_relaxed);
        if (latched.load()) m.play[0] = { media_col, flash ? 1.f : motion::breathe(now, 0.25f, 0.75f, 2048) };
        else                m.play[0] = { media_col, (flash || held.load()) ? 1.f : 0.1f };
        m.play[1] = { pal::kCyan, freeze.load() ? 1.f : 0.1f };
        if (clock_follow.load()) m.rev[0] = { pal::kGreen, 0.8f };
        m.cycle[0] = { media_col, flash ? 1.f : 0.f };

        led::route_leds(m, _route);
        led::fader_balance(m, clampf(drywet_k + cv_drywet, 0.f, 1.f));
    }
};

constexpr uint32_t KuskusEngine::Impl::kMediaColor[4];

// --- KuskusEngine: thin forwarders to Impl (which lives in the SDRAM arena) ----------------------
void KuskusEngine::init(const EngineContext& ctx)
{
    Arena ar(ctx.arena);
    void* mem = ar.alloc<uint8_t>(sizeof(Impl), alignof(Impl));
    _p = new (mem) Impl();
    _p->setup(ctx, ar);
}

void KuskusEngine::prepare() { if (_p) _p->prepare(); }

void KuskusEngine::process(const float* const* in, float** out, size_t size)
{
    if (_p) _p->process(in, out, size);
}

void  KuskusEngine::set_param(ParamId id, DeckRef::Ref d, float v) { if (_p) _p->set_param(id, d, v); }
float KuskusEngine::param(ParamId id, DeckRef::Ref d) const        { return _p ? _p->param(id, d) : 0.f; }
void  KuskusEngine::set_mod_speed(DeckRef::Ref d, float v, bool)   { if (_p) _p->set_mod_speed(d, v); }
bool  KuskusEngine::set_config(ConfigId id, DeckRef::Ref d, int v) { return _p ? _p->set_config(id, d, v) : false; }
Route KuskusEngine::route() const                                  { return _p ? _p->_route : Route::Stereo; }
float KuskusEngine::mix() const                                    { return _p ? _p->drywet_k : 0.5f; }

DeckRef::Ref KuskusEngine::handle_midi_note(uint8_t ch, uint8_t note)
{
    return _p ? _p->handle_midi_note(ch, note) : DeckRef::Count;
}
void KuskusEngine::set_fx(DeckRef::Ref d, FxKind k, bool on)  { if (_p) _p->set_fx(d, k, on); }
bool KuskusEngine::on_play_pad(DeckRef::Ref d, bool rev)      { return _p ? _p->on_play_pad(d, rev) : false; }
void KuskusEngine::on_record_pad(DeckRef::Ref d, bool rev)    { if (_p) _p->on_record_pad(d, rev); }
void KuskusEngine::on_gate_trigger(DeckRef::Ref d)            { if (_p) _p->on_gate_trigger(d); }
bool KuskusEngine::gate_out_triggered(DeckRef::Ref d)         { return _p ? _p->gate_out_triggered(d) : false; }
void KuskusEngine::cv_mix(DeckRef::Ref d, float v)            { if (_p) _p->cv_mix(d, v); }
void KuskusEngine::cv_size_pos(DeckRef::Ref d, float v)       { if (_p) _p->cv_size_pos(d, v); }
void KuskusEngine::cv_voct(DeckRef::Ref d, float v)           { if (_p) _p->cv_voct(d, v); }
void KuskusEngine::cv_crossfade(float v)                      { if (_p) _p->cv_crossfade(v); }
void KuskusEngine::render(DisplayModel& m)                    { if (_p) _p->render(m); }
