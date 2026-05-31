#include "core.ui.h"

using namespace spotykach;

void CoreUI::_on_pad_touch(Hardware::Pad pad) 
{
    auto& deck_a = _core.deck(Deck::A);
    auto& deck_b = _core.deck(Deck::B);
    auto is_alt_touched = _touched.test(Alt);
    _reset_changing_value_id();

    using P = Hardware::Pad;
    switch (pad) {
        case P::PlayA: _on_play_touch(Deck::A, false); break;
            
        case P::RevA: _on_play_touch(Deck::A, true); break;

        case P::FluxA:
            _touched.set(FluxA);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(Deck::A, FxKind::Flux);
            } else {
                _engine.set_fx(Deck::A, FxKind::Flux, true);
            }
            break;

        case P::GritA:
            _touched.set(GritA);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(Deck::A, FxKind::Grit);
            }
            else {
                _engine.set_fx(Deck::A, FxKind::Grit, true);
            }
            break;
            
        case P::PlayB: _on_play_touch(Deck::B, false); break;

        case P::RevB: _on_play_touch(Deck::B, true); break;
        
        case P::FluxB:
            _touched.set(FluxB);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(Deck::B, FxKind::Flux);
            } else {
                _engine.set_fx(Deck::B, FxKind::Flux, true);
            }
            break;

        case P::GritB:
            _touched.set(GritB);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(Deck::B, FxKind::Grit);
            }
            else {
                _engine.set_fx(Deck::B, FxKind::Grit, true);
            }
            break;

        case P::SeqA: {
            if (_storage.of(Deck::A).is_selecting()) {
                if (is_alt_touched) _storage.of(Deck::A).previous_tape();
                else _storage.of(Deck::A).next_tape();
            }
            else if (_storage.of(Deck::A).is_idle()) {
                if (_tap_hold.passed() && _core.driver().is_external_sync()) {
                    _core.driver().reset();
                } 
                else if (is_alt_touched) {
                    auto& t = deck_a.track();
                    if (t.is_armed()) t.disarm(); else t.arm(!_core.driver().is_key_sub_quarter());
                    _hold_clear[Deck::A].begin();
                }
                else {
                    auto e = make_event();
                    deck_a.trigger(&e);
                }
            }
            break;
        }
        case P::SeqB: {
            if (_storage.of(Deck::B).is_selecting()) {
                if (is_alt_touched) _storage.of(Deck::B).previous_tape();
                else _storage.of(Deck::B).next_tape();
            }
            else if (_storage.of(Deck::B).is_idle()) {
                if (is_alt_touched) {
                    auto& t = deck_b.track();
                    if (t.is_armed()) t.disarm(); else t.arm(!_core.driver().is_key_sub_quarter());
                    _hold_clear[Deck::B].begin();
                } 
                else {
                    auto e = make_event();
                    deck_b.trigger(&e);    
                }
            }
            break;
        }
        case P::Alt: 
            _on_alt_touch();
            break;

        case P::Spot:
            if (_calibrator.phase() == Calibrator::Phase::calibrating) {
                _calibrator.collect();
            }
            break;

        default: break;
    }
};

void CoreUI::_on_pad_release(Hardware::Pad pad) 
{   
    using P = Hardware::Pad;
    switch (pad) {
        case P::SeqA: 
            _hold_clear[Deck::A].end();
            break;

        case P::SeqB:
            _hold_clear[Deck::B].end();
            break;

        case P::FluxA:
            _touched.reset(FluxA);
            _engine.set_fx(Deck::A, FxKind::Flux, false);
            _changing_value_id[Deck::A] = 0;
            break;

        case P::GritA:
            _touched.reset(GritA);
            _engine.set_fx(Deck::A, FxKind::Grit, false);
            _changing_value_id[Deck::A] = 0;
            break;

        case P::FluxB:
            _touched.reset(FluxB);
            _engine.set_fx(Deck::B, FxKind::Flux, false);
            _changing_value_id[Deck::B] = 0;
            break;

        case P::GritB:
            _touched.reset(GritB);
            _engine.set_fx(Deck::B, FxKind::Grit, false);
            _changing_value_id[Deck::B] = 0;
            break;

        case P::Alt: 
            _touched.reset(Alt);
            _reset_changing_value_id();
            break;
        
        default: break;
    }
};

void CoreUI::_on_play_touch(const Deck::Ref ref, const bool reverse)
{
    auto& deck = _core.deck(ref);

    if (_tap_hold.passed()) {
        if (deck.is_generating()) deck.stop();
        
        auto& s = _storage.of(ref);
        
        if (s.is_processing()) {
            bool is_loading = s.state() == DeckStorage::State::loading;
            _storage.cancel(ref);
            if (is_loading) deck.buffer().clear();
        }
        else if (s.is_idle()) _storage.activate(ref);
        else if (s.is_selecting()) _storage.deactivate(ref);
        return;
    }

    if (_storage.of(ref).is_selecting()) {
        if (_touched.test(Alt)) {
            if (reverse) _storage.load(ref);
            else _storage.save(ref);
        }
        else if (!reverse) {
            _storage.load(ref);
        }
        return;
    }
    
    if (_touched.test(Alt)) {
        auto src = reverse ? Deck::Source::internal : Deck::Source::external;
        _core.set_source(src, ref);
        deck.toggle_recording();
        _storage.of(ref).reset_recent_slot();
    }
    else {
        deck.disarm();
        if (deck.is_empty()) _show_empty(ref);
        if (!deck.is_overdubbing() && (!deck.is_playing() || deck.is_reverse() == reverse)) {
            _core.driver().toggle_play(ref);
        }
        deck.set_reverse(reverse);
    }
}

void CoreUI::_on_alt_touch() 
{
    _touched.set(Alt);
    
    auto& deck_a = _core.deck(Deck::A);
    if (deck_a.track().is_armed()) deck_a.track().disarm();
    if (_touched.test(GritA)) _engine.toggle_fx_lock(Deck::A, FxKind::Grit);
    if (_touched.test(FluxA)) _engine.toggle_fx_lock(Deck::A, FxKind::Flux);

    auto& deck_b = _core.deck(Deck::B);
    if (deck_b.track().is_armed()) deck_b.track().disarm();
    if (_touched.test(GritB)) _engine.toggle_fx_lock(Deck::B, FxKind::Grit);
    if (_touched.test(FluxB)) _engine.toggle_fx_lock(Deck::B, FxKind::Flux);

    if (_tap_hold.passed()) {
        _core.driver().toggle_source();
        _clock_source_changed = true;
        _value_display_timeout.start();
    }
}
