// SPDX-License-Identifier: GPL-3.0-only
//
// This file is GPLv3, NOT MIT like the rest of this repository: the 12 algorithms below are ported from
// Rob Scape's Noisferatu (https://github.com/rob-scape/noisferatu), a GPLv3 project, so they are a
// derivative work and inherit its license. A build with ENGINE=glitch is therefore distributed under
// GPLv3. See src/engine/glitch/{NOTICE.md,LICENSE}.
// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <cmath>

// glitch::Voice - one lo-fi "glitch" voice: a bank of 12 curated algorithms ported from Rob Scape's
// Noisferatu (firmware/Noisferatu/algos.h upstream, originally an Arduino sketch at 16 kHz). This is a clean,
// de-Arduino'd, per-instance reimplementation:
//   * no <Arduino.h>: fixed-width ints from <cstdint>, the sketch's own xorshift PRNG replaces random().
//   * no file-scope globals: every algorithm's state is a member, so two voices (deck A/B) are independent
//     (the sketch shared one global state set - fine for a single oscillator, a multi-instance bug here).
//   * sample-rate agnostic: the sketch baked phase increments as freq * 2^32/16000 and decay coefficients
//     for 16 kHz. Here phase-per-Hz is derived from the injected rate and the decay constants are retuned
//     to preserve the author's wall-clock envelope times at 48 kHz.
//   * float output (~[-1,1]); the sketch emitted 10-bit signed ints (-512..+511), scaled by 1/512 here.
//
// Only ONE algorithm runs per voice at a time, so the algorithms share a small pool of generic oscillator/
// clock/envelope state (the `State` struct) that set_algo() resets. The three buffer-player algorithms
// (SparseGlitch, WanderWindow, BitMangle) read one shared 4000-sample glitch buffer regenerated on select.
namespace spotykach {
namespace glitch {

enum class Algo : uint8_t {
    SparseGlitch = 0,  // chunked noise/silence buffer, played back with random silence injection (GW1)
    WanderWindow,      // same buffer, a small playback window random-walking through it (GW5)
    BitMangle,         // same buffer, address bits corrupted before each read - SoundScaper-style (GW8)
    TriXorTri,         // two triangles XORed - Benjolin/logic-noise metallic tone
    SquareNand,        // two ramps NANDed - harsh digital logic tone
    FmNoise,           // two inharmonic triangles XORed, ratio re-rolled on a coincidence clock
    RingMod,           // two triangles multiplied (ring modulation)
    PhrygianTri,       // enveloped triangle blips walking a Phrygian scale
    PentaBlips,        // enveloped triangle blips picking random major-pentatonic notes
    BernoulliTris,     // two Bernoulli-gated enveloped triangles (probabilistic intervals)
    Dust,              // sparse filtered random clicks (SuperCollider Dust)
    NoiseRhythm,       // clock-divided rhythmic noise bursts (two enveloped noise voices)
    Count
};

constexpr int kAlgoCount = static_cast<int>(Algo::Count);

class Voice {
public:
    void init(float sample_rate, uint32_t seed) {
        _sr           = sample_rate > 0.f ? sample_rate : 48000.f;
        _phase_per_hz = 4294967296.0f / _sr;       // 2^32 / fs : Hz -> per-sample phase increment
        _rng          = seed ? seed : 0x12345678u; // distinct seed per voice so A/B differ
        // Retune the sketch's hard-coded 16 kHz per-sample decay coefficients to the real rate so the
        // envelope *times* are preserved: d_fs = d16 ^ (16000/fs).
        _phryg_decay = retune_decay(0.9963f);
        _bern_decay  = retune_decay(0.9985f);
        _nr1_decay   = retune_decay(0.965f);
        _nr2_decay   = retune_decay(0.985f);
        _phryg_env_len = static_cast<uint32_t>(0.05f * _sr);  // 50 ms blip (sketch: 800 samples @ 16 kHz)
        set_algo(Algo::SparseGlitch);
    }

    Algo algo() const { return _algo; }

    void set_algo(Algo a) {
        if (a >= Algo::Count) a = static_cast<Algo>(kAlgoCount - 1);
        _algo = a;
        _s = State{};                 // reset all per-run DSP state (rng/config persist)
        // Buffer density per algo: SparseGlitch wants the original GW1 sparseness (1%); the wandering
        // window and bit-mangle need denser material (GW5 used 22%) or they chew on mostly-silence.
        // The wandering window reads a tiny 80-sample slice, so for it the silence runs are also capped
        // below the window size - otherwise a long chunk of zeros can trap the window in silence (the
        // GW1 chunk model chains silent chunks into runs well over 80). SparseGlitch leaves silence
        // uncapped (its sparseness is the point); BitMangle jumps the whole buffer so it needs no cap.
        _buf_noise_prob   = (a == Algo::SparseGlitch) ? 1 : 35;
        _max_silence_run  = (a == Algo::WanderWindow) ? 48 : kBufSize;
        if (is_buffer_algo()) regen();
        // Start the wandering window at a random buffer position (not the fixed index 0).
        if (a == Algo::WanderWindow) _s.windowStart = static_cast<uint16_t>(rand12() % kBufSize);
        recompute();
    }

    void set_p1(float v)    { _p1 = clamp01(v); recompute(); }
    void set_p2(float v)    { _p2 = clamp01(v); recompute(); }
    void set_pitch(float v) { _pitch = clamp01(v); recompute(); }
    float p1() const    { return _p1; }
    float p2() const    { return _p2; }
    float pitch() const { return _pitch; }

    // (Re)fill the shared glitch buffer with the GW1 "sparse glitchy" pattern: mostly silence, occasional
    // latched-noise chunks, plus one short triangle blip. Cheap (~4000 writes); call off the audio path.
    void regen() {
        uint16_t writePos = 0;
        uint16_t silenceRun = 0;
        while (writePos < kBufSize) {
            uint16_t chunk;
            switch (rand12() % 3) { case 0: chunk = 32; break; case 1: chunk = 8; break; default: chunk = 64; break; }
            if (writePos + chunk > kBufSize) chunk = kBufSize - writePos;
            bool writeNoise = (rand12() % 100) < _buf_noise_prob;          // density set per algo
            if (!writeNoise && silenceRun + chunk > _max_silence_run) writeNoise = true;  // break long silence
            if (writeNoise) {
                int16_t n = noise10();
                for (uint16_t i = 0; i < chunk; i++) { if ((rand12() % 10) < 3) n = noise10(); _buf[writePos + i] = n; }
                silenceRun = 0;
            } else {
                for (uint16_t i = 0; i < chunk; i++) _buf[writePos + i] = 0;
                silenceRun += chunk;
            }
            writePos += chunk;
        }
        // One triangle blip somewhere in the buffer (GW1_BLIP_SIZE = 50).
        constexpr uint16_t blipSize = 50;
        const uint16_t blipPos = rand12() % (kBufSize - blipSize);
        const float freq = 220.f + (static_cast<float>(rand12() % 1000) / 1000.f) * (7000.f - 220.f);
        float ph = 0.f; const float inc = freq / _sr;
        for (uint16_t i = 0; i < blipSize; i++) {
            const float t = (ph < 0.5f) ? (ph * 2.f) : (2.f - ph * 2.f);
            _buf[blipPos + i] = static_cast<int16_t>((t * 2.f - 1.f) * 511.f);
            ph += inc; if (ph >= 1.f) ph -= 1.f;
        }
    }

    // One sample, float (roughly [-1,1]; the caller soft-limits). Per-sample hot path - no allocation,
    // no virtual dispatch, one switch on the active algorithm.
    float process() {
        switch (_algo) {
            case Algo::SparseGlitch:  return out(sparse_glitch());
            case Algo::WanderWindow:  return out(wander_window());
            case Algo::BitMangle:     return out(bit_mangle());
            case Algo::TriXorTri:     return out(tri_xor_tri());
            case Algo::SquareNand:    return out(square_nand());
            case Algo::FmNoise:       return out(fm_noise());
            case Algo::RingMod:       return out(ring_mod());
            case Algo::PhrygianTri:   return out(phrygian_tri());
            case Algo::PentaBlips:    return out(penta_blips());
            case Algo::BernoulliTris: return out(bernoulli_tris());
            case Algo::Dust:          dust_tick(); return _dust_state * (1.f / 512.f);
            case Algo::NoiseRhythm:   return out(noise_rhythm());
            default:                  return 0.f;
        }
    }

private:
    static constexpr uint16_t kBufSize = 4000;

    struct Osc { uint32_t ph = 0, inc = 0; };
    struct Clk { uint32_t phase = 0, inc = 0, last = 0; };

    // All per-run DSP state. set_algo() does `_s = State{}` to reset between algorithms.
    struct State {
        Osc o1, o2;
        Clk clk, clk2;
        float    env1 = 0.f, env2 = 0.f;
        uint16_t playPos = 0;  float fpos = 0.f;     // buffer playback head
        uint16_t silenceRemaining = 0;               // SparseGlitch
        uint16_t windowStart = 0;  uint32_t walkCounter = 0;   // WanderWindow
        uint8_t  bitPosition = 5, bitMode = 0;  uint16_t heldBits = 0;  // BitMangle
        uint32_t ctrA = 0, ctrB = 0;                 // generic counters (triggers / env length)
        int8_t   scalePos = 0;  uint8_t divCtr = 0;  // PhrygianTri / NoiseRhythm
    };

    bool is_buffer_algo() const {
        return _algo == Algo::SparseGlitch || _algo == Algo::WanderWindow || _algo == Algo::BitMangle;
    }

    // --- helpers ---------------------------------------------------------------------------------------
    static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    static float expmap(float v, float lo, float hi) { return lo * std::pow(hi / lo, v); }   // log freq map
    float  retune_decay(float d16) const { return std::exp(std::log(d16) * (16000.f / _sr)); }
    uint32_t freq_to_inc(float hz) const { return static_cast<uint32_t>(hz * _phase_per_hz); }
    static float out(int v) { return static_cast<float>(v) * (1.f / 512.f); }

    // xorshift32 (the sketch's PRNG), and its noise/rand helpers.
    uint32_t rnd()      { _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5; return _rng; }
    int16_t  noise10()  { return static_cast<int16_t>(rnd() & 0x3FF) - 512; }   // signed 10-bit
    uint32_t rand12()   { return rnd() & 0x0FFF; }                              // 0..4095

    // Triangle / ramp from a 32-bit phase's top 10 bits, range -512..+511 (the sketch's exact integer form).
    static int tri10(uint32_t phase) {
        const uint32_t p = phase >> 22;
        return p < 512 ? static_cast<int>((p << 1) - 512) : static_cast<int>(1535 - (p << 1));
    }
    static int saw10(uint32_t phase) { return static_cast<int>(phase >> 22) - 512; }

    // Derive every algorithm's increments / periods / coefficients from p1,p2,pitch. Runs at control rate
    // (on any param or algo change), so per-sample code stays branch-light.
    void recompute() {
        _pm = std::exp2((_pitch - 0.5f) * 4.f);   // master pitch: +/- 2 octaves around centre
        switch (_algo) {
            case Algo::SparseGlitch:
                _playback_speed = expmap(_p1, 0.25f, 4.f) * _pm;
                _silence_prob   = _p2;
                break;
            case Algo::WanderWindow:
                _playback_speed = expmap(_p1, 0.25f, 4.f) * _pm;
                _walk_period    = static_cast<uint32_t>(_sr / expmap(_p2, 1.f, 60.f));   // 1..60 Hz walk
                break;
            case Algo::BitMangle:
                _playback_speed = expmap(_p1, 0.25f, 4.f) * _pm;
                _s.clk.inc      = freq_to_inc(expmap(_p2, 0.5f, 30.f));                  // bit-clock 0.5..30 Hz
                break;
            case Algo::TriXorTri:
                _s.o1.inc = freq_to_inc(expmap(_p1, 0.7f, 220.f) * _pm);
                _s.o2.inc = freq_to_inc(expmap(_p2, 0.6f, 440.f) * _pm);
                break;
            case Algo::SquareNand:
                _s.o1.inc = freq_to_inc(expmap(_p1, 0.1f, 50.f) * _pm);
                _s.o2.inc = freq_to_inc(expmap(_p2, 0.08f, 45.f) * _pm);
                break;
            case Algo::FmNoise:
                _fm_base  = expmap(_p1, 20.f, 2000.f) * _pm;
                _s.o1.inc = freq_to_inc(_fm_base);
                _s.o2.inc = freq_to_inc(_fm_base * 1.4983f);                            // inharmonic partner
                _s.clk.inc  = freq_to_inc(expmap(_p2, 0.5f, 50.f));                      // coincidence clocks
                _s.clk2.inc = freq_to_inc(expmap(_p2, 0.5f, 50.f) * 1.33f);
                break;
            case Algo::RingMod:
                _s.o1.inc = freq_to_inc(expmap(_p1, 20.f, 2000.f) * _pm);
                _s.o2.inc = freq_to_inc(expmap(_p2, 20.f, 2000.f) * _pm);
                break;
            case Algo::PhrygianTri:
                _root     = 110.f * _pm;
                _perA     = static_cast<uint32_t>(_sr / expmap(_p1, 1.f, 16.f));         // trigger 1..16 Hz
                _s.clk.inc = freq_to_inc(expmap(_p2, 0.1f, 20.f));                       // burst modulation
                break;
            case Algo::PentaBlips:
                _s.clk.inc  = freq_to_inc(expmap(_p1, 3.f, 11.5f));                      // clock 3..11.5 Hz
                _penta_decay = retune_decay(0.99f + _p2 * 0.0098f);                      // 0.99..0.9998
                break;
            case Algo::BernoulliTris:
                _s.clk.inc = freq_to_inc(5.f);                                           // fixed 5 Hz clock
                _bern_prob1 = _p1; _bern_prob2 = _p2;
                break;
            case Algo::Dust:
                _dust_prob  = _p1 * _p1 * 0.5f;                                           // density (sparse)
                _dust_alpha = expmap(_p2, 0.02f, 1.f);                                    // 1-pole tone
                break;
            case Algo::NoiseRhythm:
                _s.clk.inc = freq_to_inc(expmap(_p1, 1.f, 10.f));                        // clock 1..10 Hz
                _division  = static_cast<uint8_t>(1 + static_cast<int>(_p2 * 6.f));      // /1../7
                break;
            default: break;
        }
    }

    // --- buffer playback helpers -----------------------------------------------------------------------
    void advance(float speed) {
        _s.fpos += speed;
        const uint16_t inc = static_cast<uint16_t>(_s.fpos);
        _s.fpos -= inc;
        _s.playPos += inc;
        while (_s.playPos >= kBufSize) _s.playPos -= kBufSize;
    }

    int sparse_glitch() {
        if (_s.silenceRemaining == 0) {
            uint16_t chunk;
            switch (rand12() % 3) { case 0: chunk = 32; break; case 1: chunk = 8; break; default: chunk = 64; break; }
            if ((static_cast<float>(rand12() % 1000) / 1000.f) < _silence_prob) _s.silenceRemaining = chunk;
        }
        int16_t sample;
        if (_s.silenceRemaining > 0) { _s.silenceRemaining--; sample = 0; }
        else                          sample = _buf[_s.playPos];
        advance(_playback_speed);
        return sample;
    }

    int wander_window() {
        constexpr uint16_t kWindow = kBufSize / 50;   // 80 samples
        if (++_s.walkCounter >= _walk_period) {
            _s.walkCounter = 0;
            const int step = (rand12() & 1) ? (1 + static_cast<int>(rand12() % 20)) : -(1 + static_cast<int>(rand12() % 20));
            int ns = static_cast<int>(_s.windowStart) + step;
            if (ns < 0) ns += kBufSize;
            if (ns >= kBufSize) ns -= kBufSize;
            _s.windowStart = static_cast<uint16_t>(ns);
        }
        uint16_t windowEnd = _s.windowStart + kWindow;
        if (windowEnd > kBufSize) windowEnd = kBufSize;
        if (_s.playPos < _s.windowStart || _s.playPos >= windowEnd) _s.playPos = _s.windowStart;
        const int16_t sample = _buf[_s.playPos];
        _s.fpos += _playback_speed;
        const uint16_t inc = static_cast<uint16_t>(_s.fpos);
        _s.fpos -= inc;
        _s.playPos += inc;
        if (_s.playPos >= windowEnd) _s.playPos = _s.windowStart;
        return sample;
    }

    int bit_mangle() {
        _s.clk.phase += _s.clk.inc;
        const uint32_t state = _s.clk.phase & 0x80000000u;
        if (state && !_s.clk.last) {
            if (rand12() & 1) _s.bitMode = static_cast<uint8_t>(rand12() % 4);   // SET_0/SET_1/XOR/HOLD
            int bp = static_cast<int>(_s.bitPosition) + ((rand12() & 1) ? 1 : -1);
            bp %= 12; if (bp < 0) bp += 12;     // wrap 0..11 (the sketch's % could underflow to a huge shift)
            _s.bitPosition = static_cast<uint8_t>(bp);
            if (_s.bitMode == 3) _s.heldBits = _s.playPos & ((1u << _s.bitPosition) - 1u);
        }
        _s.clk.last = state;
        advance(_playback_speed);
        uint16_t addr = _s.playPos;
        const uint16_t mask = static_cast<uint16_t>((1u << _s.bitPosition) - 1u);
        switch (_s.bitMode) {
            case 0: addr &= ~mask; break;                                   // force low bits 0 (short loops)
            case 1: addr |= mask;  break;                                   // force low bits 1 (jump up)
            case 2: addr ^= mask;  break;                                   // toggle (chaotic jumps)
            case 3: addr = (addr & ~mask) | (_s.heldBits & mask); break;    // freeze low bits (repeats)
        }
        addr %= kBufSize;
        return _buf[addr];
    }

    int tri_xor_tri() {
        _s.o1.ph += _s.o1.inc; _s.o2.ph += _s.o2.inc;
        return static_cast<int16_t>(tri10(_s.o1.ph)) ^ static_cast<int16_t>(tri10(_s.o2.ph));
    }

    int square_nand() {
        _s.o1.ph += _s.o1.inc; _s.o2.ph += _s.o2.inc;
        const int r = (~(saw10(_s.o1.ph) & saw10(_s.o2.ph))) & 0x3FF;   // unipolar 0..1023...
        return r - 512;                                                 // ...centred for audio
    }

    int fm_noise() {
        _s.o1.ph += _s.o1.inc; _s.o2.ph += _s.o2.inc;
        _s.clk.phase += _s.clk.inc; _s.clk2.phase += _s.clk2.inc;
        const uint32_t c1 = _s.clk.phase & 0x80000000u, c2 = _s.clk2.phase & 0x80000000u;
        if ((c1 && !_s.clk.last) && c2) {
            // Coincidence: re-roll osc2's inharmonic ratio (the sketch stored a ratio index but never used
            // it - wired here so the clock actually modulates the timbre, which is the point of FMnoise).
            static const float ratios[8] = { 1.26f, 1.41f, 1.5f, 1.68f, 1.78f, 2.0f, 2.41f, 3.0f };
            _s.o2.inc = freq_to_inc(_fm_base * ratios[rand12() % 8]);
        }
        _s.clk.last = c1;
        return static_cast<int16_t>(tri10(_s.o1.ph)) ^ static_cast<int16_t>(tri10(_s.o2.ph));
    }

    int ring_mod() {
        _s.o1.ph += _s.o1.inc; _s.o2.ph += _s.o2.inc;
        const int32_t product = static_cast<int32_t>(tri10(_s.o1.ph)) * tri10(_s.o2.ph);
        return static_cast<int16_t>(product >> 9);
    }

    int phrygian_tri() {
        // Phrygian intervals (semitone ratios from the root).
        static const float kPhryg[8] = { 1.f, 1.0595f, 1.1892f, 1.3348f, 1.4983f, 1.5874f, 1.7818f, 2.f };
        _s.clk.phase += _s.clk.inc;                       // burst modulation -> irregular trigger timing
        const uint32_t curPeriod = _perA + ((_s.clk.phase >> 22) << 4);
        if (++_s.ctrA >= curPeriod) {
            _s.ctrA = 0;
            _s.scalePos += (rand12() & 1) ? 1 : -1;       // random-walk the scale degree
            if (_s.scalePos < 0) _s.scalePos = 7;
            if (_s.scalePos > 7) _s.scalePos = 0;
            _s.o1.inc = freq_to_inc(_root * kPhryg[_s.scalePos]);   // (the sketch never set this - completed)
            _s.env1 = 1.f; _s.ctrB = 0;
        }
        if (_s.ctrB < _phryg_env_len) { _s.ctrB++; _s.env1 *= _phryg_decay; } else _s.env1 = 0.f;
        _s.o1.ph += _s.o1.inc;
        return static_cast<int>(tri10(_s.o1.ph) * _s.env1);
    }

    int penta_blips() {
        // Major pentatonic ratios from a 220 Hz root (scaled by master pitch).
        static const float kPenta[5] = { 1.f, 1.125f, 1.25f, 1.5f, 1.6875f };
        _s.clk.phase += _s.clk.inc;
        const uint32_t state = _s.clk.phase & 0x80000000u;
        if (state && !_s.clk.last) {
            if (rand12() & 1) {                            // 50/50 Bernoulli gate on the clock
                _s.o1.inc = freq_to_inc(220.f * _pm * kPenta[rand12() % 5]);
                _s.env1 = 1.f;
            }
        }
        _s.clk.last = state;
        _s.env1 *= _penta_decay;
        _s.o1.ph += _s.o1.inc;
        return static_cast<int>(tri10(_s.o1.ph) * _s.env1);
    }

    int bernoulli_tris() {
        _s.clk.phase += _s.clk.inc;
        const uint32_t state = _s.clk.phase & 0x80000000u;
        if (state && !_s.clk.last) {
            if (rand12() & 1) {                            // 50/50 gate on the clock itself
                const float r1 = static_cast<float>(rand12() % 1000) / 1000.f;
                _s.o1.inc = freq_to_inc((r1 < _bern_prob1 ? 220.f : 330.f) * _pm);  // root or perfect 5th
                _s.env1 = 1.f;
                const float r2 = static_cast<float>(rand12() % 1000) / 1000.f;
                _s.o2.inc = freq_to_inc((r2 < _bern_prob2 ? 264.f : 396.f) * _pm);  // minor 3rd or minor 7th
                _s.env2 = 1.f;
            }
        }
        _s.clk.last = state;
        _s.env1 *= _bern_decay; _s.env2 *= _bern_decay;
        _s.o1.ph += _s.o1.inc; _s.o2.ph += _s.o2.inc;
        const int v1 = static_cast<int>(tri10(_s.o1.ph) * _s.env1);
        const int v2 = static_cast<int>(tri10(_s.o2.ph) * _s.env2);
        return (v1 + v2) >> 1;
    }

    void dust_tick() {
        const float r = static_cast<float>(rand12()) * (1.f / 4096.f);
        const int click = (r < _dust_prob) ? noise10() : 0;
        _dust_state += _dust_alpha * (static_cast<float>(click) - _dust_state);
    }

    int noise_rhythm() {
        _s.clk.phase += _s.clk.inc;
        const uint32_t state = _s.clk.phase & 0x80000000u;
        if (state && !_s.clk.last) {
            _s.env1 = 1.f;
            if (++_s.divCtr >= _division) { _s.divCtr = 0; _s.env2 = 1.f; }
        }
        _s.clk.last = state;
        _s.env1 *= _nr1_decay; _s.env2 *= _nr2_decay;
        const int n1 = static_cast<int16_t>(noise10()) & 0x3E0;   // fixed ~5 kHz "highpass" bit mask
        const int e1 = static_cast<int>(n1 * _s.env1);
        const int e2 = static_cast<int>(static_cast<int16_t>(noise10()) * _s.env2);
        return (e1 + e2) >> 1;
    }

    // --- config / state --------------------------------------------------------------------------------
    float    _sr = 48000.f, _phase_per_hz = 4294967296.0f / 48000.f;
    uint32_t _rng = 0x12345678u;
    Algo     _algo = Algo::SparseGlitch;
    float    _p1 = 0.5f, _p2 = 0.5f, _pitch = 0.5f, _pm = 1.f;

    State    _s;
    int16_t  _buf[kBufSize] = {0};   // shared glitch buffer for the three buffer-player algorithms
    uint8_t  _buf_noise_prob = 1;    // regen density (% of chunks that are noise), set per algo
    uint16_t _max_silence_run = kBufSize;  // regen: cap consecutive silence (WanderWindow), else uncapped

    // derived (set in recompute / init)
    float    _playback_speed = 1.f, _silence_prob = 0.f;
    uint32_t _walk_period = 4800, _perA = 12000, _phryg_env_len = 2400;
    float    _root = 110.f, _fm_base = 220.f;
    float    _bern_prob1 = 0.5f, _bern_prob2 = 0.5f;
    float    _dust_prob = 0.f, _dust_alpha = 1.f, _dust_state = 0.f;
    float    _penta_decay = 0.999f, _phryg_decay = 0.9963f, _bern_decay = 0.9985f;
    float    _nr1_decay = 0.965f, _nr2_decay = 0.985f;
    uint8_t  _division = 1;
};

} // namespace glitch
} // namespace spotykach
