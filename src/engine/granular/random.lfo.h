#pragma once

#include "daisysp.h"

namespace spotykach {

class RandomLFO {
public:
    RandomLFO();
    ~RandomLFO() = default;

    void init(const float sample_rate);

    void set_freq(const float freq) { _phase_inc = freq * _sr_kof; }

    void set_amp(const float amp) { _noise.SetAmp(amp); }

    void reset() { _phase = 0; _flip = false; }

    float process();

private:
    daisysp::WhiteNoise _noise;

    float _sr_kof;
    float _phase;
    float _phase_inc;
    float _sample;
    bool  _flip;
};
    
};
