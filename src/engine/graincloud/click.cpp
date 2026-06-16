#include "click.h"

using namespace spotykach;
using namespace daisysp;

Click::Click():
_counter { _kBeat_counts },
_trigger { false } 
{};

void Click::init(const float sample_rate) {
  _env.Init(sample_rate);
  _env.SetTime(ADSR_SEG_ATTACK, .0);
  _env.SetTime(ADSR_SEG_RELEASE, .01);
  _env.SetSustainLevel(1.0);
  _osc.Init(sample_rate);
  _osc.SetWaveform(Oscillator::WAVE_TRI);
};

float Click::process() {
  if (_trigger) {
    _osc.Reset();
    _osc.SetFreq(_is_key ? 200.f : 150.f);
    if (_counter++ == _kBeat_counts) {
      _counter = 0;
    }
  }
  auto amp = _env.Process(_trigger);
  _osc.SetAmp(amp);
  _trigger = false;
  return _osc.Process();
};
