#include "panner.h"
#include "daisysp.h"

using namespace spotykach;

Panner::Panner():
_speed { 0.f },
_range { 1.f }
{}

void Panner::init(const float sample_rate)
{
    _sample_rate = sample_rate;
    _xfade[0].SetStage(.5f);
    _xfade[1].SetStage(.5f);
    _target_stage[0] = kMid;
    _target_stage[1] = kMid;

    _samples_to_change.fill(0);
}

void Panner::set_mode(const Mode mode, const uint8_t chan)
{
    if (mode == _mode[chan]) return;
    _mode[chan] = mode;

    switch (_mode[chan]) {
        case Mode::off: break;
        case Mode::smooth: _schedule_smooth(chan); break;
        case Mode::step: _schedule_step(chan); break;
    }
}

void Panner::set_speed(const float norm)
{
    _speed = std::clamp(norm, 0.f, 1.f);
}

void Panner::set_range(const float norm)
{
    _range = daisysp::fmap(norm, kMinDeviation, kMinDeviation + kDeviationRange);
}

void Panner::tick()
{
    for (uint8_t i = 0; i < 2; i++) {
        if (_mode[i] != Mode::step) continue;
        if (_ticks_count[i]++ >= _ticks_to_change[i]) {
            _schedule_smooth(i, true);
        }
    }
}

void Panner::process(float* in[2], float* out[2])
{
    for (uint8_t i = 0; i < 2; i++) {
        switch (_mode[i]) {
            case Mode::off:
                out[i][0] = in[i][0];
                out[i][1] = in[i][1]; 
                continue;

            case Mode::smooth:
                if (!_transition(i)) _schedule_smooth(i);
                break;

            case Mode::step:
                if (!_transition(i) && !_pending_step.test(i)) _schedule_step(i);
                break;
        }
        _xfade[i].Process(in[i][0], 0, 0, in[i][1], out[i][0], out[i][1]);
    }
}

bool Panner::_transition(const uint8_t chan)
{
    if (_sample_count[chan] < _samples_to_change[chan]) {
        auto stage = (_sample_count[chan] * _smooth_change_kof[chan] + _prev_stage[chan]) * kStageKof;
        _xfade[chan].SetStage(stage);
        _sample_count[chan] ++;
        return true;
    }
    return false;
}   

void Panner::_schedule_stage(const uint8_t chan)
{
    _prev_stage[chan] = _target_stage[chan];
    auto move_left = _target_stage[chan] == kMid ? _dice(_rand) > .5f : _target_stage[chan] > kMid;
    auto deviation = kMid * _range * _dice(_rand);
    if (move_left) _target_stage[chan] = kMid - deviation;
    else _target_stage[chan] = kMid  + deviation;
}

void Panner::_schedule_step(const uint8_t chan)
{
    _ticks_to_change[chan] = kMinTicksToChange + std::round(_dice(_rand) * kTicksToChangeRange * (1.f - _speed));
    _ticks_count[chan] = 0;
    _pending_step.set(chan);
}

void Panner::_schedule_smooth(const uint8_t chan, const bool fast)
{
    _schedule_stage(chan);
    _samples_to_change[chan] = (fast ? .01f : (kMinSecToChange + _dice(_rand) * kSecToChangeRange * (1.f - _speed))) * _sample_rate;
    _smooth_change_kof[chan] = static_cast<float>(_target_stage[chan] - _prev_stage[chan]) / _samples_to_change[chan];
    _sample_count[chan] = 0;
    _pending_step.reset(chan);
}
