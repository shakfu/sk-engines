#include "adenv.h"
#include <algorithm>

using namespace spotykach;

ADEnvelope::ADEnvelope():
  _out          { 0.f },
  _shape        { 0.f },
  _t_attack_kof { 0.f },
  _t_decay_kof  { 0.f },
  _t_attack     { 0.f },
  _t_decay      { 0.f },
  _phase        { 0.f },
  _smooth_shape { false }
  {};

void ADEnvelope::set_length(const size_t value) { 
    if (value != _length) {
        _length = value;
        _make_shape();
        _smooth_shape = true;
    }
}

void ADEnvelope::set_shape(float shape) { 
    if (std::abs(shape - _shape) > 0.02) {
        if (shape < 0.02) shape = 0.f;
        if (shape > 0.98) shape = 1.f;
        _shape = shape;
        _make_shape();
        _smooth_shape = true;
    }
}

void ADEnvelope::_make_shape() {
    if (_shape < .33f) {
        _t_attack = 0.f;
        _t_decay = 3.f * _length * _shape;
    }
    else if (_shape >= .33f && _shape < .99f) {
        auto shape = 1.515f * (_shape - .33f);
        _t_attack = _length * shape;
        _t_decay = _length * (1.f - shape);
    }
    else {
        _t_attack = _length;
        _t_decay = 0;
    }
    _decay_phase = _length - _t_decay;
    _t_attack_kof = 1.f / _t_attack;
    _t_decay_kof = 1.f / _t_decay;
}

void ADEnvelope::reverse() {
    _phase = std::max(_length - _phase, 0.f);
}

void ADEnvelope::trigger() {
    if (_phase > _decay_phase) {
        _phase = _ph_attack(_out);
    }
    else {
        _phase = 0.f;
    }
    _smooth_shape = false;
};

float ADEnvelope::process(const float increment) {
    if (_phase < _t_attack) {
        _target_out = _amp_attack(_phase * _t_attack_kof);
    }
    else if (_phase >= _t_attack && _phase <= _decay_phase) {
        _target_out = 1.f;
    }
    else if (_phase > _decay_phase && _phase < _length) {
        _target_out = _amp_decay((_phase - _decay_phase) * _t_decay_kof);
    }

    _phase += increment;

    if (_smooth_shape && std::abs(_target_out - _out) > 0.001) {
        _out += (_target_out - _out) * 0.002604166667f; //8ms
    }
    else {
        _out = _target_out;
        _smooth_shape = false;
    }

    return std::clamp(_out, 0.f, 1.f);
};

void ADEnvelope::reset() {
    _phase = 0.f;
}
