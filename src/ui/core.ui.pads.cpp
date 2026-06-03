#include "core.ui.h"

// Pad gesture handling runs only in the main loop (ProcessPads), never the audio path -
// optimize for size to reclaim SRAM_EXEC. Perf-irrelevant here.
#pragma GCC optimize("Os")

using namespace spotykach;

void CoreUI::_on_pad_touch(Hardware::Pad pad)
{
    auto is_alt_touched = _touched.test(Alt);
    _reset_changing_value_id();

    using P = Hardware::Pad;
    switch (pad) {
        case P::PlayA: _on_play_touch(DeckRef::A, false); break;
            
        case P::RevA: _on_play_touch(DeckRef::A, true); break;

        case P::FluxA:
            _touched.set(FluxA);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(DeckRef::A, FxKind::Flux);
            } else {
                _engine.set_fx(DeckRef::A, FxKind::Flux, true);
            }
            break;

        case P::GritA:
            _touched.set(GritA);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(DeckRef::A, FxKind::Grit);
            }
            else {
                _engine.set_fx(DeckRef::A, FxKind::Grit, true);
            }
            break;
            
        case P::PlayB: _on_play_touch(DeckRef::B, false); break;

        case P::RevB: _on_play_touch(DeckRef::B, true); break;
        
        case P::FluxB:
            _touched.set(FluxB);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(DeckRef::B, FxKind::Flux);
            } else {
                _engine.set_fx(DeckRef::B, FxKind::Flux, true);
            }
            break;

        case P::GritB:
            _touched.set(GritB);
            if (is_alt_touched) {
                _engine.toggle_fx_lock(DeckRef::B, FxKind::Grit);
            }
            else {
                _engine.set_fx(DeckRef::B, FxKind::Grit, true);
            }
            break;

        case P::SeqA: {
            if (_storage.of(DeckRef::A).is_selecting()) {
                if (is_alt_touched) _storage.of(DeckRef::A).previous_tape();
                else _storage.of(DeckRef::A).next_tape();
            }
            else if (_storage.of(DeckRef::A).is_idle()) {
                if (_tap_hold.passed() && _engine.transport_is_external_sync()) {
                    _engine.transport_reset();
                }
                else if (is_alt_touched) {
                    _engine.on_seq_toggle_arm(DeckRef::A);
                    _hold_clear[DeckRef::A].begin();
                }
                else {
                    _engine.on_seq_trigger(DeckRef::A);
                }
            }
            break;
        }
        case P::SeqB: {
            if (_storage.of(DeckRef::B).is_selecting()) {
                if (is_alt_touched) _storage.of(DeckRef::B).previous_tape();
                else _storage.of(DeckRef::B).next_tape();
            }
            else if (_storage.of(DeckRef::B).is_idle()) {
                if (is_alt_touched) {
                    _engine.on_seq_toggle_arm(DeckRef::B);
                    _hold_clear[DeckRef::B].begin();
                }
                else {
                    _engine.on_seq_trigger(DeckRef::B);
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
            _hold_clear[DeckRef::A].end();
            break;

        case P::SeqB:
            _hold_clear[DeckRef::B].end();
            break;

        case P::FluxA:
            _touched.reset(FluxA);
            _engine.set_fx(DeckRef::A, FxKind::Flux, false);
            _changing_value_id[DeckRef::A] = 0;
            break;

        case P::GritA:
            _touched.reset(GritA);
            _engine.set_fx(DeckRef::A, FxKind::Grit, false);
            _changing_value_id[DeckRef::A] = 0;
            break;

        case P::FluxB:
            _touched.reset(FluxB);
            _engine.set_fx(DeckRef::B, FxKind::Flux, false);
            _changing_value_id[DeckRef::B] = 0;
            break;

        case P::GritB:
            _touched.reset(GritB);
            _engine.set_fx(DeckRef::B, FxKind::Grit, false);
            _changing_value_id[DeckRef::B] = 0;
            break;

        case P::Alt: 
            _touched.reset(Alt);
            _reset_changing_value_id();
            break;
        
        default: break;
    }
};

void CoreUI::_on_play_touch(const DeckRef::Ref ref, const bool reverse)
{
    if (_tap_hold.passed()) {
        _engine.stop_if_generating(ref);

        auto& s = _storage.of(ref);

        if (s.is_processing()) {
            bool is_loading = s.state() == DeckStorage::State::loading;
            _storage.cancel(ref);
            if (is_loading) _engine.clear_buffer(ref);
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
        _engine.on_record_pad(ref, reverse);
        _storage.of(ref).reset_recent_slot();
    }
    else {
        if (_engine.on_play_pad(ref, reverse)) _show_empty(ref);
    }
}

void CoreUI::_on_alt_touch() 
{
    _touched.set(Alt);
    
    _engine.disarm_track(DeckRef::A);
    if (_touched.test(GritA)) _engine.toggle_fx_lock(DeckRef::A, FxKind::Grit);
    if (_touched.test(FluxA)) _engine.toggle_fx_lock(DeckRef::A, FxKind::Flux);

    _engine.disarm_track(DeckRef::B);
    if (_touched.test(GritB)) _engine.toggle_fx_lock(DeckRef::B, FxKind::Grit);
    if (_touched.test(FluxB)) _engine.toggle_fx_lock(DeckRef::B, FxKind::Flux);

    if (_tap_hold.passed()) {
        _engine.transport_toggle_source();
        _clock_source_changed = true;
        _value_display_timeout.start();
    }
}
