#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "engine/istreamdeck.h"
#include "nocopy.h"

#include <cstdint>
#include <cmath>

namespace spotykach {

// Streaming tape deck: plays arbitrarily long files from SD and records arbitrarily long takes to SD,
// bypassing the in-SDRAM loop-length cap. The engine itself is thin - in process() (the audio ISR) it
// only moves interleaved float frames between the platform's lock-free streaming rings and the audio
// buffers; the slow FatFs I/O happens in the platform's StreamDeck pump (main loop). Control: the SeqA
// pad toggles playback, SeqB toggles recording, both to a single fixed tape file. (First-cut UX: one
// file, two pads - file selection / per-deck streams / transport come later.)
//
// Format note: streams the build's native WAV body format (float, interleaved L/R) so play/record need
// no sample conversion - the audio path is already float.
class TapeEngine : public IEngine {
public:
    TapeEngine() = default;
    ~TapeEngine() override = default;

    void init(const EngineContext& ctx) override { _stream = ctx.stream; _time = ctx.time; }
    void prepare() override {}

    void process(const float* const* in, float** out, size_t size) override {
        const size_t n = size > kMaxFrames ? kMaxFrames : size;

        if (_stream && _stream->is_playing()) {
            // Varispeed playback: pull source frames from the ring at fractional rate `_speed` and
            // linearly interpolate. Pitch and speed move together (tape-style). _speed == 1 is unity.
            if (!_primed) {                     // prime the 2-frame interpolation window
                _pull(_cur0, _cur1);
                _pull(_next0, _next1);
                _phase = 0.f; _primed = true;
            }
            for (size_t i = 0; i < n; i++) {
                out[0][i] = _cur0 + (_next0 - _cur0) * _phase;
                out[1][i] = _cur1 + (_next1 - _cur1) * _phase;
                _phase += _speed;
                while (_phase >= 1.f) {         // advance one (or more) source frames
                    _phase -= 1.f;
                    _cur0 = _next0; _cur1 = _next1;
                    _pull(_next0, _next1);       // underrun -> 0 (silence) via play_consume
                }
            }
        } else if (_stream && _stream->is_recording()) {
            _primed = false;  // re-prime the resampler next time playback starts
            // Monitor the live input while recording (so you can hear what you're capturing).
            for (size_t i = 0; i < n; i++) { out[0][i] = in ? in[0][i] : 0.f; out[1][i] = in ? in[1][i] : 0.f; }
        } else {
            _primed = false;
            for (size_t i = 0; i < n; i++) { out[0][i] = 0.f; out[1][i] = 0.f; }
        }

        if (_stream && _stream->is_recording()) {
            float il[kMaxFrames * 2];
            for (size_t i = 0; i < n; i++) {
                il[2 * i]     = in ? in[0][i] : 0.f;
                il[2 * i + 1] = in ? in[1][i] : 0.f;
            }
            _stream->record_produce(reinterpret_cast<const uint8_t*>(il),
                                    static_cast<uint32_t>(n * 2 * sizeof(float)));
        }
    }

    Capabilities capabilities() const override { return CapOwnDisplay | CapDualDeck; }

    // PITCH knob -> varispeed playback rate. v in [0,1], centred at 0.5 = unity; +/-1 octave at the ends
    // (0 -> half speed/-12 semitones, 1 -> double speed/+12). Either deck's PITCH drives the one stream.
    void set_param(ParamId id, DeckRef::Ref, float v) override {
        if (id == ParamId::Speed) { _speed_n = v; _speed = std::exp2f((v - 0.5f) * 2.f); }
    }
    float param(ParamId id, DeckRef::Ref) const override {
        return id == ParamId::Speed ? _speed_n : 0.f;
    }

    // SeqA = play toggle, SeqB = record toggle. Called from the UI main loop, so opening/closing FatFs
    // files in StreamDeck here is main-loop-safe. Play and record are mutually exclusive.
    void on_seq_trigger(DeckRef::Ref d) override {
        if (!_stream) return;
        const uint32_t now = _time ? _time->now_ms() : 0;
        const int      i   = (d == DeckRef::A) ? 0 : 1;
        // Debounce: the capacitive Seq pads can glitch a single press into a release+touch pair, which
        // would toggle play/record straight back off (seen as a brief green/red flash). Drop a repeat
        // trigger on the same deck within kDebounceMs; an intentional stop press lands far later.
        if (_time && now - _last_trig_ms[i] < kDebounceMs) return;
        _last_trig_ms[i] = now;

        // A failed start_play/start_record (file missing, SD not mounted, disk full) returns false;
        // arm an amber error flash on that deck's ring so a rejected press is not silent.
        _err_until[i] = 0;                                  // clear any stale flash on a fresh press
        if (d == DeckRef::A) {
            if (_stream->is_playing())          _stream->stop();
            else if (!_stream->is_recording()) { if (!_stream->start_play(kPath))   _err_until[i] = now + kErrFlashMs; }
        } else {
            if (_stream->is_recording())        _stream->stop();
            else if (!_stream->is_playing())   { if (!_stream->start_record(kPath)) _err_until[i] = now + kErrFlashMs; }
        }
    }

    void render(DisplayModel& m) override {
        m.clear();
        const bool playing   = _stream && _stream->is_playing();
        const bool recording = _stream && _stream->is_recording();
        const uint32_t now   = _time ? _time->now_ms() : 0;
        const bool errA = _time && now < _err_until[0];        // failed start_play  still flashing
        const bool errB = _time && now < _err_until[1];        // failed start_record still flashing
        // Idle = fully off, active = full-bright, failed start = amber - so the change reads as an
        // unambiguous off->on. (The previous dim-vs-bright same-hue scheme was unreadable on the
        // hardware LEDs - dim red and bright red looked identical, so you couldn't tell recording
        // had engaged; and a rejected press was completely silent.)
        const uint32_t cA = errA ? kErrColor : (playing   ? 0x00ff00 : 0x000000);  // green = playing
        const uint32_t cB = errB ? kErrColor : (recording ? 0xff0000 : 0x000000);  // red   = recording
        m.ring[0].set_hex_color(cA); m.ring[0].set_segment(0.f, 0.999f); m.ring[0].set_updated();
        m.ring[1].set_hex_color(cB); m.ring[1].set_segment(0.f, 0.999f); m.ring[1].set_updated();
        m.play[0] = { cA, (playing   || errA) ? 1.f : 0.f };
        m.play[1] = { cB, (recording || errB) ? 1.f : 0.f };
    }

private:
    NOCOPY(TapeEngine)

    // Pull one stereo source frame from the play ring; zero-filled (silence) on underrun.
    inline void _pull(float& o0, float& o1) {
        float f[2] = { 0.f, 0.f };
        _stream->play_consume(reinterpret_cast<uint8_t*>(f), 2 * sizeof(float));
        o0 = f[0]; o1 = f[1];
    }

    static constexpr size_t      kMaxFrames  = 128;          // platform block is 96
    static constexpr const char* kPath       = "/TAPE.WAV";  // single fixed tape file (first cut)
    static constexpr uint32_t    kErrColor   = 0xff6000;     // amber: a start_play/record was rejected
    static constexpr uint32_t    kErrFlashMs = 1200;         // how long the amber rejection flash lasts
    static constexpr uint32_t    kDebounceMs = 300;          // ignore a same-deck retrigger within this

    IStreamDeck*       _stream = nullptr;
    const ITimeSource* _time   = nullptr;
    uint32_t _err_until[2]    = { 0, 0 };  // now_ms() deadline of each ring's error flash (0 = no flash)
    uint32_t _last_trig_ms[2] = { 0, 0 };  // now_ms() of each deck's last accepted toggle (debounce)

    // Varispeed playback resampler state.
    float _speed   = 1.f;    // source frames advanced per output frame
    float _speed_n = 0.5f;   // PITCH knob value (0.5 = unity) for param() readback
    float _phase   = 0.f;    // fractional position in [0,1) between _cur and _next
    float _cur0 = 0.f, _cur1 = 0.f, _next0 = 0.f, _next1 = 0.f;
    bool  _primed = false;
};

} // namespace spotykach
