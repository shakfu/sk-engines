#include "core.ui.h"
#include "Utility/dsp.h"
#include "expose.h"

// Size-optimize this whole TU to reclaim SRAM_EXEC for the LED migration. The only
// audio-adjacent code here is read_cv (per-block 500 Hz control glue, not per-sample DSP),
// so -Os is perf-irrelevant; the audio DSP in core/*.cpp stays -O2 -funroll-loops.
#pragma GCC optimize("Os")

using namespace spotykach;
using namespace infrasonic;
using namespace daisy;

static float snapped_speed(const float speed)
{
    auto s = static_cast<float>(kSpeedSteps.size() - 1);
    auto idx = static_cast<int>(std::clamp(std::round(speed * s), 0.f, s));
    return kSpeedSteps[idx];
}

CoreUI::CoreUI(Hardware& hw, IEngine& engine, Settings& settings, Storage& storage):
_hw                 { hw },
_engine             { engine },
_settings           { settings },
_storage            { storage },
_calibrator         { Calibrator(hw, settings) },
_state              { State::launching },
_show_key_quarter   { false },
_clock_led_on       { false },
_tap_was_tapped     { false }
{};

void CoreUI::init() {
    _hw.StartAdcs();

    _blink_timer.Init();
    _arm_blink_timer.Init();

    _gate_out_timer[Deck::A].Init();
    _gate_out_timer[Deck::B].Init();

    _pot_monitor.Init(_ui_queue, _hw, 500, 0.005f, 0.002f);

    using namespace std::placeholders;
    auto on_touch = std::bind(&CoreUI::_on_pad_touch, this, _1);
    auto on_release = std::bind(&CoreUI::_on_pad_release, this, _1);
    _hw.SetOnTouch(on_touch);
    _hw.SetOnRelease(on_release);

    auto on_quarter = std::bind(&CoreUI::_on_quarter, this, _1);
    _engine.transport_set_on_quarter(on_quarter);

    auto on_clock_out = std::bind(&CoreUI::_process_clock_out, this);
    _engine.transport_set_on_clock_out(on_clock_out);

    for (int i = 0; i < Hardware::LED_LAST; i++) _led[i].init(i);
};

void CoreUI::_init_values()
{
    for (auto ref: { Deck::A, Deck::B }) {
        
        // Engine-derived seeds (start position + fx state) come from the engine's pre-seeded
        // param cache; the rest are the platform's UI-default pickup starting points.
        mv(ParamId::Pos)[ref].set(_engine.param(ParamId::Pos, ref));
        mv(ParamId::Size)[ref].set(1.f);
        mv(ParamId::Speed)[ref].set(.5f);
        mv(ParamId::Mix)[ref].set(.5f);
        mv(ParamId::Feedback)[ref].set(kDefaultFeedback);
        mv(ParamId::Env)[ref].set(0.f);
        mv(ParamId::EnvSize)[ref].set(1.f);
        mv(ParamId::Win)[ref].set(.2f);
        mv(ParamId::PolySlice)[ref].set(.51f);

        mv(ParamId::ModSpeed)[ref].set(.3f);
        mv(ParamId::ModAmp)[ref].set(0.f);

        _hold_clear[ref].init();

        mv(ParamId::GritMix)[ref].set(_engine.param(ParamId::GritMix, ref));
        mv(ParamId::GritIntensity)[ref].set(_engine.param(ParamId::GritIntensity, ref));
        mv(ParamId::FluxMix)[ref].set(_engine.param(ParamId::FluxMix, ref));
        mv(ParamId::FluxIntensity)[ref].set(_engine.param(ParamId::FluxIntensity, ref));
        mv(ParamId::FluxFb)[ref].set(_engine.param(ParamId::FluxFb, ref));
    }

    mv(ParamId::PanRange)[Deck::A].set(.6f);
    mv(ParamId::PanSpeed)[Deck::A].set(1.f);

    mv(ParamId::ClickMix)[Deck::A].set(0.f);
    mv(ParamId::KeyInterval)[Deck::A].set(.0588f); //Corresponds to 1/4th
    mv(ParamId::Tempo)[Deck::A].set(Tempo::abs_to_norm(120.f));
}

void CoreUI::process() 
{
    if (_state == State::launching) return;

    _hw.ProcessDigitalControls();
    _pot_monitor.Process();
    _hw.ProcessPads();
    _process_ui_queue();
    _process_switches();
    _process_gate_out(Deck::A);
    _process_gate_out(Deck::B);

    if (_state == State::init_values) {
        _init_values(); // effectively override the first read of the pots
        _reset_changing_value_id();
        _apply.set();
        _state = State::ready;
    }

    _tap_hold.process();

    auto layout_a = _engine.deck_layout(Deck::A);
    auto layout_b = _engine.deck_layout(Deck::B);
    auto is_chord_a = layout_a == DeckLayout::chord;
    auto is_chord_b = layout_b == DeckLayout::chord;

    auto blink = _arm_blink_timer.HasPassedMs(250);
    if (blink) _arm_blink_timer.Restart();

    for (auto ref: { Deck::A, Deck::B }) {
        if (_hold_clear[ref].process()) {
            _hold_clear[ref].end();
            _engine.clear_sequence(ref);
        }
        // LEDs /////////
        _draw_ring(ref);
        _draw_fx(ref);
        _draw_alt(ref);    
        _draw_play(ref, blink);
    }

    if (_apply.test(Hardware::CTRL_POS_A)) {
        if (_touched.test(FluxA)) {
            _engine.set_param(ParamId::FluxFb, Deck::A, mv(ParamId::FluxFb)[Deck::A].value());
        }
        else {
            _engine.set_param(ParamId::Pos, Deck::A, mv(ParamId::Pos)[Deck::A].value());
        }
    }
    if (_apply.test(Hardware::CTRL_POS_B)) {
        if (_touched.test(FluxB)) {
            _engine.set_param(ParamId::FluxFb, Deck::B, mv(ParamId::FluxFb)[Deck::B].value());
        }
        else {
            _engine.set_param(ParamId::Pos, Deck::B, mv(ParamId::Pos)[Deck::B].value());
        }
    }
    if (_apply.test(Hardware::CTRL_ENV_A)) {
        if (is_chord_a) _engine.set_param(ParamId::EnvSize, Deck::A, mv(ParamId::EnvSize)[Deck::A].value());
        _engine.set_param(ParamId::Env, Deck::A, mv(ParamId::Env)[Deck::A].value());
    }
    if (_apply.test(Hardware::CTRL_ENV_B)) {
        if (is_chord_b) _engine.set_param(ParamId::EnvSize, Deck::B, mv(ParamId::EnvSize)[Deck::B].value());
        _engine.set_param(ParamId::Env, Deck::B, mv(ParamId::Env)[Deck::B].value());
    }
    if (_apply.test(Hardware::CTRL_SIZE_A)) {
        if (is_chord_a) {
            _engine.set_param(ParamId::Win, Deck::A, mv(ParamId::Win)[Deck::A].value());
            _engine.set_param(ParamId::Size, Deck::A, mv(ParamId::Size)[Deck::A].value());
        }
        else if (layout_a == DeckLayout::slice && _touched.test(Alt)) {
            _engine.set_param(ParamId::PolySlice, Deck::A, mv(ParamId::PolySlice)[Deck::A].value());
        }
        else {
            _engine.set_param(ParamId::Size, Deck::A, mv(ParamId::Size)[Deck::A].value());
        }
    }
    if (_apply.test(Hardware::CTRL_SIZE_B)) {
        if (is_chord_b) {
            _engine.set_param(ParamId::Win, Deck::B, mv(ParamId::Win)[Deck::B].value());
            _engine.set_param(ParamId::Size, Deck::B, mv(ParamId::Size)[Deck::B].value());
        }
        else if (layout_b == DeckLayout::slice && _touched.test(Alt)) {
            _engine.set_param(ParamId::PolySlice, Deck::B, mv(ParamId::PolySlice)[Deck::B].value());
        }
        else {
            _engine.set_param(ParamId::Size, Deck::B, mv(ParamId::Size)[Deck::B].value());
        }
    }
    if (_apply.test(Hardware::CTRL_PITCH_A)) {
        if (_touched.test(FluxA)) {
            _engine.set_param(ParamId::FluxIntensity, Deck::A, mv(ParamId::FluxIntensity)[Deck::A].value());
        }
        else if (_touched.test(GritA)) {
            _engine.set_param(ParamId::GritIntensity, Deck::A, mv(ParamId::GritIntensity)[Deck::A].value());
        }
        else {
            auto speed_a = mv(ParamId::Speed)[Deck::A].value();
            if (_pitch_quantized.test(Deck::A)) {
                speed_a = snapped_speed(speed_a);
            }
            _engine.set_param(ParamId::Speed, Deck::A, speed_a);
        }
    }
    if (_apply.test(Hardware::CTRL_PITCH_B)) {
        if (_touched.test(FluxB)) {
            _engine.set_param(ParamId::FluxIntensity, Deck::B, mv(ParamId::FluxIntensity)[Deck::B].value());
        }
        else if (_touched.test(GritB)) {
            _engine.set_param(ParamId::GritIntensity, Deck::B, mv(ParamId::GritIntensity)[Deck::B].value());
        }
        else {
            auto speed_b = mv(ParamId::Speed)[Deck::B].value();
            if (_pitch_quantized.test(Deck::B)) {
                speed_b = snapped_speed(speed_b);
            }
            _engine.set_param(ParamId::Speed, Deck::B, speed_b);
        }
    }
    if (_apply.test(Hardware::CTRL_MODFREQ_A)) {
        if (_tap_hold.passed()) _engine.set_param(ParamId::Tempo, Deck::A, mv(ParamId::Tempo)[Deck::A].value());
        else _engine.set_mod_speed(Deck::A, mv(ParamId::ModSpeed)[Deck::A].value(), _touched.test(Alt));
    }
    if (_apply.test(Hardware::CTRL_MOD_AMT_A)) {
        if (_tap_hold.passed()) _engine.set_param(ParamId::ClickMix, Deck::A, mv(ParamId::ClickMix)[Deck::A].value());
        else _engine.set_param(ParamId::ModAmp, Deck::A, mv(ParamId::ModAmp)[Deck::A].value());
    }
    if (_apply.test(Hardware::CTRL_MODFREQ_B)) {
        if (_tap_hold.passed()) _engine.set_param(ParamId::PanSpeed, Deck::B, mv(ParamId::PanSpeed)[Deck::A].value());
        else _engine.set_mod_speed(Deck::B, mv(ParamId::ModSpeed)[Deck::B].value(), _touched.test(Alt));
    }
    if (_apply.test(Hardware::CTRL_MOD_AMT_B)) {
        if (_tap_hold.passed()) _engine.set_param(ParamId::PanRange, Deck::B, mv(ParamId::PanRange)[Deck::A].value());
        else _engine.set_param(ParamId::ModAmp, Deck::B, mv(ParamId::ModAmp)[Deck::B].value());
    }
    if (_apply.test(Hardware::CTRL_SOS_A)) {
        if (_tap_hold.passed()) {
            _engine.set_param(ParamId::KeyInterval, Deck::A, mv(ParamId::KeyInterval)[Deck::A].value());
        }
        else if (_touched.test(FluxA)) {
            _engine.set_param(ParamId::FluxMix, Deck::A, mv(ParamId::FluxMix)[Deck::A].value());
        }
        else if (_touched.test(GritA)) {
            _engine.set_param(ParamId::GritMix, Deck::A, mv(ParamId::GritMix)[Deck::A].value());
        }
        else if (_touched.test(Alt)) {
            _engine.set_param(ParamId::Feedback, Deck::A, mv(ParamId::Feedback)[Deck::A].value());
        }
        else {
            _engine.set_param(ParamId::Mix, Deck::A, mv(ParamId::Mix)[Deck::A].value());
        }
    }
    if (_apply.test(Hardware::CTRL_SOS_B)) {
        if (_touched.test(FluxB)) {
            _engine.set_param(ParamId::FluxMix, Deck::B, mv(ParamId::FluxMix)[Deck::B].value());
        }
        else if (_touched.test(GritB)) {
            _engine.set_param(ParamId::GritMix, Deck::B, mv(ParamId::GritMix)[Deck::B].value());
        }
        else if (_touched.test(Alt)) {
            _engine.set_param(ParamId::Feedback, Deck::B, mv(ParamId::Feedback)[Deck::B].value());
        }
        else {
            _engine.set_param(ParamId::Mix, Deck::B, mv(ParamId::Mix)[Deck::B].value());
        }
    }

    //Don't forget to reset flags
    _apply.reset();
    if ((!_tap_hold.passed() && !_touched.test(Alt)) && _value_display_timeout.is_passed()) {
        _reset_changing_value_id();
    }
};

// CV ///////////////////////////////////////
void CoreUI::read_cv() {
    // Platform reads + calibrates each CV jack; the engine routes by role.
    auto mix_mod_a = _hw.GetControlVoltageValue(Hardware::CV_MIX_A);
    _engine.cv_mix(Deck::A, _calibrator.correct(Hardware::CV_MIX_A, mix_mod_a));

    auto pos_size_mod_a = _hw.GetControlVoltageValue(Hardware::CV_SIZE_POS_A);
    auto cor_pos_size_mod_a = _calibrator.correct(Hardware::CV_SIZE_POS_A, pos_size_mod_a);
    cor_pos_size_mod_a = std::round(cor_pos_size_mod_a * 1000.f) / 1000.f;
    _engine.cv_size_pos(Deck::A, cor_pos_size_mod_a);

    auto raw_cv_a = _hw.GetControlVoltageValue(Hardware::CV_V_OCT_A);
    _engine.cv_voct(Deck::A, _calibrator.correctVOctA(raw_cv_a));

    auto mix_mod_b = _hw.GetControlVoltageValue(Hardware::CV_MIX_B);
    _engine.cv_mix(Deck::B, _calibrator.correct(Hardware::CV_MIX_B, mix_mod_b));

    auto pos_size_mod_b = _hw.GetControlVoltageValue(Hardware::CV_SIZE_POS_B);
    auto cor_pos_size_mod_b = _calibrator.correct(Hardware::CV_SIZE_POS_B, pos_size_mod_b);
    cor_pos_size_mod_b = std::round(cor_pos_size_mod_b * 1000.f) / 1000.f;
    _engine.cv_size_pos(Deck::B, cor_pos_size_mod_b);

    auto raw_cv_b = _hw.GetControlVoltageValue(Hardware::CV_V_OCT_B);
    _engine.cv_voct(Deck::B, _calibrator.correctVOctB(raw_cv_b));

    auto mix_mod = _hw.GetControlVoltageValue(Hardware::CV_CROSSFADE);
    _engine.cv_crossfade(_calibrator.correct(Hardware::CV_CROSSFADE, mix_mod));
}

// Gate /////////////////////////////////////
void CoreUI::process_gate_in()
{ 
    if (_storage.of(Deck::A).is_idle()) {
        auto a_high = _hw.GetGateInputAState();
        if (a_high && !_gate_in.test(0)) {
            _gate_a_latency.start();
        }
        _gate_in.set(0, a_high);
        if (_gate_a_latency.is_passed()) {
            _engine.on_gate_trigger(Deck::A);
            _show_gate_in(Deck::A);
        }
    }
    
    if (_storage.of(Deck::B).is_idle()) {
        auto b_high = _hw.GetGateInputBState();
        if (b_high && !_gate_in.test(1)) {
            _gate_b_latency.start();
        }
        _gate_in.set(1, b_high);
        if (_gate_b_latency.is_passed()) {
            _engine.on_gate_trigger(Deck::B);
            _show_gate_in(Deck::B);
        }
    }
}
void CoreUI::_process_gate_out(const Deck::Ref ref)
{
    if (_engine.gate_out_triggered(ref)) {
        _gate_out_timer[ref].Restart();
        _gate_out_high[ref] = true;
    }
    if (_gate_out_high[ref] && _gate_out_timer[ref].HasPassedMs(7)) {
        _gate_out_high[ref] = false;
    }
    if (ref == Deck::A) {
        _hw.SetGateOutA(_gate_out_high[ref]);
    }
    else {
        _hw.SetGateOutB(_gate_out_high[ref]);
    }
}

// Knobs ////////////////////////////////////
void CoreUI::_process_ui_queue()
{
    auto layout_a = _engine.deck_layout(Deck::A);
    auto layout_b = _engine.deck_layout(Deck::B);

    auto is_alt_touched = _touched.test(Alt);
    auto fx_a_touched = _touched.test(FluxA) || _touched.test(GritA);
    auto fx_b_touched = _touched.test(FluxB) || _touched.test(GritB);

    int* changing_id_a = &(_changing_value_id[Deck::A]);
    int* changing_id_b = &(_changing_value_id[Deck::B]);

    while(!_ui_queue.IsQueueEmpty())
    {
        auto event = _ui_queue.GetAndRemoveNextEvent();
        if (event.type == UiEventQueue::Event::EventType::potMoved) {
            auto val = event.asPotMoved.newPosition;
            _apply.set(event.asPotMoved.id);
            switch (event.asPotMoved.id) {
                // DECK A //////////////////////////////////////
                case Hardware::CTRL_SOS_A: {
                    mv(ParamId::FluxMix)[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    mv(ParamId::GritMix)[Deck::A].process(val, _touched.test(GritA), changing_id_a);
                    mv(ParamId::KeyInterval)[Deck::A].process(val, !fx_a_touched && !is_alt_touched && _tap_hold.is_holding(), changing_id_a);
                    mv(ParamId::Mix)[Deck::A].process(val, !fx_a_touched && !is_alt_touched && !_tap_hold.is_holding(), changing_id_a);
                    mv(ParamId::Feedback)[Deck::A].process(val, !fx_a_touched && is_alt_touched && !_tap_hold.is_holding(), changing_id_a);
                    break;
                }

                case Hardware::CTRL_SIZE_A:
                    switch (layout_a) {
                        case DeckLayout::single: {
                            mv(ParamId::Size)[Deck::A].process(val, true, changing_id_a);
                        }
                        break;
                        case DeckLayout::slice: {
                            if (_tap_hold.passed() && _engine.size_sets_tempo(Deck::A)) {
                                _size_quarters[Deck::A].process(val, true, changing_id_a);
                                _set_tempo_by_size(Deck::A, val);
                            }
                            else {
                                mv(ParamId::Size)[Deck::A].process(val, !is_alt_touched, changing_id_a);
                                mv(ParamId::PolySlice)[Deck::A].process(val, is_alt_touched, changing_id_a);
                                _size_quarters[Deck::A].set(val);
                            }
                        }
                        break;
                        case DeckLayout::chord: {
                            mv(ParamId::Size)[Deck::A].process(val, !is_alt_touched, changing_id_a);
                            mv(ParamId::Win)[Deck::A].process(val, is_alt_touched, changing_id_a);

                        }
                        break;
                        case DeckLayout::none: break;
                    }
                    break;

                case Hardware::CTRL_POS_A:
                    mv(ParamId::Pos)[Deck::A].process(val, !fx_a_touched, changing_id_a);
                    mv(ParamId::FluxFb)[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    break;

                case Hardware::CTRL_ENV_A:
                    if (layout_a == DeckLayout::chord) {
                        mv(ParamId::Env)[Deck::A].process(val, !is_alt_touched, changing_id_a);
                        mv(ParamId::EnvSize)[Deck::A].process(val, is_alt_touched, changing_id_a);
                    }
                    else {
                        mv(ParamId::Env)[Deck::A].process(val, true, changing_id_a);
                    }
                    break;

                case Hardware::CTRL_PITCH_A: {
                    if (_storage.of(Deck::A).state() == DeckStorage::State::selecting) {
                        _storage.of(Deck::A).select_slot_at(val * kStorageSlotCount);
                        break;
                    }

                    mv(ParamId::FluxIntensity)[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    mv(ParamId::GritIntensity)[Deck::A].process(val, _touched.test(GritA), changing_id_a);
                    mv(ParamId::Speed)[Deck::A].process(val, !fx_a_touched, changing_id_a);
                    if (!fx_a_touched) {
                        _pitch_quantized.set(Deck::A, _touched.test(Alt));
                    }
                    break;
                }

                case Hardware::CTRL_MODFREQ_A: 
                    mv(ParamId::Tempo)[Deck::A].process(val, _tap_hold.passed(), changing_id_a);
                    mv(ParamId::ModSpeed)[Deck::A].process(val, !_tap_hold.passed(), changing_id_a);
                    break;

                case Hardware::CTRL_MOD_AMT_A: 
                    mv(ParamId::ClickMix)[Deck::A].process(val, _tap_hold.passed(), changing_id_a);
                    mv(ParamId::ModAmp)[Deck::A].process(val, !_tap_hold.passed(), changing_id_a);
                    break;

                // DECK B //////////////////////////////////////
                case Hardware::CTRL_SOS_B: {
                    mv(ParamId::FluxMix)[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    mv(ParamId::GritMix)[Deck::B].process(val, _touched.test(GritB), changing_id_b);
                    mv(ParamId::Mix)[Deck::B].process(val, !fx_b_touched && !is_alt_touched, changing_id_b);
                    mv(ParamId::Feedback)[Deck::B].process(val, !fx_b_touched && is_alt_touched, changing_id_b);
                    break;
                }

                case Hardware::CTRL_SIZE_B:
                    switch (layout_b) {
                        case DeckLayout::single: {
                            mv(ParamId::Size)[Deck::B].process(val, true, changing_id_b);
                        }
                        break;
                        case DeckLayout::slice: {
                            if (_tap_hold.passed() && _engine.size_sets_tempo(Deck::B)) {
                                _size_quarters[Deck::B].process(val, true, changing_id_b);
                                _set_tempo_by_size(Deck::B, val);
                            }
                            else {
                                mv(ParamId::Size)[Deck::B].process(val, !is_alt_touched, changing_id_b);
                                mv(ParamId::PolySlice)[Deck::B].process(val, is_alt_touched, changing_id_b);
                                _size_quarters[Deck::B].set(val);
                            }
                        }
                        break;
                        case DeckLayout::chord: {
                            mv(ParamId::Size)[Deck::B].process(val, !is_alt_touched, changing_id_b);
                            mv(ParamId::Win)[Deck::B].process(val, is_alt_touched, changing_id_b);
                        }
                        break;
                        case DeckLayout::none: break;
                    }
                    break;

                case Hardware::CTRL_POS_B:
                    mv(ParamId::Pos)[Deck::B].process(val, !fx_b_touched, changing_id_b);
                    mv(ParamId::FluxFb)[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    break;

                case Hardware::CTRL_ENV_B:
                    if (layout_b == DeckLayout::chord) {
                        mv(ParamId::Env)[Deck::B].process(val, !is_alt_touched, changing_id_b);
                        mv(ParamId::EnvSize)[Deck::B].process(val, is_alt_touched, changing_id_b);
                    }
                    else {
                        mv(ParamId::Env)[Deck::B].process(val, true, changing_id_b);
                    }
                    break;
                
                case Hardware::CTRL_PITCH_B: {                    
                    if (_storage.of(Deck::B).state() == DeckStorage::State::selecting) {
                        _storage.of(Deck::B).select_slot_at(val * kStorageSlotCount);
                        break;
                    }

                    mv(ParamId::FluxIntensity)[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    mv(ParamId::GritIntensity)[Deck::B].process(val, _touched.test(GritB), changing_id_b);

                    auto no_fx_b_touched = !_touched.test(FluxB) && !_touched.test(GritB);
                    mv(ParamId::Speed)[Deck::B].process(val, no_fx_b_touched, changing_id_b);
                    if (no_fx_b_touched) {
                        _pitch_quantized.set(Deck::B, _touched.test(Alt));
                    }
                    break;
                }

                case Hardware::CTRL_MODFREQ_B: 
                    mv(ParamId::PanSpeed)[Deck::A].process(val, _tap_hold.passed(), changing_id_b);
                    mv(ParamId::ModSpeed)[Deck::B].process(val, !_tap_hold.passed(), changing_id_b);
                    break;

                case Hardware::CTRL_MOD_AMT_B:
                    mv(ParamId::PanRange)[Deck::A].process(val, _tap_hold.passed(), changing_id_b);
                    mv(ParamId::ModAmp)[Deck::B].process(val, !_tap_hold.passed(), changing_id_b);
                    break;

                case Hardware::CTRL_CROSSFADE:
                    _engine.set_param(ParamId::Crossfade, Deck::A, val);
                    break;
            }

            _value_display_timeout.start();
        }
    }
}

// Switchess ////////////////////////////////
void CoreUI::_process_switches()
{
    // construct into 8-bit set from inverted bitmask state
    // (all inputs are inverted due to pullups)
    std::bitset<8> sr1 = ~_hw.GetShiftRegState(0);
    std::bitset<8> sr2 = ~_hw.GetShiftRegState(1);

    // The platform reads the switch bits (hardware) and writes them as categorical config; the
    // engine maps each selector to its enums and owns the side effects (item 3a-0).

    // Mode A/B/C switch (mutex) -> route (global; deck arg ignored)
    _engine.set_config(ConfigId::Route, Deck::A, sr1.test(2) ? 2 : sr1.test(3) ? 1 : 0);

    // Mod A Type switch
    if (sr1.test(1)) _engine.set_config(ConfigId::ModType, Deck::A, 1);
    else {
        _engine.set_config(ConfigId::ModType, Deck::A, 0);
        _engine.set_config(ConfigId::LfoShape, Deck::A, sr1.test(0) ? 1 : 0);
    }

    // Mode A switch (engine reports whether it changed, so we re-apply size for the new mode)
    if (_engine.set_config(ConfigId::Mode, Deck::A, sr1.test(6) ? 2 : sr1.test(7) ? 1 : 0)) {
        _apply.set(Hardware::CTRL_SIZE_A);
    }

    // Size/Pos A switch
    _engine.set_config(ConfigId::StartModOn, Deck::A, (sr1.test(5) || !sr1.test(4)) ? 1 : 0);
    _engine.set_config(ConfigId::SizeModOn,  Deck::A, (sr1.test(4) || !sr1.test(5)) ? 1 : 0);

    // Mod B Type switch
    if (sr2.test(5)) _engine.set_config(ConfigId::ModType, Deck::B, 1);
    else {
        _engine.set_config(ConfigId::ModType, Deck::B, 0);
        _engine.set_config(ConfigId::LfoShape, Deck::B, sr2.test(4) ? 1 : 0);
    }

    // Mode B switch
    if (_engine.set_config(ConfigId::Mode, Deck::B, sr2.test(2) ? 2 : sr2.test(3) ? 1 : 0)) {
        _apply.set(Hardware::CTRL_SIZE_B);
    }

    // Size/Pos B switch
    _engine.set_config(ConfigId::StartModOn, Deck::B, (sr2.test(1) || !sr2.test(0)) ? 1 : 0);
    _engine.set_config(ConfigId::SizeModOn,  Deck::B, (sr2.test(0) || !sr2.test(1)) ? 1 : 0);

    // Manual tempo tap switch
    // Update no faster than 500Hz
    static auto is_tap_tapped = false;
    static uint32_t last_tap_update = 0;
    uint32_t now = System::GetNow();
    if(now - last_tap_update >= 2)
    {
        last_tap_update = now;
        is_tap_tapped = sr2.test(6);
    }
    if(is_tap_tapped) {
        if (_tap_was_tapped) return;
        _tap_was_tapped = true;
        
        if (_touched.test(Alt)) {
            _engine.transport_toggle_source();
            _clock_source_changed = true;
            _value_display_timeout.start();
        }
        else if (_touched.test(GritA)) {
            auto rs = _engine.toggle_grit_mode(Deck::A);
            mv(ParamId::GritIntensity)[Deck::A].set(rs.intensity);
            mv(ParamId::GritMix)[Deck::A].set(rs.mix);
        }
        else if (_touched.test(GritB)) {
            auto rs = _engine.toggle_grit_mode(Deck::B);
            mv(ParamId::GritIntensity)[Deck::B].set(rs.intensity);
            mv(ParamId::GritMix)[Deck::B].set(rs.mix);
        }
        else {
            if (!_engine.transport_is_external_sync()) {
                _engine.transport_tap_tempo();
                mv(ParamId::Tempo)[Deck::A].set(Tempo::abs_to_norm(_engine.transport_tempo()));
            }
            if (!_tap_hold.is_holding()) {
                _tap_hold.begin();
            }
        }
    }
    else if (_tap_was_tapped) {
        _reset_changing_value_id();
        _tap_was_tapped = false;
        _tap_hold.end();
    }
}

// Clock /////////////////////////////////////
void CoreUI::_on_quarter(const bool is_key_quarter) 
{
    _clock_led_on = true;
    _show_key_quarter = is_key_quarter;
}

// Calibration //////////////////////////////
void CoreUI::calibrate(const bool recalibrate) {
    _calibrator.init(recalibrate);
}

bool CoreUI::_is_changing(const MValue& value) const
{
    return _changing_value_id[Deck::A] == value.id() || _changing_value_id[Deck::B] == value.id();
}

void CoreUI::_reset_changing_value_id()
{
   _changing_value_id.fill(0);
   _clock_source_changed = false;
}

void CoreUI::_set_tempo_by_size(const Deck::Ref ref, const float fraction)
{
    auto bpm = _engine.tempo_to_fit(ref, fraction);
    auto norm = Tempo::abs_to_norm(bpm); 
    mv(ParamId::Tempo)[Deck::A].set(norm);
    _engine.transport_set_tempo_norm(norm);
}
