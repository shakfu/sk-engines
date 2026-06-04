// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <stdint.h>
#include <functional>

#include "nocopy.h"
#include "engine/itimesource.h"
#include "engine/itransport.h"     // ITransport + TransportTick (+ ClockSource via mode.h)
#include "engine/engine_leds.h"    // TransportLeds (the clock indicator state the platform renders)
#include "synclock.h"
#include "dsp/divider.h"   // shared clock-divider primitive (also used by granular Deck's sequencer)
#include "tempo.h"

namespace spotykach {

// The platform clock/transport service - the engine-agnostic half of the old granular Driver.
// It owns SynClock + Tempo + Divider, the clock-source selection, tap tempo, key/quarter counting,
// and the clock-out + on-quarter callbacks. It fans a generic TransportTick out to whichever engine
// subscribed (set_on_tick), so any engine - granular looper, tempo-synced delay, Euclidean
// sequencer - reacts to the same clock. The granular per-tick fan-out (deck/panner/mod/click) lives
// in granular Core's sink; this class knows nothing about any engine's DSP.
//
// Ownership: the platform (app + CoreUI) holds the concrete Transport and drives the control API
// below; engines receive only the read-only ITransport view via EngineContext.
class Transport : public ITransport {
public:
    using Source = ClockSource::Source;

    Transport();
    ~Transport() = default;

    void init(const float sample_rate, const float buffer_size, const ITimeSource* time);

    // --- platform control API (CoreUI / app only) -------------------------------------------------
    void tick(const bool external_tick);     // per audio block: advance the clock (CoreUI::tick)
    void reset();
    void toggle_source();
    void tap_tempo() { _tempo.tap(); }
    void set_tempo_norm(const float value) { _tempo.set_norm(value); }
    void set_key_tick_interval_norm(const float);

    void set_on_quarter(std::function<void(const bool)> on_quarter) { _on_quarter = on_quarter; }
    void set_on_clock_out(std::function<void()> on_clock_out) { _on_clock_out = on_clock_out; }

    bool is_key_at_quarter() const { return _key_tick_interval == k1_4; }
    TransportLeds leds() const {
        return { source(), is_key_at_quarter(), is_key_sub_quarter(), is_external_sync(), key_interval() };
    }

    // --- ITransport (engine-facing, read-only + subscribe) ----------------------------------------
    float   tempo() const override { return _clock.Tempo(); }
    Source  source() const override { return _source; }
    bool    is_external_sync() const override { return _source != Source::internal; }
    uint8_t key_interval() const override { return _key_tick_interval; }
    bool    is_key_sub_quarter() const override { return _key_tick_interval < k1_4; }
    void    set_on_tick(std::function<void(const TransportTick&)> on_tick) override { _on_tick = on_tick; }

private:
    NOCOPY(Transport)

    void _on_clock_tick(const bool external_tick);
    void _make_key();
    void _divider_reset_counts(const bool force = false);
    void _emit(const bool tick, const bool is_quarter, const float tempo, const bool reset);

    SynClock _clock;
    Divider  _divider;
    Tempo    _tempo;

    const ITimeSource* _time = nullptr;
    uint32_t _reset_us = 0;

    std::function<void(const bool /*is key quarter*/)> _on_quarter;
    std::function<void()> _on_clock_out;
    std::function<void(const TransportTick&)> _on_tick;

    uint32_t _tick_index;
    Source   _source;
    int8_t   _quarter_tick_count;
    int8_t   _key_tick_count;
    int8_t   _key_tick_interval;
    uint8_t  _key_tick_interval_idx;
    bool     _is_key;
    bool     _tap;
    bool     _send_clock;
};

};
