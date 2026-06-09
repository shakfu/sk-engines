#include "engine/tape/tape_engine.h"
#include "engine/arena.h"
#include "engine/tape/faust_kernel_tapefx.h" // the cyfaust-generated kernel: tfx_tapefx::mydsp

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new> // placement new

namespace spotykach {

// Per-deck tape-FX kernel: wow/flutter (modulated fractional delay) -> Jiles-Atherton hysteresis. The
// generated kernel is mono (1 in, 1 out); compute() is allocation-free and in-place safe (Faust reads
// each input sample into a local before writing the output). The ~16 KB delay buffer lives inside the
// object, so the whole struct is placement-new'd into the SDRAM arena. Knobs are written as normalized
// 0..1 values straight into the captured zones (the .dsp does the musical scaling).
struct TapeFx {
    tfx_tapefx::mydsp dsp;
    FAUSTFLOAT* z[4] = { nullptr, nullptr, nullptr, nullptr }; // drive, char, wow, rate

    struct Cap : UI {
        TapeFx* fx;
        void bind(const char* l, FAUSTFLOAT* zp) {
            if      (std::strcmp(l, "drive") == 0) fx->z[0] = zp;
            else if (std::strcmp(l, "char")  == 0) fx->z[1] = zp;
            else if (std::strcmp(l, "wow")   == 0) fx->z[2] = zp;
            else if (std::strcmp(l, "rate")  == 0) fx->z[3] = zp;
        }
        void addHorizontalSlider(const char* l, FAUSTFLOAT* zp, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, zp); }
        void addVerticalSlider  (const char* l, FAUSTFLOAT* zp, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { bind(l, zp); }
    };

    void init(int sr) { dsp.init(sr); Cap ui; ui.fx = this; dsp.buildUserInterface(&ui); }
    void set(int which, float v) { if (z[which]) *z[which] = v; }       // which: 0..3, v in [0,1]
    void process(float* buf, int n) { FAUSTFLOAT* io[1] = { buf }; dsp.compute(n, io, io); } // in-place
};

void TapeEngine::init(const EngineContext& ctx) {
    _stream = ctx.stream;
    _time   = ctx.time;
    // Construct a tape-FX kernel per deck in the SDRAM arena (each ~16 KB of delay-line state).
    Arena ar(ctx.arena);
    const int sr = static_cast<int>(ctx.sample_rate);
    for (int i = 0; i < 2; i++) {
        if (void* m = ar.alloc<uint8_t>(sizeof(TapeFx), alignof(TapeFx))) {
            _fx[i] = new (m) TapeFx();
            _fx[i]->init(sr);
            for (int p = 0; p < 4; p++) _fx[i]->set(p, _fx_n[i][p]); // seed from the cached defaults
        }
    }
}

// Main-loop housekeeping: push each deck's loop-enable to the stream (so it seeks to the top vs stops at
// EOF), and action a Frippertronics fade-out that asked to stop (stop() touches FatFs, so it must run
// here, not in the audio ISR that set the flag).
void TapeEngine::prepare() {
    if (!_stream) return;
    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        const int i = (d == DeckRef::A) ? 0 : 1;
        _stream->set_loop(d, _loop_mode[i] != Loop::None);
        if (_want_stop[i]) { _want_stop[i] = false; _stream->stop(d); }
        if (_rescan[i])    { _rescan[i] = false; _scan_slots(d); }  // refresh recorded/empty slot cache
    }
}

void TapeEngine::process(const float* const* in, float** out, size_t size) {
    const size_t n = size > kMaxFrames ? kMaxFrames : size;
    if (!_stream) { for (int c = 0; c < 2; c++) for (size_t i = 0; i < n; i++) out[c][i] = 0.f; return; }

    // Each deck -> a mono stream (playback / record-monitor / silence). Deck A reads input A, B reads B.
    float monoA[kMaxFrames], monoB[kMaxFrames];
    _render_deck(DeckRef::A, in, 0, monoA, n);
    _render_deck(DeckRef::B, in, 1, monoB, n);

    // Tape FX (wow/flutter + hysteresis) on the played-back signal only - not on the record monitor,
    // whose wow/flutter delay would add monitoring latency.
    if (_fx[0] && _stream->is_playing(DeckRef::A)) _fx[0]->process(monoA, static_cast<int>(n));
    if (_fx[1] && _stream->is_playing(DeckRef::B)) _fx[1]->process(monoB, static_cast<int>(n));

    // Per-block output gains: per-deck pan (selected by the routing switch) scaled by the mix-fader A/B
    // blend. Pan/blend gains are precomputed on knob change, so the ISR loop is just multiplies.
    float pLa, pRa, pLb, pRb;
    switch (_route) {
        case Route::DoubleMono:                          // LEFT: each deck panned by its Alt+POS knob
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

// PITCH -> per-deck varispeed. Alt+POS (`AltPos`) -> per-deck pan. MIX -> per-deck volume. ENV -> loop
// mode (4 quadrants: none / plain / faded / Frippertronics). Alt+PITCH (`Aux`) -> tape-slot select.
// Bare POS (`Pos`) is reserved for a future loop-start control - ignored for now.
void TapeEngine::set_param(ParamId id, DeckRef::Ref d, float v) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Speed) { _speed_n[i] = v; _speed[i] = std::exp2f((v - 0.5f) * 2.f); }
    else if (id == ParamId::AltPos) { _pan[i] = v; _panL[i] = std::cos(v * kHalfPi); _panR[i] = std::sin(v * kHalfPi); }
    else if (id == ParamId::Crossfade) { _xfade = v; _gA = v <= 0.5f ? 1.f : 2.f * (1.f - v);
                                                      _gB = v >= 0.5f ? 1.f : 2.f * v; }
    else if (id == ParamId::Mix) { _gain[i] = v; }
    else if (id == ParamId::Env) { _env_n[i] = v;
        _loop_mode[i] = v < 0.25f ? Loop::None  : v < 0.5f ? Loop::Plain
                      : v < 0.75f ? Loop::Faded : Loop::Fripp; }
    else if (id == ParamId::Aux) { const int s = static_cast<int>(v * kSlots);
                                   _slot[i] = s < 0 ? 0 : (s >= kSlots ? kSlots - 1 : s); }
    // Tape FX: POS=drive, SIZE=character, MOD_AMT=wow/flutter depth (rate is MODFREQ, set_mod_speed).
    else if (id == ParamId::Pos)    { _fx_n[i][0] = v; if (_fx[i]) _fx[i]->set(0, v); }
    else if (id == ParamId::Size)   { _fx_n[i][1] = v; if (_fx[i]) _fx[i]->set(1, v); }
    else if (id == ParamId::ModAmp) { _fx_n[i][2] = v; if (_fx[i]) _fx[i]->set(2, v); }
}

void TapeEngine::set_mod_speed(DeckRef::Ref d, float v, bool /*sync*/) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    _fx_n[i][3] = v; if (_fx[i]) _fx[i]->set(3, v);   // MODFREQ -> wow/flutter rate
}

float TapeEngine::param(ParamId id, DeckRef::Ref d) const {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Speed) return _speed_n[i];
    if (id == ParamId::AltPos) return _pan[i];
    if (id == ParamId::Mix)   return _gain[i];
    if (id == ParamId::Env)   return _env_n[i];
    if (id == ParamId::Aux)   return (static_cast<float>(_slot[i]) + 0.5f) / static_cast<float>(kSlots);
    if (id == ParamId::Pos)     return _fx_n[i][0]; // drive
    if (id == ParamId::Size)    return _fx_n[i][1]; // character
    if (id == ParamId::ModAmp)  return _fx_n[i][2]; // wow/flutter depth
    if (id == ParamId::ModSpeed) return _fx_n[i][3]; // wow/flutter rate
    return 0.f;
}

void TapeEngine::set_aux_active(DeckRef::Ref d, bool held) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (held && !_aux_held[i]) _rescan[i] = true;   // selector just opened -> re-probe slots in prepare()
    _aux_held[i] = held;
}

// Routing switch (mirrors the granular int mapping so the panel L/C/R reads the same):
// 0 = Stereo (centre), 1 = DoubleMono (left), 2 = GenerativeStereo (right).
bool TapeEngine::set_config(ConfigId id, DeckRef::Ref, int value) {
    if (id == ConfigId::Route) {
        _route = (value == 2) ? Route::GenerativeStereo
               : (value == 1) ? Route::DoubleMono
                              : Route::Stereo;
        if (_route == Route::GenerativeStereo) _roll_random_pans();
    }
    return false;
}

// Per-deck transport. Only the Play pad acts (`reverse == false`); the Rev pad is left inert (free for
// future reverse playback), so the mapping is exactly Play = play, Alt+Play = record.
bool TapeEngine::on_play_pad(DeckRef::Ref d, bool reverse)   { if (!reverse) _toggle(d, /*record=*/false); return false; }
void TapeEngine::on_record_pad(DeckRef::Ref d, bool reverse) { if (!reverse) _toggle(d, /*record=*/true); }

void TapeEngine::render(DisplayModel& m) {
    m.clear();
    const uint32_t now = _time ? _time->now_ms() : 0;
    for (DeckRef::Ref dk : { DeckRef::A, DeckRef::B }) {
        const int  i         = (dk == DeckRef::A) ? 0 : 1;
        const bool playing   = _stream && _stream->is_playing(dk);
        const bool recording = _stream && _stream->is_recording(dk);
        const bool err       = _time && now < _err_until[i];   // failed start still flashing
        // Idle off, playing bright green, recording bright red, rejected start amber - an unambiguous
        // off->on per deck. Feedback rides the Play-pad LED (the pad you pressed).
        const uint32_t c = err       ? kErrColor
                         : playing    ? 0x00ff00
                         : recording  ? 0xff0000
                         :              0x000000;
        m.play[i] = { c, (playing || recording || err) ? 1.f : 0.f };
        if (_aux_held[i]) {
            // Alt+PITCH held: show the tape-slot selector - kSlots dots evenly around the ring (selected
            // bright, recorded mid, empty dim) over a faint base. (Play/record stays on the LED.)
            m.ring[i].set_hex_color(0x202020); m.ring[i].set_segment(0.f, 0.999f);
            m.ring[i].set_point_hex_color(0xffffff);
            for (int s = 0; s < kSlots; s++) {
                // selected slot bright, recorded slots mid, empty slots dim
                const float b = (s == _slot[i]) ? 1.f : (_slot_used[i][s] ? 0.45f : 0.12f);
                m.ring[i].set_point(static_cast<uint8_t>(s * (kRingLeds / kSlots)), b);
            }
        } else {
            m.ring[i].set_hex_color(c); m.ring[i].set_segment(0.f, 0.999f);
        }
        m.ring[i].set_updated();
    }
    // Routing-switch position on the mode L/C/R indicators (clear() already turned the others off).
    if (_route == Route::DoubleMono)       m.mode_left   = { 0xffffff, 0.8f };
    else if (_route == Route::Stereo)      m.mode_center = { 0xffffff, 0.8f };
    else                                   m.mode_right  = { 0xffffff, 0.8f };
}

void TapeEngine::_render_deck(DeckRef::Ref d, const float* const* in, int ch, float* mono, size_t n) {
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

// Toggle play (record=false) or record (record=true) on deck `d`; play and record are mutually exclusive
// per deck. A failed start (file missing / SD not mounted / disk full) returns false and arms an amber
// error flash on that deck's ring so a rejected press is not silent.
void TapeEngine::_toggle(DeckRef::Ref d, bool record) {
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
        else if (!_stream->is_playing(d)) {
            if (!_stream->start_record(d, _path(d, _slot[i]))) _err_until[i] = now + kErrFlashMs;
            else _slot_used[i][_slot[i]] = true;   // the slot's file now exists
        }
    } else {
        if (_stream->is_playing(d))        _stream->stop(d);
        else if (!_stream->is_recording(d)) { if (!_stream->start_play(d, _path(d, _slot[i]))) _err_until[i] = now + kErrFlashMs; }
    }
}

float TapeEngine::_pull(DeckRef::Ref d) {
    float f = 0.f;
    _stream->play_consume(d, reinterpret_cast<uint8_t*>(&f), sizeof(float));
    return f;
}

// Assign each deck a fresh random equal-power pan (the GenerativeStereo / RIGHT routing). Uses a small
// LCG so it needs no Math.random (unavailable here) and stays deterministic.
void TapeEngine::_roll_random_pans() {
    for (int i = 0; i < 2; i++) {
        _rng = _rng * 1664525u + 1013904223u;
        const float p = static_cast<float>(_rng >> 8) * (1.f / 16777216.f);  // [0,1)
        _rndL[i] = std::cos(p * kHalfPi);
        _rndR[i] = std::sin(p * kHalfPi);
    }
}

// Faded-loop seam envelope: ramp up over the first `f` frames and down over the last `f` of each loop
// (f bounded to ~kFadeFrames, and never more than 1/8 of the loop), so the wrap is not a click.
float TapeEngine::_fade_env(uint32_t pos, uint32_t L) {
    const uint32_t f = (L >= 8u * kFadeFrames) ? kFadeFrames : (L / 8u);
    if (f == 0) return 1.f;
    if (pos < f)     return static_cast<float>(pos) / static_cast<float>(f);
    if (pos > L - f) return static_cast<float>(L - pos) / static_cast<float>(f);
    return 1.f;
}

// Build the selected slot's path, e.g. "tapes/tape_a_1.wav", by hand (no printf - keeps the printf
// machinery out of the build). Single-digit slot keeps the name 8.3-safe; FatFile creates "tapes/".
const char* TapeEngine::_path(DeckRef::Ref d, int slot) {
    char* p = _pbuf;
    for (const char* s = "tapes/tape_"; *s; ) *p++ = *s++;
    *p++ = (d == DeckRef::A) ? 'a' : 'b';
    *p++ = '_';
    *p++ = static_cast<char>('1' + slot);   // slot 0..kSlots-1 -> '1'..
    for (const char* s = ".wav"; *s; ) *p++ = *s++;
    *p = '\0';
    return _pbuf;
}

// Probe each slot's file (f_stat via the stream) to mark recorded vs empty for the selector. Main-loop
// only (from prepare(), on selector-open) - 8 stats is cheap and rare, and the ring absorbs the latency.
void TapeEngine::_scan_slots(DeckRef::Ref d) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    for (int s = 0; s < kSlots; s++) _slot_used[i][s] = _stream->exists(_path(d, s));
}

} // namespace spotykach
