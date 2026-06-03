#include "modulator.h"
#include "config.h"
#include "hann.h"

using namespace spotykach;

void Modulator::init(const float sample_rate)
{
    _lfo.init(sample_rate);
}

void Modulator::set_type(const Type type)
{
    if (type != _type && type == Type::Follow) _follower.reset();
    _type = type;
}

void Modulator::set_lfo_type(const LFO::Type type)
{
    _lfo.set_type(type);
}

void Modulator::set_speed_norm(const float norm, const bool sync)
{
    auto speed = curved_value(norm);
    _is_synced = sync;
    
    if (sync) {
        auto ticks = kFreqDiv[std::clamp(1.f - norm, 0.f, 1.f) * kFreqDiv.size() - 1];
        _freq_mult = 4.f / ticks;
        _ticks_to_reset = std::max(1.f, ticks);
    }
    else {
        _lfo.set_speed(speed * kLFOFreqRange + kLFOFreqMin);
    }
    _follower.set_speed(speed); 
}

void Modulator::tick(const float tempo, const bool is_quarter)
{
    if (!_is_synced) return;
    
    if (is_quarter && _reset_count % 4) {
        _reset_count += _reset_count % 4;
    }

    auto freq = tempo * 0.01666666667 * _freq_mult; // freq_mult * tempo / 60
    _lfo.set_speed(freq);
    if (_reset_count++ >= _ticks_to_reset) {
        _lfo.reset();
        _reset_count = 1;
    }
}


void Modulator::set_amp_norm(const float norm_val)
{
    _follower.set_amp(norm_val);
    _lfo.set_glow(norm_val);
}

void Modulator::follow(const float in) 
{
    if (_type == Type::Follow) {
        _follower.process(in);
    }
}

void Modulator::process(float& out)
{
    switch (_type) {
        case Type::Follow: out = _follower.value(); break;
        case Type::LFO: out = _lfo.process(); break;
    }
}
