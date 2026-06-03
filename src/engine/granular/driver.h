#pragma once

#include <stdint.h>
#include <array>
#include <functional>

#include "nocopy.h"
#include "engine/itimesource.h"
#include "engine/mode.h"   // ClockSource (Driver::Source alias target, item 5c)
#include "synclock.h"
#include "divider.h"
#include "deck.h"
#include "click.h"
#include "tempo.h"
#include "panner.h"
#include "modulator.h"

namespace spotykach {

// kKeyInterval is contract-owned (engine/mode.h, included above) since Phase 5 R4 - the UI's
// key-interval ring display needs the enum without pulling this header. The lookup array stays here.
static const std::array<kKeyInterval, 17> kKeyIntervals = {
    kKeyInterval::k1_16,
    kKeyInterval::k1_4, 
    kKeyInterval::k2_4,
    kKeyInterval::k3_4, 
    kKeyInterval::k4_4,
    kKeyInterval::k5_4,
    kKeyInterval::k6_4,
    kKeyInterval::k7_4,
    kKeyInterval::k8_4,
    kKeyInterval::k9_4,
    kKeyInterval::k10_4,
    kKeyInterval::k11_4,
    kKeyInterval::k12_4,
    kKeyInterval::k13_4,
    kKeyInterval::k14_4,
    kKeyInterval::k15_4,
    kKeyInterval::k16_4 
};

class Driver {
public:
    using Source = ClockSource::Source; // moved to the contract (engine/mode.h) in item 5c
    
    Driver(Deck&, Deck&, Click&, Panner&, Modulator*);
    ~Driver() {};

    void init(const float sample_rate, const float buffer_size, const ITimeSource* time);

    void toggle_play(const DeckRef::Ref deck);
    
    float tempo() { return _clock.Tempo(); }
    void set_tempo_norm(const float value) { _tempo.set_norm(value); }
    void tap_tempo() { _tempo.tap(); }

    Source source() const { return _source; }
    void toggle_source();
    bool is_external_sync() const { return _source != Source::internal; };

    void set_key_tick_interval_norm(const float); 
    uint8_t key_interval() const { return _key_tick_interval; }
    bool is_key_sub_quarter() const { return _key_tick_interval < k1_4; }
    bool is_key_at_quarter() const { return _key_tick_interval == k1_4; }

    void tick(const bool external_tick);
    
    void set_on_quarter(std::function<void(const bool)> on_quarter) { _on_quarter = on_quarter; }
    void set_on_clock_out(std::function<void()> on_clock_out) { _on_clock_out = on_clock_out; }

    void reset();

private:
    NOCOPY(Driver)

    void _on_clock_tick(const bool external_tick);
    void _toggle_clock();
    void _indicate_quarter();
    void _send_tick(const bool is_common_tick, const bool is_quarter, const float tempo);
    void _make_key();
    void _divider_reset_counts(const bool force = false);
    
    Deck&       _deck_a;
    Deck&       _deck_b;
    SynClock    _clock;
    Divider     _divider;
    Tempo       _tempo;
    Click&      _click;
    Panner&     _panner;

    Modulator*  _mod;

    const ITimeSource* _time = nullptr;
    uint32_t _reset_us = 0;

    std::function<void(const bool /*is key quarter*/)> _on_quarter;
    std::function<void()> _on_clock_out;

    Source _source;
    int8_t _quarter_tick_count;
    int8_t _key_tick_count;
    int8_t _key_tick_interval;
    uint8_t _key_tick_interval_idx;
    bool _is_key;
    bool _tap;
    bool _send_clock;
};

};
