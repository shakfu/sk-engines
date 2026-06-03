#include "calibrator.h"
#include "expose.h"

// Size-optimize: control calibration runs only at startup, never the audio path.
#pragma GCC optimize("Os")

using namespace spotykach;
using namespace daisy;
using namespace infrasonic;

Calibrator::Calibrator(Hardware& hw, Settings& settings):
    _hw         { hw },
    _settings   { settings },
    _phase      { Phase::idle },
    _step       { Step::neg3v },
    _state      { State::waiting }
    {};

void Calibrator::init(const bool calibrate) 
{
    if (calibrate) {
        if (_phase != Phase::idle) return;
        _collect_offset();
        _calibrate();
    }
    else {
        apply_and_exit();
    }
}

float Calibrator::correct(Hardware::CvInputId id, const float input) {
    return input - _settings.data().offset[id];
}

void Calibrator::_calibrate() 
{
    _phase = Phase::calibrating;
    _step = Step::neg3v;
    _wait();
}

void Calibrator::next() 
{
    if (_state[0] == State::collecting || _state[0] == State::deviation || _state[1] == State::deviation) return;
    auto went_next = false;
    switch (_step) {
        case Step::neg3v: _step = Step::neg1v; went_next = true; break;
        case Step::neg1v: _step = Step::pos1v; went_next = true; break;
        case Step::pos1v: _step = Step::pos3v; went_next = true; break;
        case Step::pos3v: break;
    }
    if (went_next) _wait();
}

void Calibrator::previous() 
{
    if (_state[0] == State::collecting) return;
    switch (_step) {
        case Step::neg3v: init(true); break;
        case Step::neg1v: _step = Step::neg3v; break;
        case Step::pos1v: _step = Step::neg1v; break;
        case Step::pos3v: _step = Step::pos1v; break;
    }
    _wait();
}

void Calibrator::collect() 
{
    if (_state[0] != State::ok && _state[0] != State::collecting) {
        _collect(Hardware::CV_V_OCT_A);
    }
    if (_state[1] != State::ok && _state[1] != State::collecting) {
        _collect(Hardware::CV_V_OCT_B);
    }
    if (_state[0] == State::ok && _state[1] == State::ok) next();
}

void Calibrator::_collect(Hardware::CvInputId id) {
    auto state_idx = id == Hardware::CV_V_OCT_A ? 0 : 1;
    _state[state_idx] = State::collecting;
    auto avg = 0.f;
    for (int i = 0; i < kMaxAvgCount; i++) {
        avg += _hw.GetControlVoltageValue(id);
        daisy::System::Delay(4);
    }
    avg /= static_cast<float>(kMaxAvgCount);

    auto& data = _settings.data();
    auto target = id == Hardware::CV_V_OCT_A ? &(data.v_oct_a) : &(data.v_oct_b);
    auto defaults = _defaults.v_oct_a;
    switch (_step) {
        case Step::neg3v: 
            if (std::fabs(avg - defaults.neg3v) > kDeviationTolerance) { 
                _state[state_idx] = State::deviation; 
            }
            else { 
                target->neg3v = avg;
                _state[state_idx] = State::ok; 
            }
            break;

        case Step::neg1v: 
            if (std::abs(avg - defaults.neg1v) > kDeviationTolerance) {
                _state[state_idx] = State::deviation;
            }
            else { 
                target->neg1v = avg; 
                _state[state_idx] = State::ok;
            }
            break;

        case Step::pos1v: 
            if (std::abs(avg - defaults.pos1v) > kDeviationTolerance) { 
                _state[state_idx] = State::deviation; 
            }
            else { 
                target->pos1v = avg; 
                _state[state_idx] = State::ok; 
            }
            break;

        case Step::pos3v: 
            if (std::abs(avg - defaults.pos3v) > kDeviationTolerance) {
                _state[state_idx] = State::deviation;
            }
            else { 
                target->pos3v = avg; 
                _state[state_idx] = State::ok; 
            }
            break;
    }
}


void Calibrator::apply_and_exit() 
{
    _voct_a.Init(_settings.data().v_oct_a);
    _voct_b.Init(_settings.data().v_oct_b);
    _phase = Phase::idle;
}

void Calibrator::_collect_offset() {
    _state[0] = State::collecting;
    _phase = Phase::collecting_offset;
    for (auto i = 0; i < Hardware::CV_LAST; i++) {
        auto avg = 0.f;
        for (auto j = 0; j < kMaxAvgCount; j++) {
            avg += _hw.GetControlVoltageValue(Hardware::CvInputId(i));
            daisy::System::Delay(4);
        }
        avg = avg / static_cast<float>(kMaxAvgCount);
        if (std::abs(avg) > 0.03) {
            _state[0] = State::deviation;
            return;
        }
        _settings.data().offset[i] = avg;
    }
}

void Calibrator::_wait() 
{
    std::memset(&_state, int(State::waiting), 2 * sizeof(State));
}