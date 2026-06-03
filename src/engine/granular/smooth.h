#pragma once
#include <cmath>

namespace spotykach {

class OnePoleSmoother {
public:
    OnePoleSmoother(): 
    _kof            { 1.f }, 
    _value          { 0.f },
    _is_smoothing   { false }
    {}
    ~OnePoleSmoother() = default;

    void init(const float sample_rate, const float time_s = 0.001f) {
        if (time_s <= 0.f || sample_rate <= 0.0f) {
            _kof = 1.f;
        } 
        else {
            _kof = 1.0f / (time_s * sample_rate);
        }
    }

    float process(const float target_value) {
        auto diff = target_value - _value;
        if (!_is_smoothing && std::abs(diff) < .002f) return _value;
        
        _is_smoothing = true;
        _value += _kof * diff;
        
        if (std::abs(target_value - _value) < .002f) {
            _value = target_value;
            _is_smoothing = false;
        }
        
        return _value;
    }

    void reset(const float value = 0.0f) {
        _value = value;
        _is_smoothing = false;
    }

private:
    float _kof;
    float _value;
    bool _is_smoothing;
};

};
