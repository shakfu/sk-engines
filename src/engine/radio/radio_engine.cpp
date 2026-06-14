#include "engine/radio/radio_engine.h"

#include "daisysp.h"   // daisysp::SoftLimit

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spotykach {

void RadioEngine::init(const EngineContext& ctx) {
    _stream = ctx.stream;
    _time   = ctx.time;
    for (int i = 0; i < 2; i++) { _rescan[i] = true; _open_station[i] = -1; }
}

// Main-loop housekeeping (off the audio path): (re)scan a deck's bank directory, then settle the
// desired station and (re)open it at its free-running position. All FatFs work lives here.
void RadioEngine::prepare() {
    if (!_stream) return;
    const uint32_t now = _time ? _time->now_ms() : 0;

    // Read the optional on-card rate.txt once, during the boot window (the SD mounts ~1 s in). Default
    // stays 48 kHz if the file is absent; a present file rebases the resampler for that card's rate.
    if (!_rate_loaded && (_try_load_rate() || now >= kBootScanMs)) _rate_loaded = true;

    for (DeckRef::Ref d : { DeckRef::A, DeckRef::B }) {
        const int i = (d == DeckRef::A) ? 0 : 1;

        // Boot retry: the SD card mounts ~1 s after power-up, so keep rescanning an empty bank for a
        // few seconds rather than showing dead air until the user touches the BANK knob.
        if (_nst[i] == 0 && now < kBootScanMs) _rescan[i] = true;

        if (_rescan[i]) {
            _rescan[i] = false;
            _nst[i] = _stream->scan_bank(_bank_dir(i), _stations[i], kMaxStations);
            _open_station[i] = -1;          // force the station to (re)open in the new/rescanned bank
            _pending[i] = -1;
        }

        const int desired = _quant_station(i);
        if (desired != _open_station[i]) {
            // High static = live dial tuning (switch on every crossed station); low static settles so a
            // clean sweep only opens the station you land on (no zipper of half-second file opens).
            if (_env_n[i] >= kStaticThresh || _settle_ready(i, desired, now)) _do_open(d, desired, now);
        } else if (_retune[i]) {
            _retune[i] = false;
            _do_open(d, desired, now);       // RESET / gate: re-seek the current station to live position
        }
    }
}

void RadioEngine::process(const float* const* /*in*/, float** out, size_t size) {
    const size_t n = size > kMaxFrames ? kMaxFrames : size;
    if (!_stream) { for (int c = 0; c < 2; c++) for (size_t i = 0; i < n; i++) out[c][i] = 0.f; return; }

    float monoA[kMaxFrames], monoB[kMaxFrames];
    _render_deck(DeckRef::A, monoA, n);
    _render_deck(DeckRef::B, monoB, n);

    // Per-deck stereo placement from the routing switch, scaled by the mix-fader A/B blend and volume.
    float pLa, pRa, pLb, pRb;
    switch (_route) {
        case Route::DoubleMono:                       // LEFT: radio A hard-left, radio B hard-right
            pLa = 1.f; pRa = 0.f; pLb = 0.f; pRb = 1.f; break;
        case Route::GenerativeStereo:                 // RIGHT: random pan per deck
            pLa = _rndL[0]; pRa = _rndR[0]; pLb = _rndL[1]; pRb = _rndR[1]; break;
        case Route::Stereo: default:                  // CENTRE: both centred
            pLa = pRa = pLb = pRb = kCenterGain; break;
    }
    const float La = _gain[0] * _gA * pLa, Ra = _gain[0] * _gA * pRa,
                Lb = _gain[1] * _gB * pLb, Rb = _gain[1] * _gB * pRb;
    for (size_t i = 0; i < n; i++) {
        out[0][i] = daisysp::SoftLimit(monoA[i] * La + monoB[i] * Lb);
        out[1][i] = daisysp::SoftLimit(monoA[i] * Ra + monoB[i] * Rb);
    }
    _clock += n;   // the radio keeps running whether or not you are tuned in
}

// PITCH -> STATION select. POS -> START offset. SIZE -> varispeed. ENV -> static amount. MIX -> volume.
// Crossfade -> A/B blend. Aux (Alt+PITCH) -> BANK select.
void RadioEngine::set_param(ParamId id, DeckRef::Ref d, float v) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Speed)          { _station_n[i] = v; }
    else if (id == ParamId::Pos)       { _start_n[i] = v; }
    else if (id == ParamId::Size)      { _size_n[i] = v; _speed[i] = std::exp2f((v - 0.5f) * 2.f); }
    else if (id == ParamId::Env)       { _env_n[i] = v; }
    else if (id == ParamId::Mix)       { _gain[i] = v; }
    else if (id == ParamId::Crossfade) { _xfade = v; _gA = v <= 0.5f ? 1.f : 2.f * (1.f - v);
                                                     _gB = v >= 0.5f ? 1.f : 2.f * v; }
    else if (id == ParamId::Aux) {
        int b = static_cast<int>(v * kMaxBanks);
        b = b < 0 ? 0 : (b >= kMaxBanks ? kMaxBanks - 1 : b);
        _bank_n[i] = v;
        if (b != _bank[i]) { _bank[i] = b; _rescan[i] = true; }   // new bank -> rescan in prepare()
    }
}

float RadioEngine::param(ParamId id, DeckRef::Ref d) const {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (id == ParamId::Speed) return _station_n[i];
    if (id == ParamId::Pos)   return _start_n[i];
    if (id == ParamId::Size)  return _size_n[i];
    if (id == ParamId::Env)   return _env_n[i];
    if (id == ParamId::Mix)   return _gain[i];
    if (id == ParamId::Aux)   return (static_cast<float>(_bank[i]) + 0.5f) / static_cast<float>(kMaxBanks);
    return 0.f;
}

void RadioEngine::set_aux_active(DeckRef::Ref d, bool held) {
    _aux_held[(d == DeckRef::A) ? 0 : 1] = held;
}

// STATION / START CV jacks: cached as offsets summed with the knob in prepare() (an unpatched jack reads
// ~0 after calibration, so the knob dominates). cv_voct is RadioMusic's STATION CV; cv_size_pos its START CV.
void RadioEngine::cv_voct(DeckRef::Ref d, float value)     { _station_cv[(d == DeckRef::A) ? 0 : 1] = value; }
void RadioEngine::cv_size_pos(DeckRef::Ref d, float value) { _start_cv[(d == DeckRef::A) ? 0 : 1] = value; }

// Routing switch (mirrors the granular int mapping): 0=Stereo(centre), 1=DoubleMono(left), 2=Generative(right).
bool RadioEngine::set_config(ConfigId id, DeckRef::Ref, int value) {
    if (id == ConfigId::Route) {
        const Route r = (value == 2) ? Route::GenerativeStereo
                      : (value == 1) ? Route::DoubleMono
                                     : Route::Stereo;
        if (r != _route) { _route = r; if (_route == Route::GenerativeStereo) _roll_random_pans(); }
    }
    return false;
}

// RESET: re-tune the current station to its live position. Play pad is debounced (capacitive glitch
// guard); the gate input is already debounced by the platform. Only the Play pad acts (Rev pad inert).
bool RadioEngine::on_play_pad(DeckRef::Ref d, bool reverse) {
    if (reverse) return false;
    const int i = (d == DeckRef::A) ? 0 : 1;
    const uint32_t now = _time ? _time->now_ms() : 0;
    if (_time && now - _last_reset_ms[i] < kDebounceMs) return false;
    _last_reset_ms[i] = now;
    _retune[i] = true;
    return false;
}

void RadioEngine::on_gate_trigger(DeckRef::Ref d) { _retune[(d == DeckRef::A) ? 0 : 1] = true; }

void RadioEngine::render(DisplayModel& m) {
    m.clear();
    const uint32_t now = _time ? _time->now_ms() : 0;
    for (DeckRef::Ref dk : { DeckRef::A, DeckRef::B }) {
        const int  i       = (dk == DeckRef::A) ? 0 : 1;
        const bool playing = _stream && _stream->is_playing(dk);
        const bool err     = _time && now < _err_until[i];
        const uint32_t c   = err ? kErrColor : playing ? 0x00ff00 : 0x000000;
        m.play[i] = { c, (playing || err) ? 1.f : 0.f };

        if (_aux_held[i]) {
            // BANK selector: kMaxBanks dots around the ring, the current bank bright.
            m.ring[i].set_hex_color(0x202020); m.ring[i].set_segment(0.f, 0.999f);
            m.ring[i].set_point_hex_color(0xffffff);
            for (int b = 0; b < kMaxBanks; b++) {
                const float pos = static_cast<float>(b) / static_cast<float>(kMaxBanks);
                m.ring[i].add_point(pos, (b == _bank[i]) ? 1.f : 0.15f);
            }
        } else {
            // STATION position: a faint base ring + a bright marker at station/N (amber if the bank is empty).
            const uint32_t base = playing ? 0x003000 : 0x101010;
            m.ring[i].set_hex_color(base); m.ring[i].set_segment(0.f, 0.999f);
            if (_nst[i] > 0 && _open_station[i] >= 0) {
                m.ring[i].set_point_hex_color(err ? kErrColor : 0x00ff00);
                m.ring[i].add_point(static_cast<float>(_open_station[i]) / static_cast<float>(_nst[i]), 1.f);
            } else if (err) {
                m.ring[i].set_point_hex_color(kErrColor); m.ring[i].add_point(0.f, 1.f);
            }
        }
        m.ring[i].set_updated();
    }
    if (_route == Route::DoubleMono)  m.mode_left   = { 0xffffff, 0.8f };
    else if (_route == Route::Stereo) m.mode_center = { 0xffffff, 0.8f };
    else                              m.mode_right  = { 0xffffff, 0.8f };
}

// --- private ------------------------------------------------------------------------------------------

void RadioEngine::_render_deck(DeckRef::Ref d, float* mono, size_t n) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    if (_stream->is_playing(d)) {
        if (!_primed[i]) { _cur[i] = _pull(d); _next[i] = _pull(d); _phase[i] = 0.f; _primed[i] = true; }
        // Source-frames advanced per output frame = the SIZE varispeed x the source-rate rebase (so a
        // 44.1k card with rate.txt plays at correct pitch, and SIZE varies around that).
        const float step = _speed[i] * _src_rate_ratio;
        for (size_t s = 0; s < n; s++) {
            const float station = _cur[i] + (_next[i] - _cur[i]) * _phase[i];   // varispeed interp
            const float st      = _env_n[i] * _static_env[i];                   // static crossfade amount
            mono[s] = station * (1.f - st) + _static_sample(i) * st;
            if (_static_env[i] > 0.f) { _static_env[i] -= kStaticDec; if (_static_env[i] < 0.f) _static_env[i] = 0.f; }
            _phase[i] += step;
            while (_phase[i] >= 1.f) { _phase[i] -= 1.f; _cur[i] = _next[i]; _next[i] = _pull(d); }
        }
    } else {
        _primed[i] = false;                                  // dead air: static hiss at the ENV level
        for (size_t s = 0; s < n; s++) mono[s] = _static_sample(i) * _env_n[i];
    }
}

// Read /radio/rate.txt (a single integer, e.g. "44100") and rebase the resampler to that source rate.
// Returns true once a file was found (so prepare() stops retrying), false while it is still absent.
// Default 48 kHz (ratio 1.0) is kept if no file appears during the boot window.
bool RadioEngine::_try_load_rate() {
    char buf[16];
    const int n = _stream->read_text("radio/rate.txt", buf, sizeof(buf));
    if (n <= 0) return false;                  // absent/empty -> keep retrying within the boot window
    int rate = 0;
    for (int i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) rate = rate * 10 + (buf[i] - '0');
    if (rate >= 8000 && rate <= 192000) _src_rate_ratio = static_cast<float>(rate) / 48000.f;
    return true;                               // a file was present -> stop retrying (even if value odd)
}

float RadioEngine::_pull(DeckRef::Ref d) {
    int16_t s = 0;
    _stream->play_consume(d, reinterpret_cast<uint8_t*>(&s), sizeof(int16_t));  // underrun -> 0 (silence)
    return static_cast<float>(s) * (1.f / 32768.f);
}

// One filtered-noise sample (LCG white -> one-pole low-pass for a "tuned static" colour). No table, so no
// large static sine to overflow SRAM_EXEC.
float RadioEngine::_static_sample(int i) {
    _rng = _rng * 1664525u + 1013904223u;
    const float w = static_cast<float>(static_cast<int32_t>(_rng)) * (1.f / 2147483648.f);  // [-1,1)
    _noise_lp[i] += 0.2f * (w - _noise_lp[i]);
    return _noise_lp[i] * kNoiseLevel;
}

int RadioEngine::_quant_station(int i) const {
    const int n = _nst[i];
    if (n <= 0) return -1;
    float x = _station_n[i] + _station_cv[i];
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    int s = static_cast<int>(std::lround(x * static_cast<float>(n - 1)));
    return s < 0 ? 0 : (s >= n ? n - 1 : s);
}

void RadioEngine::_do_open(DeckRef::Ref d, int station, uint32_t now) {
    const int i = (d == DeckRef::A) ? 0 : 1;
    _stream->stop(d);                                        // close the current station (cheap for a player)
    if (station < 0 || station >= _nst[i]) { _open_station[i] = -1; return; }
    const uint32_t L = _stations[i][station].frames;
    if (L == 0) { _open_station[i] = -1; return; }
    uint32_t start = static_cast<uint32_t>((_start_n[i] + _start_cv[i]) * static_cast<float>(L));
    if (start >= L) start = L - 1;
    const uint32_t offset = static_cast<uint32_t>((_clock + start) % L);  // the free-running live position
    if (_stream->start_play_raw(d, _path(i, station), offset, /*loop=*/true)) {
        _open_station[i] = station;
        _primed[i]       = false;
        _static_env[i]   = 1.f;                              // tuning burst (level gated by ENV)
    } else {
        _open_station[i] = -1;
        _err_until[i]    = now + kErrFlashMs;
    }
}

bool RadioEngine::_settle_ready(int i, int desired, uint32_t now) {
    if (desired != _pending[i]) { _pending[i] = desired; _pending_ms[i] = now; return false; }
    return !_time || (now - _pending_ms[i]) >= kSettleMs;
}

void RadioEngine::_roll_random_pans() {
    for (int i = 0; i < 2; i++) {
        _rng = _rng * 1664525u + 1013904223u;
        const float p = static_cast<float>(_rng >> 8) * (1.f / 16777216.f);  // [0,1)
        _rndL[i] = std::cos(p * kHalfPi);
        _rndR[i] = std::sin(p * kHalfPi);
    }
}

// Build "radio/<bank>" (no leading slash, matching the tape engine's relative paths). FatFs reads it
// directly; the user creates the /radio tree on the card.
const char* RadioEngine::_bank_dir(int i) {
    char* p = _dbuf;
    for (const char* s = "radio/"; *s; ) *p++ = *s++;
    const int b = _bank[i];
    if (b >= 10) { *p++ = '1'; *p++ = static_cast<char>('0' + (b - 10)); }
    else         { *p++ = static_cast<char>('0' + b); }
    *p = '\0';
    return _dbuf;
}

// Build "radio/<bank>/<name>" for the selected station.
const char* RadioEngine::_path(int i, int station) {
    char* p = _pbuf;
    for (const char* s = "radio/"; *s; ) *p++ = *s++;
    const int b = _bank[i];
    if (b >= 10) { *p++ = '1'; *p++ = static_cast<char>('0' + (b - 10)); }
    else         { *p++ = static_cast<char>('0' + b); }
    *p++ = '/';
    for (const char* s = _stations[i][station].name; *s; ) *p++ = *s++;
    *p = '\0';
    return _pbuf;
}

} // namespace spotykach
