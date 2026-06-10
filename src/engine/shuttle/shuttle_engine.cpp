#include "engine/shuttle/shuttle_engine.h"
#include "engine/arena.h"

#include <cmath>
#include <cstring>

namespace spotykach {

// Bipolar capstan-speed map. The knob splits at noon (v=0.5): a small deadzone about centre maps to an
// exact 0 (a guaranteed full stop -> silence), and outside it the speed ramps LINEARLY from 0 at the
// deadzone edge to +/-kMaxSpeed at the extremes. Forward (CW, v>0.5) positive, reverse (CCW) negative.
float ShuttleEngine::speed_from_knob(float v) {
    const float c = v - 0.5f;
    const float a = std::fabs(c);
    if (a <= kDead) return 0.f;
    const float frac = (a - kDead) / (0.5f - kDead);     // 0 at the deadzone edge -> 1 at full throw
    const float s = frac * kMaxSpeed;
    return c < 0.f ? -s : s;
}

// Inverse for a positive (forward) speed - the knob value the Play pad snaps to, so param(Speed)
// reports a position consistent with the map and the pot pickup reseeds to it.
float ShuttleEngine::knob_for_speed(float s) {
    const float frac = s / kMaxSpeed;
    return 0.5f + (kDead + frac * (0.5f - kDead));
}

void ShuttleEngine::init(const EngineContext& ctx) {
    _stream = ctx.stream;
    _time   = ctx.time;
    _cap    = static_cast<uint32_t>(kBufSeconds * ctx.sample_rate);
    Arena ar(ctx.arena);
    for (int d = 0; d < 2; d++)
        for (int s = 0; s < kTracks; s++) {
            _buf[d][s] = ar.alloc<float>(_cap);
            if (!_buf[d][s]) _cap = 0;                // arena exhausted -> no tape (defensive)
        }
    _recompute_blend();
}

void ShuttleEngine::_request_load(DeckRef::Ref d, int s) {
    if (!_stream || !_cap) return;
    const int i = idx(d);
    const uint32_t now = _time ? _time->now_ms() : 0;
    if (_stream->start_play(d, _path(d, _tape_slot[i][s]))) {
        _load[i][s] = Load::Priming; _wpos[i][s] = 0; _len[i][s] = 0;
        _recording[i][s] = false; _rolling[i][s] = false; _read[i][s] = 0.0;
    } else {
        _err_until[i] = now + kErrFlashMs;
    }
}

// Main-loop housekeeping: refresh the slot cache and advance each track's SD->RAM load (the FatFs-
// touching stream calls must run here, not in the audio ISR). Only one stream exists, so loads are
// effectively serialized; a track whose load has not started yet simply waits a pass.
void ShuttleEngine::prepare() {
    if (!_stream) return;
    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        const int i = idx(d);
        if (_rescan[i]) { _rescan[i] = false; for (int s = 0; s < kTapeSlots; s++) _slot_used[i][s] = _stream->exists(_path(d, s)); }

        for (int s = 0; s < kTracks; s++) {
            if (_load[i][s] == Load::Priming) {
                const uint32_t target = _stream->loop_frames(d);
                if (target > 0)                   _load[i][s] = Load::Draining;
                else if (!_stream->is_playing(d)) _load[i][s] = Load::Idle;   // empty/failed file
            }
            if (_load[i][s] == Load::Draining) {
                const uint32_t target   = _stream->loop_frames(d);
                const uint32_t cap_left = (_cap > _wpos[i][s]) ? _cap - _wpos[i][s] : 0;
                uint32_t want = target - _wpos[i][s];
                if (want > kLoadChunk) want = kLoadChunk;
                if (want > cap_left)   want = cap_left;
                // The ring (~1 MB) stays ahead of an 8 K-frame pull, so play_consume serves real audio
                // (not zero-fill); we pull exactly `target` frames total and never read past EOF.
                const uint32_t got = _stream->play_consume(
                    d, reinterpret_cast<uint8_t*>(_buf[i][s] + _wpos[i][s]), want * sizeof(float)) / sizeof(float);
                _wpos[i][s] += got;
                if (_wpos[i][s] >= target || _wpos[i][s] >= _cap) {
                    _len[i][s] = _wpos[i][s]; _read[i][s] = 0.0; _stream->stop(d); _load[i][s] = Load::Idle;
                }
            }
        }
    }
}

void ShuttleEngine::process(const float* const* in, float** out, size_t size) {
    const size_t n = size > kMaxFrames ? kMaxFrames : size;
    float a0[kMaxFrames], a1[kMaxFrames], b0[kMaxFrames], b1[kMaxFrames];
    _render_track(DeckRef::A, 0, in, 0, a0, n);
    _render_track(DeckRef::A, 1, in, 0, a1, n);
    _render_track(DeckRef::B, 0, in, 1, b0, n);
    _render_track(DeckRef::B, 1, in, 1, b1, n);

    // Deck mono = its two tracks summed at their per-track volume; then deck pan x A/B blend to stereo.
    const float gA0 = _gain[0][0], gA1 = _gain[0][1], gB0 = _gain[1][0], gB1 = _gain[1][1];
    const float La = _gA * _panL[0], Ra = _gA * _panR[0];
    const float Lb = _gB * _panL[1], Rb = _gB * _panR[1];
    for (size_t i = 0; i < n; i++) {
        const float mA = a0[i] * gA0 + a1[i] * gA1;
        const float mB = b0[i] * gB0 + b1[i] * gB1;
        out[0][i] = mA * La + mB * Lb;
        out[1][i] = mA * Ra + mB * Rb;
    }
}

// One track -> a mono block: record-monitor (and capture to its buffer), bidirectional varispeed
// playback, or silence. Reverse and freeze fall out of the SIGNED _speed with no special-casing.
// POS/SIZE -> the track's loop window in frames. SIZE sets the length (clamped to a minimum and to the
// recording); POS slides the start across the part of the buffer the window does not cover, so the
// window always stays inside [0, len). At SIZE=full, the window is the whole buffer and POS is inert.
void ShuttleEngine::_window(int i, int s, uint32_t& start, uint32_t& len) const {
    const uint32_t L = _len[i][s];
    if (L == 0) { start = 0; len = 0; return; }
    uint32_t ll = static_cast<uint32_t>(_size_n[i][s] * static_cast<float>(L));
    if (ll < kMinLoopFrames) ll = kMinLoopFrames;
    if (ll > L) ll = L;
    start = static_cast<uint32_t>(_pos_n[i][s] * static_cast<float>(L - ll));
    len   = ll;
}

void ShuttleEngine::_render_track(DeckRef::Ref d, int s, const float* const* in, int ch, float* mono, size_t n) {
    const int i = idx(d);
    uint32_t S, Lw; _window(i, s, S, Lw);                     // this track's loop window (frames)
    // Seq-pad re-align. A track that is audibly moving dips through a short declick ramp (fade out ->
    // jump to 0 -> fade in) so the pointer discontinuity makes no click; a silent / stopped track has
    // nothing to mask, so it just snaps. _declick counts 2*kDeclickRamp..0 across blocks; the jump is at
    // the gain minimum (kDeclickRamp).
    if (_realign[i][s]) {
        _realign[i][s] = false;
        const bool audible = _rolling[i][s] && Lw > 0 && _speed[i][s] != 0.f && _buf[i][s];
        if (audible) _declick[i][s] = 2 * kDeclickRamp;
        else { _read[i][s] = static_cast<double>(S); _declick[i][s] = 0; }   // snap to the window start
    }
    if (_recording[i][s]) {
        _declick[i][s] = 0;
        for (size_t k = 0; k < n; k++) {
            const float x = in ? in[ch][k] : 0.f;
            mono[k] = x;                                          // monitor the input being recorded
            if (_wpos[i][s] < _cap) { _buf[i][s][_wpos[i][s]++] = x; _len[i][s] = _wpos[i][s]; }
        }
        return;
    }
    if (_rolling[i][s] && Lw > 0 && _speed[i][s] != 0.f && _buf[i][s]) {
        const double   Sd  = static_cast<double>(S);
        const double   Ld  = static_cast<double>(Lw);
        const double   End = Sd + Ld;
        const uint32_t E   = S + Lw;                              // one past the window's last frame
        const float    sp  = _speed[i][s];
        double p = _read[i][s];
        { double rel = p - Sd; rel -= std::floor(rel / Ld) * Ld; p = Sd + rel; }  // bring p into the window
        for (size_t k = 0; k < n; k++) {
            float g = 1.f;
            if (_declick[i][s] > 0) {                             // realign declick ramp in progress
                const int dc = _declick[i][s];
                if (dc == kDeclickRamp) p = Sd;                   // jump to the window start at the gain min
                g = (dc > kDeclickRamp) ? (dc - kDeclickRamp) * (1.f / kDeclickRamp)   // fade out 1 -> 0
                                        : (kDeclickRamp - dc) * (1.f / kDeclickRamp);  // fade in  0 -> 1
                _declick[i][s] = dc - 1;
            }
            uint32_t i0 = static_cast<uint32_t>(p);
            if (i0 >= E) i0 = E - 1;                              // guard the fp upper edge
            uint32_t i1 = i0 + 1; if (i1 >= E) i1 = S;            // wrap to the window start for interp
            const float fr = static_cast<float>(p - static_cast<double>(i0));
            mono[k] = (_buf[i][s][i0] + (_buf[i][s][i1] - _buf[i][s][i0]) * fr) * g;
            p += sp;
            while (p >= End) p -= Ld;                             // wrap forward within the window (loop)
            while (p < Sd)   p += Ld;                             // wrap reverse within the window (loop)
        }
        _read[i][s] = p;
        return;
    }
    // stopped / empty -> silence. Finish an interrupted declick instantly (output is silent anyway), so
    // the realign target (the window start) is still reached and no stale fade lingers.
    if (_declick[i][s] > 0) { _read[i][s] = static_cast<double>(S); _declick[i][s] = 0; }
    for (size_t k = 0; k < n; k++) mono[k] = 0.f;
}

void ShuttleEngine::set_param(ParamId id, DeckRef::Ref d, float v) {
    const int i = idx(d);
    const int s = _active[i];                                     // knobs address the FOCUSED track
    if (id == ParamId::Speed) { _speed_n[i][s] = v; _speed[i][s] = speed_from_knob(v); }
    else if (id == ParamId::Pos)  { _pos_n[i][s]  = v; }   // loop window start  (computed per block)
    else if (id == ParamId::Size) { _size_n[i][s] = v; }   // loop window length (computed per block)
    else if (id == ParamId::Mix) { _gain[i][s] = v; }
    else if (id == ParamId::AltPos) { _pan[i] = v; _panL[i] = std::cos(v * kHalfPi); _panR[i] = std::sin(v * kHalfPi); }  // per-deck
    else if (id == ParamId::Crossfade) { _xfade = v; _recompute_blend(); }
    else if (id == ParamId::Aux) {
        const int sl = static_cast<int>(v * kTapeSlots);
        const int ns = sl < 0 ? 0 : (sl >= kTapeSlots ? kTapeSlots - 1 : sl);
        if (ns != _tape_slot[i][s]) { _tape_slot[i][s] = ns; _request_load(d, s); }   // pick a slot -> load it
    }
}

float ShuttleEngine::param(ParamId id, DeckRef::Ref d) const {
    const int i = idx(d);
    const int s = _active[i];
    if (id == ParamId::Speed)  return _speed_n[i][s];
    if (id == ParamId::Pos)    return _pos_n[i][s];
    if (id == ParamId::Size)   return _size_n[i][s];
    if (id == ParamId::Mix)    return _gain[i][s];
    if (id == ParamId::AltPos) return _pan[i];
    if (id == ParamId::Aux)    return (static_cast<float>(_tape_slot[i][s]) + 0.5f) / static_cast<float>(kTapeSlots);
    return 0.f;
}

void ShuttleEngine::set_aux_active(DeckRef::Ref d, bool held) {
    const int i = idx(d);
    if (held && !_aux_held[i]) _rescan[i] = true;
    _aux_held[i] = held;
}

bool ShuttleEngine::take_param_reseed(DeckRef::Ref d) {
    const int i = idx(d);
    const bool f = _want_reseed[i];
    _want_reseed[i] = false;
    return f;
}

// Rev pad (reverse=true): swap which of the deck's two tracks is focused, and ask the platform to
// reseed the focused track's knobs (Speed/Mix/Aux) so the pots catch it without a jump. Both tracks
// keep playing - the swap only moves the edit focus. Plain Play (reverse=false): toggle the focused
// track's rolling and SNAP it to unity on engage (with the same pickup reseed).
bool ShuttleEngine::on_play_pad(DeckRef::Ref d, bool reverse) {
    const int i = idx(d);
    const uint32_t now = _time ? _time->now_ms() : 0;
    if (_time && now - _last_trig_ms[i] < kDebounceMs) return false;
    _last_trig_ms[i] = now;

    if (reverse) {
        _active[i] ^= 1;
        _want_reseed[i] = true;
        _swap_show[i] = 60;                                       // briefly flash which track is focused
        return false;                                            // not "empty" - suppress the empty flash
    }

    const int s = _active[i];
    _err_until[i] = 0;
    if (_recording[i][s]) return _len[i][s] == 0;                // ignore Play while recording
    if (!_rolling[i][s]) {
        if (_len[i][s] == 0) { _err_until[i] = now + kErrFlashMs; return true; }
        _rolling[i][s] = true;
        _speed_n[i][s] = knob_for_speed(1.f); _speed[i][s] = 1.f; _want_reseed[i] = true;  // snap to unity
    } else {
        _rolling[i][s] = false;
    }
    return _len[i][s] == 0;
}

// Alt+Play: toggle recording on the focused track (play XOR record for that track). Start overwrites
// the buffer from frame 0; stop finalizes the recorded length. The deck's OTHER track is unaffected.
void ShuttleEngine::on_record_pad(DeckRef::Ref d, bool reverse) {
    if (reverse) return;
    const int i = idx(d);
    const int s = _active[i];
    const uint32_t now = _time ? _time->now_ms() : 0;
    if (_time && now - _last_trig_ms[i] < kDebounceMs) return;
    _last_trig_ms[i] = now;
    if (_recording[i][s]) { _recording[i][s] = false; _len[i][s] = _wpos[i][s]; }
    else { _recording[i][s] = true; _rolling[i][s] = false; _wpos[i][s] = 0; _len[i][s] = 0; _read[i][s] = 0.0; }
}

// Seq pad: re-align ALL four tracks to their loop start in one atomic gesture, so free-running tracks
// that have drifted apart snap back to a common downbeat. Deck-agnostic (either deck's Seq pad does the
// same global align). The actual pointer zero is deferred to the ISR (a flag per track), not written
// here, because _read is a double the audio ISR also updates - the flag avoids a torn cross-thread
// write, and all four flags being set before the next block makes the snap simultaneous.
void ShuttleEngine::on_seq_trigger(DeckRef::Ref /*d*/) {
    for (int d = 0; d < 2; d++)
        for (int s = 0; s < kTracks; s++)
            _realign[d][s] = true;
}

void ShuttleEngine::_recompute_blend() {
    _gA = _xfade <= 0.5f ? 1.f : 2.f * (1.f - _xfade);
    _gB = _xfade >= 0.5f ? 1.f : 2.f * _xfade;
}

void ShuttleEngine::render(DisplayModel& m) {
    m.clear();
    const uint32_t now = _time ? _time->now_ms() : 0;
    for (DeckRef::Ref dk : { DeckRef::A, DeckRef::B }) {
        const int  i   = idx(dk);
        const int  s   = _active[i];
        const bool err = _time && now < _err_until[i];
        // Direction-coded transport of the focused track: recording red, forward green, reverse cyan,
        // engaged-but-frozen dim white, idle off, rejected action amber.
        uint32_t c; float b;
        if      (err)               { c = kErrColor; b = 1.f; }
        else if (_recording[i][s])  { c = 0xff0000;  b = 1.f; }
        else if (_rolling[i][s] && _speed[i][s] > 0.f) { c = 0x00ff00; b = 1.f; }
        else if (_rolling[i][s] && _speed[i][s] < 0.f) { c = 0x00a0ff; b = 1.f; }
        else if (_rolling[i][s])    { c = 0x404040;  b = 0.5f; }
        else                        { c = 0x000000;  b = 0.f; }
        m.play[i] = { c, b };

        if (_aux_held[i]) {
            // Alt+PITCH held: tape-slot selector for the focused track (selected bright, recorded mid,
            // empty dim).
            m.ring[i].set_hex_color(0x202020); m.ring[i].set_segment(0.f, 0.999f);
            m.ring[i].set_point_hex_color(0xffffff);
            for (int t = 0; t < kTapeSlots; t++) {
                const float pb = (t == _tape_slot[i][s]) ? 1.f : (_slot_used[i][t] ? 0.45f : 0.12f);
                m.ring[i].set_point(static_cast<uint8_t>(t * (kRingLeds / kTapeSlots)), pb);
            }
        } else if (_swap_show[i] > 0) {
            // Just after a Rev swap: flash the focused-track number (1 or 2 dots) for a moment.
            _swap_show[i]--;
            m.ring[i].set_hex_color(c); m.ring[i].set_segment(0.f, 0.999f);
            m.ring[i].set_point_hex_color(0xffffff);
            for (int t = 0; t <= s; t++) m.ring[i].set_point(static_cast<uint8_t>(t * 4), 1.f);
        } else {
            m.ring[i].set_hex_color(c); m.ring[i].set_segment(0.f, 0.999f);
            if (_len[i][s] > 0) {                                // moving read-position dot
                m.ring[i].set_point_hex_color(0xffffff);
                m.ring[i].add_point(static_cast<float>(_read[i][s] / static_cast<double>(_len[i][s])), 1.f);
            }
        }
        m.ring[i].set_updated();
    }
}

const char* ShuttleEngine::_path(DeckRef::Ref d, int slot) {
    char* p = _pbuf;
    for (const char* s = "tapes/tape_"; *s; ) *p++ = *s++;
    *p++ = (d == DeckRef::A) ? 'a' : 'b';
    *p++ = '_';
    *p++ = static_cast<char>('1' + slot);
    for (const char* s = ".wav"; *s; ) *p++ = *s++;
    *p = '\0';
    return _pbuf;
}

int      ShuttleEngine::active_track(DeckRef::Ref d) const { return _active[idx(d)]; }
uint32_t ShuttleEngine::buffer_frames(DeckRef::Ref d, int t) const { return _len[idx(d)][t]; }
uint32_t ShuttleEngine::loop_start(DeckRef::Ref d, int t) const { uint32_t s, l; _window(idx(d), t, s, l); return s; }
uint32_t ShuttleEngine::loop_len(DeckRef::Ref d, int t)   const { uint32_t s, l; _window(idx(d), t, s, l); return l; }
float    ShuttleEngine::read_position(DeckRef::Ref d, int t) const { return static_cast<float>(_read[idx(d)][t]); }
float    ShuttleEngine::track_speed(DeckRef::Ref d, int t) const { return _speed[idx(d)][t]; }
bool     ShuttleEngine::is_rolling(DeckRef::Ref d, int t) const { return _rolling[idx(d)][t]; }
bool     ShuttleEngine::is_recording(DeckRef::Ref d, int t) const { return _recording[idx(d)][t]; }

} // namespace spotykach
