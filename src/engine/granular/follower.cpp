#include "follower.h"
#include "daisysp.h"
#include <cmath> // std::exp

using namespace spotykach;

Follower::Follower():
_value	{ 0.f },
_amp	{ kAmpMult }
{}

float Follower::value() const 
{ 
	return daisysp::fclamp(_value * _amp, 0.f, 1.f);  
}

void Follower::set_speed(const float norm)
{
	auto att_ms = kMinFadeMs + norm * kAttackRangeMs;
    _attack_ms = std::exp(kof / att_ms);

	auto rel_ms = kMinFadeMs + norm * kReleaseRangeMs;
    _release_ms = std::exp(kof / rel_ms);
}

void Follower::set_amp(const float norm)
{
	_amp = norm * norm * kAmpMult;
}

void Follower::process(const float in)
{
	//mean-square
	auto sqin = in * in;

	//Get current value 
	auto mult = sqin > _value ? _attack_ms : _release_ms;
	auto value = mult * (_value - sqin) + sqin;
	
	//Underflow check
	if ((value > 0.f && value <= 1.175494351e-19f)
        || (value < 0.f && value >= -1.175494351e-19f)) {
			value = 0;
		}
	
	_value = daisysp::fclamp(value, 0.f, 1.f);
}

void Follower::reset()
{
	_value = 0.f;
}
