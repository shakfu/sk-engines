#include "fx.h"
#include "../../common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Fx::Fx():
_flux_int       { .5f },
_flux_mix_norm  { .7f },
_flux_fb        { .5f },
_flux_on        { false },
_flux_lock      { false },
_grit_mode      { GritMode::Drive },
_grit_on        { false },
_grit_lock      { false }
{}

void Fx::init(const Params p)
{
    _flux_switch.init(p.sample_rate);
    for (auto i = 0; i < 2; i++) {
        _echo_delay[i].Init(p.sample_rate, p.delay_buf[i]);
        _echo_delay[i].SetLagTime(0.5f);
    }
    _apply_flux_int(true);
    _apply_flux_mix();
    _apply_flux_fb();
 
    _grit_switch.init(p.sample_rate);
    _drive.init(p.sample_rate);
    _reduce.init(p.sample_rate);
    set_grit_intensity(_drive.intensity());
    set_grit_mix(_drive.mix());
}

void Fx::switch_grit_mode() 
{
    if (_grit_mode == GritMode::Drive) {
        _grit_mode = GritMode::Reduce;
    } 
    else {
        _grit_mode = GritMode::Drive;
    }
}
void Fx::set_grit_on(const bool on) 
{
    _grit_on = on;
    if (_grit_lock) return;
    _grit_switch.set_on(_grit_on);
}
void Fx::toggle_grit_lock() 
{
    _grit_lock = !_grit_lock;
    _grit_switch.set_on(_grit_on || _grit_lock);
}
void Fx::set_grit_intensity(const float norm)
{
    auto clamp = fclamp(norm, 0.0f, 1.0f);
    switch (_grit_mode) {
        case GritMode::Drive: _drive.set_intensity(clamp); break;
        case GritMode::Reduce: _reduce.set_intensity(clamp); break;
    }
}
float Fx::grit_intensity()
{
    switch (_grit_mode) {
        case GritMode::Drive: return _drive.intensity();
        case GritMode::Reduce: return _reduce.intensity();
        default: return 0.0f;
    }
}
void Fx::set_grit_mix(const float norm)
{
    auto clamp = fclamp(norm, 0.0f, 1.0f);
     switch (_grit_mode) {
        case GritMode::Drive: _drive.set_mix(clamp); break;
        case GritMode::Reduce: _reduce.set_mix(clamp); break;
    }
}
float Fx::grit_mix()
{
    switch (_grit_mode) {
        case GritMode::Drive: return _drive.mix();
        case GritMode::Reduce: return _reduce.mix();
        default: return 0.f;
    }
}
 
void Fx::set_flux_on(const bool on) 
{
    _flux_on = on;
    if (_flux_lock) return;
    _flux_switch.set_on(_flux_on);
}
void Fx::toggle_flux_lock() 
{
    _flux_lock = !_flux_lock;
    _flux_switch.set_on(_flux_on || _flux_lock);
}
void Fx::set_flux_intensity(const float norm) 
{
    _flux_int = fclamp(norm, 0.f, 1.f);
    _apply_flux_int();
}
void Fx::_apply_flux_int(const bool hard)
{
    auto map_int = fmap(_flux_int, 0.01f, 2.f);
    for (auto& d: _echo_delay) d.SetDelayTime(map_int, hard);
}
void Fx::set_flux_fb(const float norm)
{
    _flux_fb = fclamp(norm, 0.f, 1.f);
    _apply_flux_fb();
}
void Fx::_apply_flux_fb()
{
    for (auto& d: _echo_delay) d.SetFeedback(_flux_fb);
}
void Fx::set_flux_mix(const float norm)
{
    _flux_mix_norm = fclamp(norm, 0.0f, 1.0f);
    _apply_flux_mix();
}
void Fx::_apply_flux_mix()
{
    _flux_mix = dbfs2lin(fmap(_flux_mix_norm, -40.f, 0.f));
}

void Fx::process(float& inout0, float& inout1)
{
    float out[2] = { inout0, inout1 };
    
    auto grit_kof = _grit_switch.process();
    if (!_grit_switch.is_idle()) {
        float grit[2] = { inout0, inout1 };
        switch (_grit_mode) {
            case GritMode::Drive: _drive.process(grit[0], grit[1]); break;
            case GritMode::Reduce: _reduce.process(grit[0], grit[1]); break;
        }
        out[0] = grit[0] * grit_kof + out[0] * (1.f - grit_kof);
        out[1] = grit[1] * grit_kof + out[1] * (1.f - grit_kof);
    }

    auto send = _flux_switch.process();
    for (auto i = 0; i < 2; i++) {
        out[i] += _echo_delay[i].Process(out[i] * send) * _flux_mix;
    }

    inout0 = out[0];
    inout1 = out[1];
}
