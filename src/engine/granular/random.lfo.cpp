#include "random.lfo.h"

using namespace spotykach;

RandomLFO::RandomLFO():
_sr_kof     { 0.f },
_phase      { 0.f },
_phase_inc  { 0.f }
{};

void RandomLFO::init(const float sample_rate) {
    _sr_kof = 1.f / sample_rate;
    _noise.Init();
    _noise.SetAmp(1.f);
}

float RandomLFO::process() {
    auto flip = _phase < 0.5 ? true : false;
    if (flip != _flip) {
        auto s = _noise.Process();
        _sample = _flip ? s : -s;
        _flip = flip;
    }
    _phase += _phase_inc;
    if (_phase > 1.0f) _phase -= 1.0f;
    return _sample;
}
