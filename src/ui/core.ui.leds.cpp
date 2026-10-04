#include "core.ui.h"
#include "Utility/dsp.h"
#include "config.h"
#include "engine/indicators.h" // pal:: - the canonical palette

// LED rendering runs only in the 62 Hz TIM5 path (render_leds), never the per-sample/per-block
// audio path - so optimize this whole TU for size to reclaim SRAM_EXEC. Perf-irrelevant here.
#pragma GCC optimize("Os")

using namespace spotykach;
using namespace infrasonic;
using namespace daisy;
using namespace daisysp;

static uint32_t clock_source_color(ClockSource::Source source)
{
    switch (source) {
        case ClockSource::internal: return pal::kGreen;
        case ClockSource::ts4: return pal::kPink;
        case ClockSource::midi: return pal::kTurq;
        default: return pal::kWhite;
    }
}
static uint32_t clock_color(const TransportLeds& t, const bool is_key)
{
    uint32_t src_color = clock_source_color(t.source);
    if (is_key) {
        if (t.key_at_quarter) return t.external_sync ? src_color : pal::kWhite;
        return t.key_sub_quarter ? src_color : pal::kWhite;
    }
    else {
        return src_color;
    }
}
static uint32_t count_in_color(const bool is_key)
{
    return is_key ? pal::kWhite : pal::kGreen;
}
static uint32_t mode_color(const spotykach::Mode mode)
{
    switch (mode) {
        case spotykach::Mode::Slice:    return pal::kSlice;
        case spotykach::Mode::Drift:   return pal::kDrift;
        default:                       return pal::kReel;
    };
}
static uint32_t grit_color(const GritMode mode)
{
      switch (mode) {
        case GritMode::Reduce: return pal::kHarshFx;
        default: return pal::kSoftFx;
    }
}

#if SPK_TERMINAL && TERM_USBDIAG
// USB-C bring-up verdict on the panel. Eight LEDs at the top of ring A, read left-to-right along the
// chain: GREEN = check passed, RED = failed. The SPOTY pad is lit dim white so a diagnostic build is
// unmistakable. The normal UI is suppressed entirely - this build is for answering one question.
//
// The bits, in the order a D+ pullup depends on them (app.cpp packs them). 0-3 and 7 are init-time
// facts; 4-6 and 8-11 are re-read every loop, because an init snapshot cannot see later damage.
//   0 clocks configured (System::Init ran ConfigureClocks, not the skip_clocks path)
//   1 HSI48 ready                       2 USB clock source == HSI48
//   3 VDD33_USB ready                   4 transceiver powered (GCCFG.PWRDWN)      [live]
//   5 D+ pullup asserted (DCTL.SDIS clear)                                        [live]
//   6 VBUS sensing off for this core (GCCFG.VBDEN clear)                          [live]
//   7 D+ pad still owned by the USB PHY: alternate function, correct AF           [live]
//   8 D- pad still owned by the USB PHY: alternate function, correct AF           [live]
//   9 the host has issued a USB bus reset at least once (sticky)                  [live]
//  10 the host is sending SOF frames (sticky)                                     [live]
// Bit 9 is weak: the bus reset is transient and the ISR clears GINTSTS before the main loop samples
// it, so it reads red even on a working link. Bit 10 (SOF) is the reliable "the host is talking to us"
// signal. Both red = the host has never seen this device at all.
void CoreUI::_draw_usb_diag()
{
    _hw.leds.Set(Hardware::LED_SPOTY_PAD, pal::kWhite, .2f);
    for (uint8_t i = 0; i < 11; i++) {
        const bool good = (_usb_diag_bits >> i) & 1u;
        _hw.leds.Set(static_cast<uint8_t>(Hardware::LED_RING_A + i), good ? pal::kGreen : pal::kRed, .35f);
    }
}
#endif

// Pixel sink for LEDRing::apply() - maps a ring-local pixel index to the physical LED chain.
// Moved here (the platform) from led.ring.cpp so the ring canvas stays hardware-free.
static void set_led(Hardware& hw, const uint16_t ring, const uint16_t idx, const int color, const float brightness)
{
    uint16_t ledidx = ring;
    static const auto kHalf = 16;
    if (idx <= kHalf) {
        ledidx += kHalf - idx;
    }
    else {
        ledidx += Hardware::kNumLedsPerRing - 1;
        ledidx -= idx - kHalf - 1;
    }
    hw.leds.Set(ledidx, color, brightness);
}

void CoreUI::render_leds()
{
    static auto led_count = 0;
    if (_clock_led_on) {
        led_count ++;
        if (led_count == 11) {
            _clock_led_on = false;
            led_count = 0;
        }
    }

    _hw.leds.Clear();
#if SPK_TERMINAL && TERM_USBDIAG
    _draw_usb_diag();   // diagnostic build: the panel IS the readout; the normal UI is suppressed
#else
    _draw_leds();
#endif
    _hw.leds.Show();
}
void CoreUI::_draw_leds() 
{
    if (_state == State::launching) {
        _draw_launching();
        return;
    }

    if (_engine_owns_display) {
        _blit_display();
        return;
    }

    _breathe_led();

    auto da = _engine.deck_leds(DeckRef::A);
    auto db = _engine.deck_leds(DeckRef::B);
    auto trn = _transport.leds();   // clock indicators are platform state, rendered for every engine
    auto mix = _engine.mix();

    auto mode_color_a = mode_color(da.mode);

    _led[Hardware::LED_GRIT_A].set(_hw);
    _led[Hardware::LED_FLUX_A].set(_hw);
    _hw.leds.Set(Hardware::LED_FADER_A, pal::kWhite, 1.f - mix);

    auto clck_src_color = clock_source_color(trn.source);

    auto cycle_a_color = pal::kWhite;
    if (da.mod_type == ModType::Follow) cycle_a_color = mode_color_a;
    else if (da.mod_synced) cycle_a_color = clck_src_color;
    _hw.leds.Set(Hardware::LED_CYCLE_A, cycle_a_color, _lfo_a);

    auto mode_color_b = mode_color(db.mode);
    _led[Hardware::LED_GRIT_B].set(_hw);
    _led[Hardware::LED_FLUX_B].set(_hw);
    _hw.leds.Set(Hardware::LED_FADER_B, pal::kWhite, mix);

    auto cycle_b_color = pal::kWhite;
    if (db.mod_type == ModType::Follow) cycle_b_color = mode_color_b;
    else if (db.mod_synced) cycle_b_color = clck_src_color;
    _hw.leds.Set(Hardware::LED_CYCLE_B, cycle_b_color, _lfo_b);

    switch (_engine.route()) {
        case Route::DoubleMono: _hw.leds.Set(Hardware::LED_MODE_LEFT, pal::kWhite, .8f); break;
        case Route::Stereo: _hw.leds.Set(Hardware::LED_MODE_CENTER, pal::kWhite, .8f); break;
        case Route::GenerativeStereo: _hw.leds.Set(Hardware::LED_MODE_RIGHT, pal::kWhite, .8f); break;
    }

    if (_clock_led_on || _clock_source_changed) {
        auto color = _clock_source_changed ? clck_src_color : clock_color(trn, _show_key_quarter);
        _hw.leds.Set(Hardware::LED_CLOCK_IN, color, 1.f);
    }

    auto gate_in_a_bright = .25f;
    if (_gate_in_led_cnt[DeckRef::A] > 0) {
        gate_in_a_bright = 1.f;
        _gate_in_led_cnt[DeckRef::A] --;
    }
    _hw.leds.Set(Hardware::LED_GATE_IN_A, mode_color_a, gate_in_a_bright);

    auto gate_in_b_bright = .25f;
    if (_gate_in_led_cnt[DeckRef::B] > 0) {
        gate_in_b_bright = 1.f;
        _gate_in_led_cnt[DeckRef::B] --;
    }
    _hw.leds.Set(Hardware::LED_GATE_IN_B, mode_color_b, gate_in_b_bright);

    // RINGS ////////////////////////////////////////////////
    _display.ring[DeckRef::A].apply([&](uint8_t i, uint32_t hex, float b){ set_led(_hw, Hardware::LED_RING_A, i, hex, b); });
    _display.ring[DeckRef::B].apply([&](uint8_t i, uint32_t hex, float b){ set_led(_hw, Hardware::LED_RING_B, i, hex, b); });
    
    _led[Hardware::LED_PLAY_A].set(_hw);
    _led[Hardware::LED_REV_A].set(_hw);
    _led[Hardware::LED_PLAY_B].set(_hw);
    _led[Hardware::LED_REV_B].set(_hw);

    _led[Hardware::LED_ALT_A].set(_hw);
    _led[Hardware::LED_ALT_B].set(_hw);
}
void CoreUI::_draw_launching()
{
    static float t = 0.0f;
    t += 0.0036f;
    const size_t num = 14;
    const float totalFadeInTime = 0.05f * (num - 1) + 0.1f;

    const uint16_t leds[num] = {
        Hardware::LED_FLUX_A, Hardware::LED_FLUX_B,
        Hardware::LED_GRIT_A, Hardware::LED_GRIT_B,
        Hardware::LED_REV_A, Hardware::LED_REV_B,
        Hardware::LED_PLAY_A, Hardware::LED_PLAY_B,
        Hardware::LED_ALT_A, Hardware::LED_ALT_B,
        Hardware::LED_CYCLE_A, Hardware::LED_CYCLE_B,
        Hardware::LED_GATE_IN_A, Hardware::LED_GATE_IN_B,
    };
    for (size_t i = 0; i < num; i += 2) {
        float progress = (t - i * 0.05f) / 0.25f;
        if (t > totalFadeInTime) progress = 1.0f - (t - totalFadeInTime) / 1.5f;
        
        float brightness = std::fmax(0.f, std::fmin(1.f, progress));
        uint32_t color = pal::kWhite;
        if (leds[i] == Hardware::LED_FLUX_A || leds[i] == Hardware::LED_GRIT_A) {
            if (Config::dynamic().is_loaded()) color = pal::kGreen;
        }

        _hw.leds.Set(leds[i], pal::kWhite, brightness);
        _hw.leds.Set(leds[i + 1], color, brightness);
    };

    if (t < totalFadeInTime) return;
    
    auto phase = .25f * (t - totalFadeInTime) / totalFadeInTime;
    auto brightness = .7f * LUT_Sin_Value_At(phase * kLUTSinSize);
    for (auto ref: { DeckRef::A, DeckRef::B }) {
        _display.ring[ref].set_brightness(brightness);
        _display.ring[ref].set_hex_color(mode_color(_engine.deck_leds(ref).mode));
        _display.ring[ref].set_segment(0.f, 0.999f);
        _display.ring[ref].set_updated();
        auto base = ref == DeckRef::A ? Hardware::LED_RING_A : Hardware::LED_RING_B;
        _display.ring[ref].apply([&](uint8_t i, uint32_t hex, float b){ set_led(_hw, base, i, hex, b); });
    }

    if (t < 2 * totalFadeInTime) return;

    _led_breathe_phase = phase;
    _state = State::init_values;
}

// Blit an own-display engine's DisplayModel straight to hardware (item 3b-2a). The engine filled
// _display in render() (main loop); here in the LED ISR we just push it out - rings via the canvas
// blit, named indicators mapped to their LedId. No granular palette/blink/query interpretation.
void CoreUI::_blit_display()
{
    _display.ring[DeckRef::A].apply([&](uint8_t i, uint32_t hex, float b){ set_led(_hw, Hardware::LED_RING_A, i, hex, b); });
    _display.ring[DeckRef::B].apply([&](uint8_t i, uint32_t hex, float b){ set_led(_hw, Hardware::LED_RING_B, i, hex, b); });

    auto put = [&](Hardware::LedId id, const DisplayModel::Indicator& in){ _hw.leds.Set(id, in.rgb, in.brightness); };
    put(Hardware::LED_PLAY_A, _display.play[DeckRef::A]);       put(Hardware::LED_PLAY_B, _display.play[DeckRef::B]);
    put(Hardware::LED_REV_A,  _display.rev[DeckRef::A]);        put(Hardware::LED_REV_B,  _display.rev[DeckRef::B]);
    put(Hardware::LED_GRIT_A, _display.grit[DeckRef::A]);       put(Hardware::LED_GRIT_B, _display.grit[DeckRef::B]);
    put(Hardware::LED_FLUX_A, _display.flux[DeckRef::A]);       put(Hardware::LED_FLUX_B, _display.flux[DeckRef::B]);
    put(Hardware::LED_GATE_IN_A, _display.gate_in[DeckRef::A]); put(Hardware::LED_GATE_IN_B, _display.gate_in[DeckRef::B]);
    put(Hardware::LED_CYCLE_A, _display.cycle[DeckRef::A]);     put(Hardware::LED_CYCLE_B, _display.cycle[DeckRef::B]);
    put(Hardware::LED_ALT_A,  _display.alt[DeckRef::A]);        put(Hardware::LED_ALT_B,  _display.alt[DeckRef::B]);
    put(Hardware::LED_FADER_A, _display.fader[DeckRef::A]);     put(Hardware::LED_FADER_B, _display.fader[DeckRef::B]);
    put(Hardware::LED_MODE_LEFT,   _display.mode_left);
    put(Hardware::LED_MODE_CENTER, _display.mode_center);
    put(Hardware::LED_MODE_RIGHT,  _display.mode_right);
    put(Hardware::LED_CLOCK_IN,    _display.clock_in);
    put(Hardware::LED_SPOTY_PAD,   _display.spot);
}

// Called from main /////////////////////
void CoreUI::_draw_fx(const DeckRef::Ref ref)
{
    auto fx = _engine.fx_leds(ref);
    auto grit_id = ref == DeckRef::A ? Hardware::LED_GRIT_A : Hardware::LED_GRIT_B;
    auto flux_id = ref == DeckRef::A ? Hardware::LED_FLUX_A : Hardware::LED_FLUX_B;
    _led[grit_id].on(grit_color(fx.grit_mode), fx.grit_on ? 1.f : 0.5f);
    _led[flux_id].on(pal::kFlux, fx.flux_on ? 1.f : 0.5f);
}
void CoreUI::_draw_play(const DeckRef::Ref ref, const bool blink)
{
    auto p = _engine.play_leds(ref);
    auto color = mode_color(p.mode);
    auto playId = ref == DeckRef::A ? Hardware::LED_PLAY_A : Hardware::LED_PLAY_B;
    auto revId = ref == DeckRef::A ? Hardware::LED_REV_A : Hardware::LED_REV_B;
    _led[playId].off();
    _led[revId].off();

    auto& storage = _storage.of(ref);
    if (storage.is_selecting()) {
        if (_touched.test(Alt)) {
            if (storage.can_load()) _led[revId].on(pal::kTape[storage.selected_tape_idx()]);
            _led[playId].on(pal::kWhite);
        }
        else if (storage.can_load()) {
            _led[playId].on(pal::kTape[storage.selected_tape_idx()]);
        }
        return;
    }
    if (p.playing || (p.play_queued && _clock_led_on)) {
        auto ledId = p.reverse ? revId : playId;
        _led[ledId].on(color);
    }
    if (p.armed || p.recording) {
        auto inout = p.armed != p.recording;
        auto rec = !inout;
        static bool led_on[DeckRef::Count] = { false, false };
        if (p.mode == Mode::Slice) {
            led_on[ref] = _clock_led_on;
        }
        else if (blink) {
            led_on[ref] = !led_on[ref];
        }
        if (rec || (led_on[ref] && inout)) {
            auto led_id = p.source == DeckSource::external ? playId : revId;
            _led[led_id].on(pal::kRed, .8f);
        }
    }
    if (_hold_clear[ref].inidcate()) {
        if (_blink_timer.HasPassedMs(50)) {
            _blink_led_on = !_blink_led_on;
            _blink_timer.Restart();
        }
        _led[playId].on(pal::kWhite, _blink_led_on);
    }
}
void  CoreUI::_draw_alt(const DeckRef::Ref ref)
{
    auto ledId = ref == DeckRef::A ? Hardware::LED_ALT_A : Hardware::LED_ALT_B;
    _led[ledId].off();
    if (_alt_blink_count[ref] > 0) {
        if (_blink_timer.HasPassedMs(80)) {
            _alt_blink_count[ref]--;
            _blink_timer.Restart();
        }
        _led[ledId].on(pal::kWhite, _alt_blink_count[ref] % 2);
    }
    // Track recording
    auto alt = _engine.alt_leds(ref);
    auto is_armed = alt.track_armed;
    auto is_recording = alt.track_recording;
    if (is_armed || is_recording) {
        uint32_t color = 0;
        if (is_armed && !is_recording && _clock_led_on) {
            color = count_in_color(_show_key_quarter);
        }
        else if (is_armed && is_recording) {
            color = pal::kRed;
        }
        else if (is_recording && !is_armed && _clock_led_on) {
            color = pal::kRed;
        }
        if (color != 0) {
            _led[ledId].on(color, .8f); 
        }
    }

    if (_touched.test(Alt)) {
        _led[ledId].on(pal::kWhite);
    }
}

// RING // Called from main ////////////////
void CoreUI::_draw_ring(const DeckRef::Ref ref)
{
    auto& ring = _display.ring[ref];
    if (ring.is_updated()) return;

    auto default_color = mode_color(_engine.deck_leds(ref).mode);

    ring.clear();
    ring.set_hex_color(default_color);
    ring.set_brightness(.5f);

    RingGeometry geo{};

    auto storage_state = _storage.of(ref).state();
    if (storage_state != DeckStorage::State::idle) {
        uint32_t color = pal::kWhite;
        switch (storage_state) {
            case DeckStorage::State::selecting: _show_slots(ref); ring.set_updated(); return;
            case DeckStorage::State::saving: color = pal::kWhite; break;
            case DeckStorage::State::loading: color = pal::kTape[_storage.of(ref).selected_tape_idx()]; break;
            case DeckStorage::State::error: /*_error_blink_count[ref] = 5;*/ break;
            default: {}
        }
        ring.set_hex_color(color);
        ring.set_segment(0.f, _storage.of(ref).progress());
        ring.set_updated();
        return;
    }
    // else if (_error_blink_count[ref] > 0) {
    //     _show_error();
    //     _error_blink_count[ref]--;
    //     return;
    // }
    else if (_is_changing(mv(ParamId::KeyInterval)[DeckRef::A]) && ref == DeckRef::A) {
        _show_key_intervals();
    }
    else if (_is_changing(_size_quarters[ref])) {
        _show_size_quarters(ref, default_color);
    }
    else if (_touched.test(ref == DeckRef::A ? GritA : GritB)) {
        auto fx_color = grit_color(_engine.fx_leds(ref).grit_mode);
        _show_value(mv(ParamId::GritIntensity)[ref], ring, fx_color, ValueDisplay::Always);
        _show_value(mv(ParamId::GritMix)[ref], ring, fx_color);
    }
    else if (_touched.test(ref == DeckRef::A ? FluxA : FluxB)) {
        _show_value(mv(ParamId::FluxIntensity)[ref], ring, pal::kFlux, ValueDisplay::Always);
        _show_value(mv(ParamId::FluxMix)[ref], ring, pal::kFlux);
        _show_value(mv(ParamId::FluxFb)[ref], ring, pal::kFlux);
    } 
    else {
        geo = _engine.render_ring(ring, ref, _led_breathe_brightness);
    }

    // Transient overlays for the playing case (matches the old !empty branch): position deviation,
    // the size-change overlay, and the overdub write-head - composited on the engine's segment, in
    // the same order (size overlay first, then the overdub head on top).
    if (geo.playing) {
        // POSITION & SIZE
        _show_value(mv(ParamId::Pos)[ref], ring, pal::kWhite, ValueDisplay::OnMoveDiffOnly);
        if (_is_changing(mv(ParamId::Size)[ref]) && !mv(ParamId::Size)[ref].is_tracking()) {
            if (geo.mode == Mode::Drift) {
                // DRAW SPREAD
                float red_value = std::max(mv(ParamId::Size)[ref].in_value(), mv(ParamId::Size)[ref].value()) * .95f;
                // RED
                ring.set_brightness(.6f);
                ring.set_hex_color(pal::kRed);
                auto seg_start = geo.start - red_value * .5f;
                ring.set_segment(seg_start, seg_start + red_value, true);
                if (mv(ParamId::Size)[ref].in_value() > mv(ParamId::Size)[ref].value()) {
                    ring.add_point(seg_start, .8f, true);
                    ring.add_point(seg_start + red_value, .8f, true);
                }
                // WHITE
                float white_value = std::min(mv(ParamId::Size)[ref].in_value(), mv(ParamId::Size)[ref].value()) * .95f;
                ring.set_brightness(.8f);
                ring.set_hex_color(pal::kWhite);
                seg_start = geo.start - white_value * .5f;
                ring.set_segment(seg_start, seg_start + white_value, true);
            }
            else {
                ring.set_brightness(.6f);
                ring.set_hex_color(pal::kRed);
                auto current_val = geo.start + geo.size;
                auto new_val = geo.start + mv(ParamId::Size)[ref].in_value();
                auto start = std::min(current_val, new_val);
                auto end = std::max(current_val, new_val);
                while (end >= 1.f) end -= 1.f; //WORKAROUND. Needs to be fixed in LEDRing::set_segment().
                ring.set_segment(start, end);
                ring.add_point(geo.start + mv(ParamId::Size)[ref].in_value(), .95f);
            }
        }

        if (geo.overdubbing) {
            ring.set_point_hex_color(pal::kRed);
            ring.add_point(geo.overdub_head, .9f, true, false);
        }
    }
    
    switch (ref) {
        case DeckRef::A: {
            _show_value(mv(ParamId::ModAmp)[DeckRef::A], ring);
            _show_value(mv(ParamId::ModSpeed)[DeckRef::A], ring);
            _show_value(mv(ParamId::ClickMix)[DeckRef::A], ring);
            _show_value(mv(ParamId::Tempo)[DeckRef::A], ring);
        }
        break;
        case DeckRef::B: {
            _show_value(mv(ParamId::ModAmp)[DeckRef::B], ring);
            _show_value(mv(ParamId::ModSpeed)[DeckRef::B], ring);
            _show_value(mv(ParamId::PanSpeed)[DeckRef::A], ring);
            _show_value(mv(ParamId::PanRange)[DeckRef::A], ring);
        }
        break;
        default:break;
    }
    _show_value(mv(ParamId::Mix)[ref], ring);
    _show_value(mv(ParamId::Feedback)[ref], ring);
    _show_value(mv(ParamId::Win)[ref], ring);
    _show_value(mv(ParamId::Env)[ref], ring);
    _show_value(mv(ParamId::EnvSize)[ref], ring);
    _show_pitch(ref);

    if (_is_changing(mv(ParamId::PolySlice)[ref])) 
    {
        ring.clear();
        auto start = 0.f;
        if (!mv(ParamId::PolySlice)[ref].is_tracking()) {
            start = mv(ParamId::PolySlice)[ref].in_value() < .5f ? 0.f : .5f;    
            ring.set_brightness(.6f);
            ring.set_hex_color(pal::kRed);
            ring.set_segment(start, start + .495f);
        }
        ring.set_brightness(.6f);
        ring.set_hex_color(pal::kWhite);
        if (mv(ParamId::PolySlice)[ref].value() < .5f) {
            ring.set_segment(.2f, .3f, true);
        }
        else {
            ring.set_segment(.5f, .535f, true);
            ring.set_segment(.65f, .7f, true);
            ring.set_segment(.825f, .855f, true);
            ring.set_segment(.97f, 1.f, true);
        }
    }
    
    ring.set_updated();
}
void CoreUI::_show_value(const MValue& val, LEDRing& ring, const uint32_t def_color, const ValueDisplay display)
{
    if (display != ValueDisplay::Always && !_is_changing(val)) return;

    // VALUE
    if (display != ValueDisplay::OnMoveDiffOnly) {
        ring.clear();
        ring.set_brightness(.6f);
        ring.set_hex_color(def_color);
        ring.set_segment(0.f, std::clamp(val.value(), 0.f, .999f));
    }

    // DEVIATION
    if (val.is_tracking()) return;

    ring.set_brightness(_led_breathe_brightness * .6f); 
    ring.set_hex_color(pal::kRed);
    auto start = std::min(val.value(), val.in_value());
    auto end = std::max(val.value(), val.in_value());
    ring.set_segment(start, end);
    ring.add_point(val.in_value(), .95f);
}
void CoreUI::_show_pitch(const DeckRef::Ref ref)
{
    if (!_is_changing(mv(ParamId::Speed)[ref])) return;
    auto& ring = _display.ring[ref];

    ring.set_hex_color(pal::kWhite);
    ring.set_segment(0.f, 0.998f);
    ring.fill_brightness(0.2f);

    if (_touched.test(Alt)) {
        ring.set_brightness(.5f);
        
        auto steps = kSpeedSteps.size() - 1;
        auto norm_step = std::round(steps * mv(ParamId::Speed)[ref].value()) / steps;
        auto spread = 0.05f;
        if (norm_step == 0.f) ring.set_segment(0.f, 2.f * spread, true);
        else if (norm_step == 1.f) ring.set_segment(1.f - 2.f * spread, 1.f, true);
        else ring.set_segment(norm_step - spread, norm_step + spread, true);
    }
    else {
        ring.set_point_hex_color(pal::kWhite);
        ring.add_point(mv(ParamId::Speed)[ref].value(), 1.0f, true);
    }
    _show_value(mv(ParamId::Speed)[ref], ring, pal::kWhite, ValueDisplay::OnMoveDiffOnly);
}
void CoreUI::_show_slots(const DeckRef::Ref ref)
{   
    auto& s = _storage.of(ref);
    auto idx = 3;
    for (uint8_t i = 0; i < kStorageSlotCount; i++) {
        auto slot = s.slot_at(i);
        auto bright = .8f;
        if (i == s.selected_slot_idx()) bright = _led_breathe_brightness * .8f;
        else bright = slot.is_empty ? .3f : .85f;
        _display.ring[ref].set_point_hex_color(i == s.selected_slot_idx() ? pal::kWhite : pal::kTape[s.selected_tape_idx()]);
        for (uint8_t i = 0; i < 2; i++) _display.ring[ref].set_point(idx + i, bright);    
        idx += 5; //2 segment + 3 gap
    }
}
void CoreUI::_show_key_intervals() 
{
    if (!mv(ParamId::KeyInterval)[DeckRef::A].is_tracking()) {
        _display.ring[DeckRef::A].set_brightness(_led_breathe_brightness * .6f); 
        _display.ring[DeckRef::A].set_hex_color(pal::kRed);
        auto start = std::min(mv(ParamId::KeyInterval)[DeckRef::A].value(), mv(ParamId::KeyInterval)[DeckRef::A].in_value());
        auto end = std::max(mv(ParamId::KeyInterval)[DeckRef::A].value(), mv(ParamId::KeyInterval)[DeckRef::A].in_value());
        _display.ring[DeckRef::A].set_segment(start, end);
    }

    auto trn = _transport.leds();
    auto interval = trn.key_interval;
    uint8_t step;
    uint8_t steps;
    using KI = kKeyInterval;
    switch (interval) {
        case KI::k1_16: step = 1; steps = 32;   break;
        case KI::k1_4:  step = 1; steps = 1;    break;
        default:        step = 2; steps = interval / 4;
    }
    uint32_t color;
    for (uint8_t i = 0; i < steps; i++) {
        if (i == 0) color = trn.key_sub_quarter ? clock_source_color(trn.source) : pal::kWhite;
        else color = clock_source_color(trn.source);
        _display.ring[DeckRef::A].set_point_hex_color(color);
        _display.ring[DeckRef::A].set_point(i * step + 4, i % 4 && interval != KI::k1_16 ? .25f : .7f);
    }
}
void CoreUI::_show_size_quarters(const DeckRef::Ref ref, const uint32_t color)
{
    auto steps = 1 + round(_size_quarters[ref].value() * 15);
    for (uint8_t i = 0; i < steps; i++) {
        _display.ring[ref].set_point_hex_color(i % 4 ? color : pal::kWhite);
        _display.ring[ref].set_point(i * 2 + 4, .7f);
    }
}
void CoreUI::_show_error(const DeckRef::Ref ref)
{
    if (_blink_led_on) {
        _display.ring[ref].set_hex_color(pal::kRed);
        _display.ring[ref].set_brightness(.6f);
        _display.ring[ref].set_segment(0.f, 0.98f);
    }
}

void CoreUI::_breathe_led()
{
    _led_breathe_phase += .0005f; 
    if (_led_breathe_phase >= 1.f) {
        _led_breathe_phase = 0.f;
    }
    _led_breathe_brightness = .7f + (LUT_Sin_Value_At(_led_breathe_phase * kLUTSinSize) + 1.f) * .15f;
}

void CoreUI::_show_empty(const DeckRef::Ref ref)
{
    _alt_blink_count[ref] = 8;
}

void CoreUI::_show_gate_in(const DeckRef::Ref ref)
{
    _gate_in_led_cnt[ref] = 10;
}
