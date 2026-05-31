// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/granular_engine.h"
#include "core/mode.h"
#include "core/event.h"
#include "config.h"

using namespace spotykach;

// Mode-dependent dispatch ported from core.ui.cpp's apply pass (the granular "meaning").
// The platform decides WHICH param a control drives; the engine decides what that param
// does given the current deck mode. The deck arg is ignored for global params.
void GranularEngine::set_param(const ParamId id, const Deck::Ref ref, const float v)
{
    const auto deck_ref = _safe_ref(ref);
    _param_cache[static_cast<size_t>(id)][deck_ref] = v;

    auto& deck = _core.deck(deck_ref);
    const auto mode = deck.mode();

    switch (id) {
        case ParamId::Pos:           deck.set_start(v); break;
        case ParamId::FluxFb:        deck.fx().set_flux_fb(v); break;
        case ParamId::Env:           deck.voxs().set_shape(v); break;
        case ParamId::EnvSize:       deck.set_size(v); break;
        case ParamId::Size:
            if (mode == Mode::Drift) deck.voxs().set_win_spread(v);
            else                     deck.set_size(v);
            break;
        case ParamId::Win:           deck.voxs().set_win_size(v); break;
        case ParamId::PolySlice:     deck.set_force_mono((1.f - v) > .5f); break;
        case ParamId::Speed:
            if (mode == Mode::Slice) deck.voxs().set_pitch(v);
            else                     deck.voxs().set_speed(v);
            break;
        case ParamId::FluxIntensity: deck.fx().set_flux_intensity(v); break;
        case ParamId::GritIntensity: deck.fx().set_grit_intensity(v); break;
        case ParamId::FluxMix:       deck.fx().set_flux_mix(v); break;
        case ParamId::GritMix:       deck.fx().set_grit_mix(v); break;
        case ParamId::Feedback:      deck.set_feedback(v); break;
        case ParamId::Mix:           deck.set_inout_mix(v); break;
        case ParamId::ModSpeed:      set_mod_speed(ref, v, false); break; // sync comes via set_mod_speed live
        case ParamId::ModAmp:        _core.mod(deck_ref).set_amp_norm(v); break;

        // Global params (deck arg ignored).
        case ParamId::Tempo:         _core.driver().set_tempo_norm(v); break;
        case ParamId::ClickMix:      _core.set_click_mix(v); break;
        case ParamId::PanSpeed:      _core.panner().set_speed(v); break;
        case ParamId::PanRange:      _core.panner().set_range(v); break;
        case ParamId::KeyInterval:   _core.driver().set_key_tick_interval_norm(v); break;
        case ParamId::Crossfade:     _core.set_mix(v); break;

        case ParamId::Count:         break;
    }
}

float GranularEngine::param(const ParamId id, const Deck::Ref ref) const
{
    return _param_cache[static_cast<size_t>(id)][_safe_ref(ref)];
}

Capabilities GranularEngine::capabilities() const
{
    return CapRecording | CapTapeStorage | CapStepSequencer
         | CapLaunchQuant | CapTransport | CapDualDeck;
}

Deck::Ref GranularEngine::handle_midi_note(const uint8_t channel, const uint8_t note)
{
    auto& c = Config::dynamic();
    auto ref = Deck::Count;
    if (channel == c.midi_channel_a()) ref = Deck::A;
    else if (channel == c.midi_channel_b()) ref = Deck::B;
    if (ref == Deck::Count) return Deck::Count;

    auto e = make_event();
    e.discont = true; // MIDI note: discontinuous (not trailed by a V/Oct change)
    e.p3 = _speed_map.bipolar_pitch2speed(static_cast<float>(note) - 60.f);
    e.p3_on = true;
    _core.deck(ref).trigger(&e);
    return ref;
}

void GranularEngine::stop_if_generating(const Deck::Ref ref)
{
    auto& deck = _core.deck(_safe_ref(ref));
    if (deck.is_generating()) deck.stop();
}

void GranularEngine::clear_buffer(const Deck::Ref ref)
{
    _core.deck(_safe_ref(ref)).buffer().clear();
}

void GranularEngine::on_record_pad(const Deck::Ref ref, const bool reverse)
{
    const auto src = reverse ? Deck::Source::internal : Deck::Source::external;
    _core.set_source(src, _safe_ref(ref));
    _core.deck(_safe_ref(ref)).toggle_recording();
}

bool GranularEngine::on_play_pad(const Deck::Ref ref, const bool reverse)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.disarm();
    const bool empty = deck.is_empty();
    if (!deck.is_overdubbing() && (!deck.is_playing() || deck.is_reverse() == reverse)) {
        _core.driver().toggle_play(_safe_ref(ref));
    }
    deck.set_reverse(reverse);
    return empty;
}

void GranularEngine::on_seq_toggle_arm(const Deck::Ref ref)
{
    auto& t = _core.deck(_safe_ref(ref)).track();
    if (t.is_armed()) t.disarm();
    else              t.arm(!_core.driver().is_key_sub_quarter());
}

void GranularEngine::on_seq_trigger(const Deck::Ref ref)
{
    auto e = make_event();
    _core.deck(_safe_ref(ref)).trigger(&e);
}

void GranularEngine::clear_sequence(const Deck::Ref ref)
{
    _core.deck(_safe_ref(ref)).clear_sequence();
}

void GranularEngine::disarm_track(const Deck::Ref ref)
{
    auto& t = _core.deck(_safe_ref(ref)).track();
    if (t.is_armed()) t.disarm();
}

void GranularEngine::set_mod_speed(const Deck::Ref ref, const float value, const bool sync)
{
    _param_cache[static_cast<size_t>(ParamId::ModSpeed)][_safe_ref(ref)] = value;
    _core.mod(_safe_ref(ref)).set_speed_norm(value, sync);
}

void GranularEngine::cv_mix(const Deck::Ref ref, const float value)
{
    _core.deck(_safe_ref(ref)).inout_mix_mod_in(value);
}

void GranularEngine::cv_size_pos(const Deck::Ref ref, const float value)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.size_mod_in(value);
    deck.start_mod_in(value);
}

void GranularEngine::cv_voct(const Deck::Ref ref, const float value)
{
    const auto deck_ref = _safe_ref(ref);
    const auto speed = _speed_map.bipolar_pitch2speed(value);
    _voct_speed[deck_ref] = speed;
    _core.deck(deck_ref).voxs().pitch_speed_mod_in(speed);
}

void GranularEngine::cv_crossfade(const float value)
{
    _core.mix_mod_in(value);
}

void GranularEngine::on_gate_trigger(const Deck::Ref ref)
{
    const auto deck_ref = _safe_ref(ref);
    auto e = make_event();
    e.p3 = _voct_speed[deck_ref];
    e.p3_on = true;
    _core.deck(deck_ref).trigger(&e);
}

bool GranularEngine::gate_out_triggered(const Deck::Ref ref)
{
    return _core.deck(_safe_ref(ref)).voxs().read_reset_is_triggered();
}

bool GranularEngine::audio_is_empty(const Deck::Ref ref)
{
    return _core.deck(_safe_ref(ref)).is_empty();
}

uint8_t* GranularEngine::audio_data(const Deck::Ref ref)
{
    return reinterpret_cast<uint8_t*>(_core.deck(_safe_ref(ref)).buffer().raw());
}

size_t GranularEngine::audio_recorded_bytes(const Deck::Ref ref)
{
    return _core.deck(_safe_ref(ref)).buffer().rec_size() * sizeof(Buffer::Frame);
}

size_t GranularEngine::audio_capacity_bytes(const Deck::Ref ref)
{
    return _core.deck(_safe_ref(ref)).buffer().size() * sizeof(Buffer::Frame);
}

void GranularEngine::audio_apply_loaded(const Deck::Ref ref, const size_t frames)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.buffer().set_rec_size(frames);
    deck.apply_start_size();
}

void GranularEngine::set_fx(const Deck::Ref ref, const FxKind kind, const bool on)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    if (kind == FxKind::Flux) fx.set_flux_on(on);
    else                      fx.set_grit_on(on);
}

void GranularEngine::toggle_fx_lock(const Deck::Ref ref, const FxKind kind)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    if (kind == FxKind::Flux) fx.toggle_flux_lock();
    else                      fx.toggle_grit_lock();
}

void GranularEngine::handle_midi_transport(const bool start)
{
    auto& c = Config::dynamic();
    if (start) {
        _core.driver().reset();
        if (c.midi_play_stop_a() && !_core.deck(Deck::A).is_empty()) _core.deck(Deck::A).play();
        if (c.midi_play_stop_b() && !_core.deck(Deck::B).is_empty()) _core.deck(Deck::B).play();
    }
    else {
        if (c.midi_play_stop_a()) _core.deck(Deck::A).stop();
        if (c.midi_play_stop_b()) _core.deck(Deck::B).stop();
    }
}
