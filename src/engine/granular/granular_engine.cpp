// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#include "engine/granular/granular_engine.h"
#include "engine/granular/mode.h"
#include "engine/granular/event.h"
#include "config.h"

using namespace spotykach;

// Pre-seeds the param cache from the DSP's initial state so the platform seeds its MValue pickup
// via param() instead of reaching into Core for _init_values (item 3a-3b). Only the genuinely
// engine-derived seeds; the platform keeps its UI-default literals for the rest. Once at boot, so
// size-optimised to stay off the SRAM_EXEC ceiling.
__attribute__((optimize("Os")))
void GranularEngine::init(const EngineContext& ctx)
{
    _core.init(ctx);
    _speed_map.init();
    for (auto ref : { DeckRef::A, DeckRef::B }) {
        auto& deck = _core.deck(ref);
        auto& fx = deck.fx();
        _param_cache[static_cast<size_t>(ParamId::Pos)][ref]           = deck.norm_start();
        _param_cache[static_cast<size_t>(ParamId::GritMix)][ref]       = fx.grit_mix();
        _param_cache[static_cast<size_t>(ParamId::GritIntensity)][ref] = fx.grit_intensity();
        _param_cache[static_cast<size_t>(ParamId::FluxMix)][ref]       = fx.flux_mix();
        _param_cache[static_cast<size_t>(ParamId::FluxIntensity)][ref] = fx.flux_intensity();
        _param_cache[static_cast<size_t>(ParamId::FluxFb)][ref]        = fx.flux_fb();
        // Preserve granular's prior MODFREQ default (was a shared 0.3 literal in core.ui; now that the
        // cycle knob is engine-seeded, granular must carry its own default so its behaviour is unchanged).
        _param_cache[static_cast<size_t>(ParamId::ModSpeed)][ref]      = 0.3f;
        // Likewise SIZE: was a shared 1.0 literal, now engine-seeded - carry granular's own 1.0 default.
        _param_cache[static_cast<size_t>(ParamId::Size)][ref]          = 1.0f;
    }
}

// Mode-dependent dispatch ported from core.ui.cpp's apply pass (the granular "meaning").
// The platform decides WHICH param a control drives; the engine decides what that param
// does given the current deck mode. The deck arg is ignored for global params.
void GranularEngine::set_param(const ParamId id, const DeckRef::Ref ref, const float v)
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
        case ParamId::ClickMix:      _core.set_click_mix(v); break;
        case ParamId::PanSpeed:      _core.panner().set_speed(v); break;
        case ParamId::PanRange:      _core.panner().set_range(v); break;
        case ParamId::Crossfade:     _core.set_mix(v); break;

        // Transport concerns: the platform writes Tempo + KeyInterval straight to the Transport now,
        // so the engine ignores them (kept as explicit no-ops to stay -Wswitch-clean).
        case ParamId::Tempo:         break;
        case ParamId::KeyInterval:   break;

        case ParamId::Aux:           break; // Alt+PITCH selector (granular doesn't use CapAux)

        case ParamId::Count:         break;
    }
}

float GranularEngine::param(const ParamId id, const DeckRef::Ref ref) const
{
    return _param_cache[static_cast<size_t>(id)][_safe_ref(ref)];
}

// Categorical config (item 3a-0), ported verbatim from core.ui.cpp's _process_switches. The
// platform passes a selector int read from the panel switch bits; the engine maps it to its enums
// and owns the side effects the platform should not know about (panner inference on mode change,
// the deck-specific LFO palette). Cold path - size-optimised so it costs little in this -O2 TU.
__attribute__((optimize("Os")))
bool GranularEngine::set_config(const ConfigId id, const DeckRef::Ref ref, const int v)
{
    const auto deck_ref = _safe_ref(ref);
    switch (id) {
        case ConfigId::Route:
            _core.set_route(v == 2 ? Route::GenerativeStereo
                          : v == 1 ? Route::DoubleMono
                                   : Route::Stereo);
            return false;
        case ConfigId::ModType:
            _core.mod(deck_ref).set_type(v ? Modulator::Type::Follow : Modulator::Type::LFO);
            return false;
        case ConfigId::LfoShape: {
            // Per-deck LFO palette, faithful to the old UI: deck A square/random, deck B saw/sine.
            const LFO::Type t = deck_ref == DeckRef::A ? (v ? LFO::Type::square : LFO::Type::random)
                                                    : (v ? LFO::Type::saw    : LFO::Type::sine);
            _core.mod(deck_ref).set_lfo_type(t);
            return false;
        }
        case ConfigId::Mode: {
            auto& deck = _core.deck(deck_ref);
            const Mode nm = v == 2 ? Mode::Drift : v == 1 ? Mode::Reel : Mode::Slice;
            if (nm == deck.mode()) return false;
            deck.set_mode(nm);
            _core.infer_panner_mode();
            return true;
        }
        case ConfigId::StartModOn: _core.deck(deck_ref).set_start_mod_on(v != 0); return false;
        case ConfigId::SizeModOn:  _core.deck(deck_ref).set_size_mod_on(v != 0);  return false;
        case ConfigId::Count: break;
    }
    return false;
}

float GranularEngine::tempo_to_fit(const DeckRef::Ref ref, const float fraction)
{
    return _core.deck(_safe_ref(ref)).tempo_to_fit(fraction);
}

GritReseed GranularEngine::toggle_grit_mode(const DeckRef::Ref ref)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    fx.switch_grit_mode();
    return { fx.grit_intensity(), fx.grit_mix() };
}

DeckLayout GranularEngine::deck_layout(const DeckRef::Ref ref)
{
    switch (_core.deck(_safe_ref(ref)).mode()) {
        case Mode::Reel:  return DeckLayout::single;
        case Mode::Slice: return DeckLayout::slice;
        case Mode::Drift: return DeckLayout::chord;
        case Mode::None:  return DeckLayout::none;
    }
    return DeckLayout::single;
}

bool GranularEngine::size_sets_tempo(const DeckRef::Ref ref)
{
    auto& deck = _core.deck(_safe_ref(ref));
    return deck.mode() == Mode::Slice && !deck.is_empty();
}

Capabilities GranularEngine::capabilities() const
{
    return CapRecording | CapTapeStorage | CapStepSequencer
         | CapLaunchQuant | CapTransport | CapDualDeck;
}

DeckRef::Ref GranularEngine::handle_midi_note(const uint8_t channel, const uint8_t note)
{
    auto& c = Config::dynamic();
    auto ref = DeckRef::Count;
    if (channel == c.midi_channel_a()) ref = DeckRef::A;
    else if (channel == c.midi_channel_b()) ref = DeckRef::B;
    if (ref == DeckRef::Count) return DeckRef::Count;

    auto e = make_event();
    e.discont = true; // MIDI note: discontinuous (not trailed by a V/Oct change)
    e.p3 = _speed_map.bipolar_pitch2speed(static_cast<float>(note) - 60.f);
    e.p3_on = true;
    _core.deck(ref).trigger(&e);
    return ref;
}

void GranularEngine::stop_if_generating(const DeckRef::Ref ref)
{
    auto& deck = _core.deck(_safe_ref(ref));
    if (deck.is_generating()) deck.stop();
}

void GranularEngine::clear_buffer(const DeckRef::Ref ref)
{
    _core.deck(_safe_ref(ref)).buffer().clear();
}

void GranularEngine::on_record_pad(const DeckRef::Ref ref, const bool reverse)
{
    const auto src = reverse ? Deck::Source::internal : Deck::Source::external;
    _core.set_source(src, _safe_ref(ref));
    _core.deck(_safe_ref(ref)).toggle_recording();
}

bool GranularEngine::on_play_pad(const DeckRef::Ref ref, const bool reverse)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.disarm();
    const bool empty = deck.is_empty();
    if (!deck.is_overdubbing() && (!deck.is_playing() || deck.is_reverse() == reverse)) {
        deck.toggle_play();
    }
    deck.set_reverse(reverse);
    return empty;
}

void GranularEngine::on_seq_toggle_arm(const DeckRef::Ref ref)
{
    auto& t = _core.deck(_safe_ref(ref)).track();
    if (t.is_armed()) t.disarm();
    else              t.arm(!_core.is_key_sub_quarter());
}

void GranularEngine::on_seq_trigger(const DeckRef::Ref ref)
{
    auto e = make_event();
    _core.deck(_safe_ref(ref)).trigger(&e);
}

void GranularEngine::clear_sequence(const DeckRef::Ref ref)
{
    _core.deck(_safe_ref(ref)).clear_sequence();
}

void GranularEngine::disarm_track(const DeckRef::Ref ref)
{
    auto& t = _core.deck(_safe_ref(ref)).track();
    if (t.is_armed()) t.disarm();
}

void GranularEngine::set_mod_speed(const DeckRef::Ref ref, const float value, const bool sync)
{
    _param_cache[static_cast<size_t>(ParamId::ModSpeed)][_safe_ref(ref)] = value;
    _core.mod(_safe_ref(ref)).set_speed_norm(value, sync);
}

void GranularEngine::cv_mix(const DeckRef::Ref ref, const float value)
{
    _core.deck(_safe_ref(ref)).inout_mix_mod_in(value);
}

void GranularEngine::cv_size_pos(const DeckRef::Ref ref, const float value)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.size_mod_in(value);
    deck.start_mod_in(value);
}

void GranularEngine::cv_voct(const DeckRef::Ref ref, const float value)
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

void GranularEngine::on_gate_trigger(const DeckRef::Ref ref)
{
    const auto deck_ref = _safe_ref(ref);
    auto e = make_event();
    e.p3 = _voct_speed[deck_ref];
    e.p3_on = true;
    _core.deck(deck_ref).trigger(&e);
}

bool GranularEngine::gate_out_triggered(const DeckRef::Ref ref)
{
    return _core.deck(_safe_ref(ref)).voxs().read_reset_is_triggered();
}

bool GranularEngine::audio_is_empty(const DeckRef::Ref ref)
{
    return _core.deck(_safe_ref(ref)).is_empty();
}

uint8_t* GranularEngine::audio_data(const DeckRef::Ref ref)
{
    return reinterpret_cast<uint8_t*>(_core.deck(_safe_ref(ref)).buffer().raw());
}

size_t GranularEngine::audio_recorded_bytes(const DeckRef::Ref ref)
{
    return _core.deck(_safe_ref(ref)).buffer().rec_size() * sizeof(Buffer::Frame);
}

size_t GranularEngine::audio_capacity_bytes(const DeckRef::Ref ref)
{
    return _core.deck(_safe_ref(ref)).buffer().size() * sizeof(Buffer::Frame);
}

void GranularEngine::audio_apply_loaded(const DeckRef::Ref ref, const size_t frames)
{
    auto& deck = _core.deck(_safe_ref(ref));
    deck.buffer().set_rec_size(frames);
    deck.apply_start_size();
}

FxLeds GranularEngine::fx_leds(const DeckRef::Ref ref)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    return { fx.grit_mode(), fx.is_grit_on(), fx.is_flux_on() };
}

PlayLeds GranularEngine::play_leds(const DeckRef::Ref ref)
{
    const auto r = _safe_ref(ref);
    auto& deck = _core.deck(r);
    return { deck.mode(), deck.is_playing(), deck.is_play_queued(), deck.is_reverse(),
             deck.is_armed(), deck.is_recording(), _core.source(r) };
}

AltLeds GranularEngine::alt_leds(const DeckRef::Ref ref)
{
    auto& track = _core.deck(_safe_ref(ref)).track();
    return { track.is_armed(), track.is_recording() };
}

// transport_leds() was removed from IEngine: the platform now reads clock indicator state directly
// from the Transport (_transport.leds()) and composites it for every engine.

DeckLeds GranularEngine::deck_leds(const DeckRef::Ref ref)
{
    const auto r = _safe_ref(ref);
    return { _core.deck(r).mode(), _core.mod(r).type(), _core.mod(r).is_synced() };
}

float GranularEngine::mix() const   { return _core.mix(); }
Route GranularEngine::route() const { return _core.route(); }

// Steady-state ring draw, ported verbatim from core.ui.leds.cpp _draw_ring's three steady-state
// arms. The caller (platform) has cleared the ring + set the default mode color + .5 brightness;
// the empty/recording/playing branches draw on that baseline. Os-tagged: runs in the main-loop UI
// tick (not audio RT), and moving it out of the -Os leds.cpp into this -O2 TU must not grow code.
__attribute__((optimize("Os")))
RingGeometry GranularEngine::render_ring(LEDRing& ring, const DeckRef::Ref ref_in, const float breathe_brightness)
{
    auto& deck = _core.deck(_safe_ref(ref_in));
    const auto mode = deck.mode();

    if (deck.is_empty() && !deck.is_armed()) {
        ring.set_brightness(breathe_brightness * .5f);
        ring.set_segment(0.f, 0.999f);
        return {};
    }
    if (deck.is_recording() && !deck.is_overdubbing()) {
        auto size = deck.buffer().norm_rec_size();
        ring.set_segment(0.f, size);
        ring.set_point_hex_color(0xff0000); // kRed
        ring.add_point(size, .9f, true, false);
        return {};
    }
    if (!deck.is_empty()) {
        const float start = deck.norm_start();
        float size;
        float seg_start = start;
        if (mode == Mode::Drift) {
            size = deck.voxs().win_spread() * .95f;
            seg_start = start - size * .5f;
        }
        else {
            size = deck.norm_size(true);
        }
        ring.set_segment(seg_start, seg_start + size);
        ring.set_point_hex_color(0xffffff); // kWhite
        if (deck.is_generating()) {
            for (auto i = 0; i < Generator::kVoxCount; i++) {
                if (deck.envelope_at(i) > 0) {
                    ring.add_point(deck.norm_playhead_at(i), deck.envelope_at(i));
                }
            }
        }
        RingGeometry geo;
        geo.playing = true;
        geo.start = start;
        geo.size = size;
        geo.mode = mode;
        geo.overdubbing = deck.is_overdubbing();
        if (geo.overdubbing) {
            auto& buff = deck.buffer();
            geo.overdub_head = static_cast<float>(buff.write_head()) / buff.rec_size();
        }
        return geo;
    }
    return {}; // empty-but-armed: nothing drawn (matches the old else-if chain)
}

void GranularEngine::set_fx(const DeckRef::Ref ref, const FxKind kind, const bool on)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    if (kind == FxKind::Flux) fx.set_flux_on(on);
    else                      fx.set_grit_on(on);
}

void GranularEngine::toggle_fx_lock(const DeckRef::Ref ref, const FxKind kind)
{
    auto& fx = _core.deck(_safe_ref(ref)).fx();
    if (kind == FxKind::Flux) fx.toggle_flux_lock();
    else                      fx.toggle_grit_lock();
}

void GranularEngine::handle_midi_transport(const bool start)
{
    auto& c = Config::dynamic();
    if (start) {
        // The platform resets the Transport on a MIDI start before calling this; the engine only
        // decides what start/stop means for its decks.
        if (c.midi_play_stop_a() && !_core.deck(DeckRef::A).is_empty()) _core.deck(DeckRef::A).play();
        if (c.midi_play_stop_b() && !_core.deck(DeckRef::B).is_empty()) _core.deck(DeckRef::B).play();
    }
    else {
        if (c.midi_play_stop_a()) _core.deck(DeckRef::A).stop();
        if (c.midi_play_stop_b()) _core.deck(DeckRef::B).stop();
    }
}
