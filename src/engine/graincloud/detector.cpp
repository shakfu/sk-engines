#include "detector.h"
#include "daisysp.h"
#include <algorithm>
#include <cmath>

using namespace spotykach;

Detector::Detector(): 
_average       { 0 },
_db_threshold  { -40.f },
_write_head    { 0 },
_read_head     { 0 },
_is_armed      { false },
_is_open       { false },
_will_close    { false },
_is_closing    { false }
{};

void Detector::init(float** buf, const float sample_rate)
{
  _buffer = buf;
  // 10 ms detection window, clamped to the static buffer capacity (kWindow).
  _window = std::min(kWindow, static_cast<size_t>(std::lround(0.010f * sample_rate)));
}

void Detector::set_armed(const bool armed) {
    if (armed && !_is_armed) {
        _is_open = false;
        _write_head = 0;
        _read_head = 0;
        _average = 0;
        _will_close = false;
        _is_closing = false;
        auto size = sizeof(float) * _window;
        std::memset(_buffer[0], 0, size);
        std::memset(_buffer[1], 0, size);
    } 
    else if (!armed && _is_armed) {
      _will_close = _is_open;
    }
    _is_armed = armed;
};

void Detector::set_treshold(const float value) {
    if (value <= 0) _db_threshold = -90.f;
    else _db_threshold = 20.f * daisysp::fastlog10f(value);
};

void Detector::process(const float in0, const float in1, float& out0, float& out1) {
    if (_is_open) {
        // Read the buffer
        out0 = _buffer[0][_read_head];
        out1 = _buffer[1][_read_head];

      if (++_read_head == _window) {
        _read_head = 0;
        
        // If it's the last cycle, close the gate.
        if (_is_closing) {
          _is_open = false;
          _is_closing = false;
        }
        else if (_will_close) {
          // After recording is set to off, we do one more cycle, so the 
          // downstream Buffer has a time to fade out.
          _is_closing = true;
          _will_close = false;
        }
      }
    }
    else {
        out0 = 0;
        out1 = 0;
    }

    if (!_is_armed && !_is_closing) {
      return;
    }
    
    // Write buffer
    _buffer[0][_write_head] = in0;
    _buffer[1][_write_head] = in1;

    // Make sample positive
    auto abs_in = std::abs(std::max(in0, in1));

    // Exponential Moving Average (EVA) aka IIR one-pole filter.
    // Because kKofRise is way higher than
    // kKofFall, the contribution of the samples above
    // average is higher than of those below.
    // So the detector has fast attack and slow release.
    auto kof = (abs_in > _average) ? kKofRise : kKofFall;
    _average = abs_in + kof * (_average - abs_in);
    
    if (++_write_head == _window) {
      // Convert average to dB
      auto db_average = 20.f * daisysp::fastlog10f(_average);

      // If average exceedes the treshold open the gate
      if (db_average >= _db_threshold) {
        _read_head = 0;
        _is_open = true;
      }
      _average = 0;
      _write_head = 0;
    }
};
