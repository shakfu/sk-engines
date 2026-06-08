#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "engine/istreamdeck.h"
#include "nocopy.h"

#include <cstdint>
#include <cmath>

namespace spotykach {

// Dual streaming tape deck: two INDEPENDENT mono decks (A/B), each playing or recording its own
// arbitrarily long file on the SD card, bypassing the in-SDRAM loop-length cap. A deck is play-XOR-
// record (no overdub), so the two run like a pair of record decks - e.g. play deck A while recording
// deck B, then play both together and beat-match by ear with each deck's PITCH (varispeed).
//
// Audio I/O (per the hardware: two MONO inputs A/B, two MONO outputs, stereo headphone monitor):
//   - Deck A records INPUT A (in[0]); deck B records INPUT B (in[1]). Independent, never summed.
//   - The two decks are mixed to a stereo bus (out[0] = L, out[1] = R) that drives the headphone, and
//     the individual outputs tap the same bus. The routing switch + POS knobs + mix fader place them:
//
//   ROUTING SWITCH (set_config ConfigId::Route, mirrors the panel L/C/R):
//     - LEFT  (DoubleMono):       each deck panned by ITS OWN POS knob (equal-power).
//     - CENTRE(Stereo):           both decks centered (summed equally to both outputs).
//     - RIGHT (GenerativeStereo): each deck at a random pan position (re-rolled on entering the mode).
//   MIX FADER (ParamId::Crossfade): blends deck A vs deck B in all modes (centre = both full).
//
// The engine is thin - in process() (the audio ISR) it only moves float frames between the platform's
// lock-free per-deck rings and the audio buffers, then applies the (per-block) pan/blend gains; the slow
// FatFs I/O happens in the platform's StreamDeck pump (main loop). Controls: each deck's Play pad
// toggles playback, Alt+Play toggles recording (one fixed mono file per deck: /TAPEA.WAV, /TAPEB.WAV).
class TapeEngine : public IEngine {
public:
    TapeEngine() = default;
    ~TapeEngine() override = default;

    void init(const EngineContext& ctx) override { _stream = ctx.stream; _time = ctx.time; }

    // Main-loop housekeeping: push each deck's loop-enable to the stream (so it seeks to the top vs
    // stops at EOF), and action a Frippertronics fade-out that asked to stop (stop() touches FatFs, so
    // it must run here, not in the audio ISR that set the flag).
    void prepare() override {
        if (!_stream) return;
        for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
            const int i = (d == DeckRef::A) ? 0 : 1;
            _stream->set_loop(d, _loop_mode[i] != Loop::None);
            if (_want_stop[i]) { _want_stop[i] = false; _stream->stop(d); }
        }
    }

    void process(const float* const* in, float** out, size_t size) override {
        const size_t n = size > kMaxFrames ? kMaxFrames : size;
        if (!_stream) { for (int c = 0; c < 2; c++) for (size_t i = 0; i < n; i++) out[c][i] = 0.f; return; }

        // Each deck -> a mono stream (playback / record-monitor / silence). Deck A reads input A, B reads B.
        float monoA[kMaxFrames], monoB[kMaxFrames];
        _render_deck(DeckRef::A, in, 0, monoA, n);
        _render_deck(DeckRef::B, in, 1, monoB, n);

        // Per-block output gains: per-deck pan (selected by the routing switch) scaled by the mix-fader
        // A/B blend. Pan/blend gains are precomputed on knob change, so the ISR loop is just multiplies.
        float pLa, pRa, pLb, pRb;
        switch (_route) {
            case Route::DoubleMono:                          // LEFT: each deck panned by its POS knob
                pLa = _panL[0]; pRa = _panR[0]; pLb = _panL[1]; pRb = _panR[1]; break;
            case Route::GenerativeStereo:                    // RIGHT: random pan per deck
                pLa = _rndL[0]; pRa = _rndR[0]; pLb = _rndL[1]; pRb = _rndR[1]; break;
            case Route::Stereo: default:                     // CENTRE: both decks centered
                pLa = pRa = pLb = pRb = kCenterGain; break;
        }
        // Total per-deck gains = MIX-knob volume x mix-fader A/B blend x pan (L/R).
        const float La = _gain[0] * _gA * pLa, Ra = _gain[0] * _gA * pRa,
                    Lb = _gain[1] * _gB * pLb, Rb = _gain[1] * _gB * pRb;
        for (size_t i = 0; i < n; i++) {
            out[0][i] = monoA[i] * La + monoB[i] * Lb;
            out[1][i] = monoA[i] * Ra + monoB[i] * Rb;
        }
    }

    Capabilities capabilities() const override { return CapOwnDisplay | CapDualDeck; }

    // PITCH -> per-deck varispeed (exp2((v-0.5)*2) = 0.5x..2x, +/-1 octave, pitch+speed linked).
    // POS   -> per-deck pan (equal-power; 0 = hard left, 1 = hard right, 0.5 = centre). Used in the LEFT
    //          (DoubleMono) routing; centre/random modes override it.
    // Crossfade (mix fader) -> A/B blend (DJ-style: centre = both full, ends = one deck only).
    // MIX knob -> per-deck playback volume. ENV knob -> per-deck loop mode (4 quadrants from CCW):
    // none / plain loop / faded loop (seam fade) / Frippertronics (loop decays away).
    void set_param(ParamId id, DeckRef::Ref d, float v) override {
        const int i = (d == DeckRef::A) ? 0 : 1;
        if (id == ParamId::Speed) { _speed_n[i] = v; _speed[i] = std::exp2f((v - 0.5f) * 2.f); }
        else if (id == ParamId::Pos) { _pan[i] = v; _panL[i] = std::cos(v * kHalfPi); _panR[i] = std::sin(v * kHalfPi); }
        else if (id == ParamId::Crossfade) { _xfade = v; _gA = v <= 0.5f ? 1.f : 2.f * (1.f - v);
                                                          _gB = v >= 0.5f ? 1.f : 2.f * v; }
        else if (id == ParamId::Mix) { _gain[i] = v; }
        else if (id == ParamId::Env) { _env_n[i] = v;
            _loop_mode[i] = v < 0.25f ? Loop::None  : v < 0.5f ? Loop::Plain
                          : v < 0.75f ? Loop::Faded : Loop::Fripp; }
    }
    float param(ParamId id, DeckRef::Ref d) const override {
        const int i = (d == DeckRef::A) ? 0 : 1;
        if (id == ParamId::Speed) return _speed_n[i];
        if (id == ParamId::Pos)   return _pan[i];
        if (id == ParamId::Mix)   return _gain[i];
        if (id == ParamId::Env)   return _env_n[i];
        return 0.f;
    }

    // Routing switch (mirrors the granular int mapping so the panel L/C/R reads the same):
    // 0 = Stereo (centre), 1 = DoubleMono (left), 2 = GenerativeStereo (right).
    bool set_config(ConfigId id, DeckRef::Ref, int value) override {
        if (id == ConfigId::Route) {
            _route = (value == 2) ? Route::GenerativeStereo
                   : (value == 1) ? Route::DoubleMono
                                  : Route::Stereo;
            if (_route == Route::GenerativeStereo) _roll_random_pans();
        }
        return false;
    }
    Route route() const override { return _route; }

    // Per-deck transport. Play pad = play toggle, Alt+Play pad = record toggle. Both arrive from the UI
    // main loop, so opening/closing FatFs in StreamDeck here is main-loop-safe. Play XOR record per deck.
    // Only the Play pad acts (`reverse == false`); the Rev pad is left inert (free for future reverse
    // playback), so the mapping is exactly Play = play, Alt+Play = record.
    bool on_play_pad(DeckRef::Ref d, bool reverse) override { if (!reverse) _toggle(d, /*record=*/false); return false; }
    void on_record_pad(DeckRef::Ref d, bool reverse) override { if (!reverse) _toggle(d, /*record=*/true); }

    void render(DisplayModel& m) override {
        m.clear();
        const uint32_t now = _time ? _time->now_ms() : 0;
        for (DeckRef::Ref dk : { DeckRef::A, DeckRef::B }) {
            const int  i         = (dk == DeckRef::A) ? 0 : 1;
            const bool playing   = _stream && _stream->is_playing(dk);
            const bool recording = _stream && _stream->is_recording(dk);
            const bool err       = _time && now < _err_until[i];   // failed start still flashing
            // Idle off, playing bright green, recording bright red, rejected start amber - an
            // unambiguous off->on per deck. Feedback rides the Play-pad LED (the pad you pressed).
            const uint32_t c = err       ? kErrColor
                             : playing    ? 0x00ff00
                             : recording  ? 0xff0000
                             :              0x000000;
            m.ring[i].set_hex_color(c); m.ring[i].set_segment(0.f, 0.999f); m.ring[i].set_updated();
            m.play[i] = { c, (playing || recording || err) ? 1.f : 0.f };
        }
        // Routing-switch position on the mode L/C/R indicators (clear() already turned the others off).
        if (_route == Route::DoubleMono)       m.mode_left   = { 0xffffff, 0.8f };
        else if (_route == Route::Stereo)      m.mode_center = { 0xffffff, 0.8f };
        else                                   m.mode_right  = { 0xffffff, 0.8f };
    }

private:
    NOCOPY(TapeEngine)

    // Fill `mono` with deck `d`'s output this block: varispeed playback, or the live input on channel
    // `ch` while recording (also pushed to the record ring), or silence. Index `i` is the deck's slot.
    void _render_deck(DeckRef::Ref d, const float* const* in, int ch, float* mono, size_t n) {
        const int i = (d == DeckRef::A) ? 0 : 1;
        if (_stream->is_playing(d)) {
            const Loop     mode = _loop_mode[i];
            // Loop length (source frames) is only needed to shape the faded / Frippertronics modes.
            const uint32_t L    = (mode == Loop::Faded || mode == Loop::Fripp) ? _stream->loop_frames(d) : 0;
            if (!_primed[i]) {
                _cur[i] = _pull(d); _next[i] = _pull(d); _phase[i] = 0.f; _primed[i] = true;
                _src_pos[i] = 2; _loop_gain[i] = 1.f;   // two frames primed into _cur/_next
            }
            for (size_t s = 0; s < n; s++) {
                float v = _cur[i] + (_next[i] - _cur[i]) * _phase[i];
                if (mode == Loop::Faded && L) v *= _fade_env(_src_pos[i], L);  // dip across the seam
                else if (mode == Loop::Fripp) v *= _loop_gain[i];              // per-pass decay
                mono[s] = v;
                _phase[i] += _speed[i];
                while (_phase[i] >= 1.f) {          // advance one (or more) source frames
                    _phase[i] -= 1.f;
                    _cur[i] = _next[i];
                    _next[i] = _pull(d);             // underrun -> 0 (silence) via play_consume
                    if (L && ++_src_pos[i] >= L) {  // crossed a loop boundary
                        _src_pos[i] = 0;
                        if (mode == Loop::Fripp) {
                            _loop_gain[i] *= kFrippDecay;
                            if (_loop_gain[i] < kFrippFloor) { _loop_gain[i] = 0.f; _want_stop[i] = true; }
                        }
                    }
                }
            }
        } else if (_stream->is_recording(d)) {
            _primed[i] = false;                      // re-prime the resampler next time playback starts
            for (size_t s = 0; s < n; s++) mono[s] = in ? in[ch][s] : 0.f;   // monitor this deck's input
            _stream->record_produce(d, reinterpret_cast<const uint8_t*>(mono),
                                    static_cast<uint32_t>(n * sizeof(float)));
        } else {
            _primed[i] = false;
            for (size_t s = 0; s < n; s++) mono[s] = 0.f;
        }
    }

    // Toggle play (record=false) or record (record=true) on deck `d`; play and record are mutually
    // exclusive per deck. A failed start (file missing / SD not mounted / disk full) returns false and
    // arms an amber error flash on that deck's ring so a rejected press is not silent.
    void _toggle(DeckRef::Ref d, bool record) {
        if (!_stream) return;
        const int      i   = (d == DeckRef::A) ? 0 : 1;
        const uint32_t now = _time ? _time->now_ms() : 0;
        // Debounce: the capacitive pads can glitch a single press into a release+touch pair, which would
        // toggle a deck straight back off. Drop a repeat toggle on the same deck within kDebounceMs.
        if (_time && now - _last_trig_ms[i] < kDebounceMs) return;
        _last_trig_ms[i] = now;
        _err_until[i]    = 0;                            // clear any stale flash on a fresh press

        if (record) {
            if (_stream->is_recording(d))      _stream->stop(d);
            else if (!_stream->is_playing(d))  { if (!_stream->start_record(d, _path(d))) _err_until[i] = now + kErrFlashMs; }
        } else {
            if (_stream->is_playing(d))        _stream->stop(d);
            else if (!_stream->is_recording(d)) { if (!_stream->start_play(d, _path(d))) _err_until[i] = now + kErrFlashMs; }
        }
    }

    // Pull one mono source frame from deck `d`'s play ring; zero-filled (silence) on underrun.
    inline float _pull(DeckRef::Ref d) {
        float f = 0.f;
        _stream->play_consume(d, reinterpret_cast<uint8_t*>(&f), sizeof(float));
        return f;
    }

    // Assign each deck a fresh random equal-power pan (the GenerativeStereo / RIGHT routing). Uses a
    // small LCG so it needs no Math.random (which is unavailable here) and stays deterministic.
    void _roll_random_pans() {
        for (int i = 0; i < 2; i++) {
            _rng = _rng * 1664525u + 1013904223u;
            const float p = static_cast<float>(_rng >> 8) * (1.f / 16777216.f);  // [0,1)
            _rndL[i] = std::cos(p * kHalfPi);
            _rndR[i] = std::sin(p * kHalfPi);
        }
    }

    // Faded-loop seam envelope: ramp up over the first `f` frames and down over the last `f` of each
    // loop (f bounded to ~kFadeFrames, and never more than 1/8 of the loop), so the wrap is not a click.
    static float _fade_env(uint32_t pos, uint32_t L) {
        const uint32_t f = (L >= 8u * kFadeFrames) ? kFadeFrames : (L / 8u);
        if (f == 0) return 1.f;
        if (pos < f)     return static_cast<float>(pos) / static_cast<float>(f);
        if (pos > L - f) return static_cast<float>(L - pos) / static_cast<float>(f);
        return 1.f;
    }

    static const char* _path(DeckRef::Ref d) { return d == DeckRef::A ? "/TAPEA.WAV" : "/TAPEB.WAV"; }

    enum class Loop : uint8_t { None, Plain, Faded, Fripp };  // ENV-knob loop modes (CCW -> CW)

    static constexpr size_t   kMaxFrames  = 128;        // platform block is 96
    static constexpr uint32_t kErrColor   = 0xff6000;   // amber: a start_play/record was rejected
    static constexpr uint32_t kErrFlashMs = 1200;       // how long the amber rejection flash lasts
    static constexpr uint32_t kDebounceMs = 300;        // ignore a same-deck retrigger within this
    static constexpr float    kHalfPi     = 1.57079632679f;
    static constexpr float    kCenterGain = 0.70710678f;  // equal-power centre (-3 dB), = cos/sin(pi/4)
    static constexpr uint32_t kFadeFrames = 2400;       // ~50 ms seam fade at 48 kHz (Faded loop)
    static constexpr float    kFrippDecay = 0.6f;       // per-pass gain multiplier (Frippertronics)
    static constexpr float    kFrippFloor = 0.02f;      // below this the loop has faded out -> stop

    IStreamDeck*       _stream = nullptr;
    const ITimeSource* _time   = nullptr;

    Route _route = Route::DoubleMono;  // routing switch position (set each loop via set_config)

    // Per-deck varispeed playback resampler state (index 0 = deck A, 1 = deck B).
    float _speed[2]   = { 1.f, 1.f };    // source frames advanced per output frame
    float _speed_n[2] = { 0.5f, 0.5f };  // PITCH knob value (0.5 = unity) for param() readback
    float _phase[2]   = { 0.f, 0.f };    // fractional position in [0,1) between _cur and _next
    float _cur[2]     = { 0.f, 0.f };
    float _next[2]    = { 0.f, 0.f };
    bool  _primed[2]  = { false, false };

    // Per-deck pan (POS knob, equal-power) and the random-mode pan; mix-fader A/B blend gains.
    float _pan[2]  = { 0.5f, 0.5f };
    float _panL[2] = { kCenterGain, kCenterGain };
    float _panR[2] = { kCenterGain, kCenterGain };
    float _rndL[2] = { kCenterGain, kCenterGain };
    float _rndR[2] = { kCenterGain, kCenterGain };
    float _xfade = 0.5f;                  // mix-fader value (0.5 = both decks full)
    float _gA = 1.f, _gB = 1.f;           // mix-fader blend gains for deck A / deck B
    uint32_t _rng = 0x9e3779b9u;          // LCG state for the random-pan routing

    // Per-deck playback volume (MIX knob) and loop mode (ENV knob) + its shaping state.
    float _gain[2]   = { 1.f, 1.f };      // MIX knob: per-deck playback volume
    float _env_n[2]  = { 0.f, 0.f };      // ENV knob value (readback); selects the loop mode
    Loop  _loop_mode[2] = { Loop::None, Loop::None };
    uint32_t _src_pos[2]   = { 0, 0 };    // source-frame position within the current loop (fade/decay)
    float    _loop_gain[2] = { 1.f, 1.f };// Frippertronics per-pass decay gain
    bool     _want_stop[2] = { false, false }; // ISR -> prepare(): a Fripp loop faded out; stop it

    uint32_t _err_until[2]    = { 0, 0 };  // now_ms() deadline of each ring's error flash (0 = none)
    uint32_t _last_trig_ms[2] = { 0, 0 };  // now_ms() of each deck's last accepted toggle (debounce)
};

} // namespace spotykach
