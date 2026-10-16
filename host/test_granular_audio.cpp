// Host test for the granular engine's AUDIO path - loading, playing, and recording real samples.
//
// Why this exists alongside test_engine_params.cpp: that test drives the *parameter surface* (every
// ParamId round-trips across every mode, output stays finite, capabilities are right). It never puts
// audio through the engine, so a break anywhere in buffer -> deck -> generator -> output would leave
// it green. Granular is the default `make` target and the tree every other looper was cloned from,
// and until now nothing off-target asserted that it makes a sound at all.
//
// What it pins:
//   1. **Load -> play produces audio that tracks the buffer.** A known DC level goes in through the
//      storage port (audio_data + audio_apply_loaded, i.e. a simulated SD load, no card involved) and
//      the output level must scale with it. Halve the input, the output roughly halves; zero it and
//      the engine goes quiet. That is the whole read path in three assertions.
//   2. **An empty deck is silent** and reports itself empty - the state the platform lights the Play
//      LED from.
//   3. **Record captures the input.** Arm, feed a signal (recording is level-triggered at ~-40 dB),
//      stop, then play back and assert audio comes out. This is the path a user touches first.
//   4. **Every mode renders finite audio** - Reel, Slice and Drift take different branches through
//      Generator/Vox, and Drift in particular runs the grain scheduler.
//
// Reel is used for the level assertions on purpose: it is the unsynced mode, so playback does not
// depend on transport ticks and the test needs no clock to be driven.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <utility>
#include <vector>

#include "engine/granular/granular_engine.h"
#include "engine/granular/buffer.h"
#include "engine/led.ring.h"
#include "config.h"
#include "transport/transport.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

// Storage that mirrors how the platform allocates: zeroed before the constructor runs.
//
// This is NOT a style preference, it is what keeps this test measuring the DSP. The platform holds
// the engine and the transport in `static AppImpl impl;` (app.cpp) - file scope, hence zeroed
// storage, hence every member the constructor does not assign is a defined zero. The granular tree
// depends on that and never had to state it: SoftSwitch::_kof/_iterator/_on, Buffer::_did_cut and
// several of Transport's tick counters are declared without initialisers and are not written by any
// constructor.
//
// Build those objects as plain stack locals and the same members are INDETERMINATE, so reading one
// is undefined behaviour. clang at -O2 acts on that and this test rendered `inf` from the first
// block on macOS, while gcc on Linux (and CI) stayed green - the classic split, since UB permits the
// failure rather than compelling it. Zeroing the bytes of a `char` array and placement-newing into
// it is what removes the indeterminacy: pre-filling the *stack* does not, because the compiler
// reasons about whether a member was written, not about what the memory happens to hold.
//
// UBSan (`-fsanitize=bool`) reports the three cases where the leftover byte was outside {0,1}; the
// float members it cannot see are the ones that produced the `inf`. Fixing this here rather than in
// the engine keeps the stock spotykach tree untouched; the members are still uninitialised, they are
// simply no longer read from indeterminate storage.
template <typename T>
class ZeroInit {
public:
    ZeroInit() { std::memset(_storage, 0, sizeof(_storage)); _obj = new (_storage) T(); }
    ~ZeroInit() { _obj->~T(); }
    ZeroInit(const ZeroInit&) = delete;
    ZeroInit& operator=(const ZeroInit&) = delete;
    T& get() { return *_obj; }

private:
    alignas(T) unsigned char _storage[sizeof(T)];
    T* _obj;
};

int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("  FAIL: %s\n", m); g_failures++; } }

// ConfigId::Mode carries the PHYSICAL SWITCH POSITION, not the Mode enum value - granular maps it
// `v == 2 ? Drift : v == 1 ? Reel : Slice` (granular_engine.cpp), so the switch order is
// Slice / Reel / Drift while the enum order is Reel=0, Slice=1, Drift=2. Passing
// `static_cast<int>(Mode::Reel)` selects SLICE. Named here so the test cannot make that mistake
// silently: it did on the first run, and the symptom was an engine that looked completely dead
// (Slice queues playback until a key/bar boundary, which a 200-block render never reaches).
constexpr int kSwitchSlice = 0;
constexpr int kSwitchReel  = 1;
constexpr int kSwitchDrift = 2;

struct Stats { float rms = 0.f, peak = 0.f; bool finite = true; };

// Render `blocks` blocks with the given constant input level, summarising the left output.
//
// transport.tick() per block is NOT optional here, and is the reason this test looked like a silent
// engine on the first run: `Deck::toggle_play()` QUEUES playback (`_is_play_queued`) and the queue is
// drained from `Deck::tick()`, which only runs off a transport tick. With a stub ITransport that
// never fires, the deck sits at playing=0/queued=1 forever and renders silence. The host plays the
// platform's role - app.cpp drives the real Transport once per audio block - so the test must too.
Stats render(Transport& transport, GranularEngine& e, int blocks, float in_level = 0.f) {
    float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
    for (size_t i = 0; i < host::kBlock; i++) { il[i] = in_level; ir[i] = in_level; }
    const float* in[2] = { il, ir }; float* out[2] = { ol, orr };
    Stats s; double acc = 0; size_t n = 0;
    for (int b = 0; b < blocks; b++) {
        transport.tick(false);
        e.prepare();                       // the platform main loop calls this between blocks
        e.process(in, out, host::kBlock);
        for (size_t i = 0; i < host::kBlock; i++) {
            for (float v : { ol[i], orr[i] }) if (!std::isfinite(v)) s.finite = false;
            const float a = std::fabs(ol[i]);
            if (a > s.peak) s.peak = a;
            acc += static_cast<double>(ol[i]) * ol[i]; n++;
        }
    }
    s.rms = n ? static_cast<float>(std::sqrt(acc / n)) : 0.f;
    return s;
}

// Write a constant level into both decks' loop buffers through the storage port - the same entry
// point Storage uses after reading a WAV off the card, so this exercises the real load path.
void load_buffer(GranularEngine& e, float v, size_t frames) {
    for (auto d : { DeckRef::A, DeckRef::B }) {
        auto* raw = reinterpret_cast<Buffer::Frame*>(e.audio_data(d));
        const size_t cap = e.audio_capacity_bytes(d) / sizeof(Buffer::Frame);
        const size_t n = frames < cap ? frames : cap;
        for (size_t i = 0; i < n; i++) { raw[i].l = v; raw[i].r = v; }
        e.audio_apply_loaded(d, n);
    }
}

// A 0..1 ramp across the buffer, so position becomes audible: a slice taken at norm position p
// plays back at roughly level p. That is what lets the start-offset walk be measured as a level.
void load_ramp(GranularEngine& e, size_t frames) {
    for (auto d : { DeckRef::A, DeckRef::B }) {
        auto* raw = reinterpret_cast<Buffer::Frame*>(e.audio_data(d));
        const size_t cap = e.audio_capacity_bytes(d) / sizeof(Buffer::Frame);
        const size_t n = frames < cap ? frames : cap;
        for (size_t i = 0; i < n; i++) {
            const float v = static_cast<float>(i) / static_cast<float>(n);
            raw[i].l = v; raw[i].r = v;
        }
        e.audio_apply_loaded(d, n);
    }
}

// Fully wet, whole-buffer playback: what the level assertions need to be readable.
void setup_wet(GranularEngine& e) {
    for (auto d : { DeckRef::A, DeckRef::B }) {
        e.set_config(ConfigId::Mode, d, kSwitchReel);   // unsynced: plays without waiting for a bar
        e.set_param(ParamId::Mix,   d, 1.0f);   // playback only, no input bleed
        e.set_param(ParamId::Pos,   d, 0.0f);   // from the start of the loop
        e.set_param(ParamId::Size,  d, 1.0f);   // the whole loop
        e.set_param(ParamId::Speed, d, 0.5f);   // unity
        e.set_param(ParamId::Env,   d, 0.0f);   // no loop envelope - keep the level flat
    }
}

// One engine + transport, isolated from the shared one in main(). Scoped, so the 48 MB arena goes too.
struct Rig {
    host::HostArena arena; host::TimeSource time;
    ZeroInit<Transport> transport_store;
    ZeroInit<GranularEngine> engine_store;
    Transport& transport = transport_store.get();
    GranularEngine& e = engine_store.get();
    Rig(const float bpm = 120.f) {
        EngineContext ctx = host::make_context(arena, time);
        transport.init(host::kSampleRate, static_cast<float>(host::kBlock), &time);
        transport.set_tempo_norm(tempo_abs_to_norm(bpm));
        ctx.transport = &transport;
        e.init(ctx);
    }
    // Render `n` samples of `in(i)` (both channels), returning the left output.
    template <typename F> std::vector<float> run(const size_t n, F in) {
        std::vector<float> out_all;
        float il[host::kBlock], ir[host::kBlock], ol[host::kBlock], orr[host::kBlock];
        const float* ins[2] = { il, ir }; float* outs[2] = { ol, orr };
        for (size_t done = 0; done < n; done += host::kBlock) {
            for (size_t i = 0; i < host::kBlock; i++) il[i] = ir[i] = in(done + i);
            transport.tick(false);
            e.prepare();
            e.process(ins, outs, host::kBlock);
            out_all.insert(out_all.end(), ol, ol + host::kBlock);
        }
        return out_all;
    }
    std::vector<float> run_silence(const size_t n) { return run(n, [](size_t) { return 0.f; }); }
};

float rms_of(const std::vector<float>& v, size_t from) {
    double acc = 0; size_t n = 0;
    for (size_t i = from; i < v.size(); i++) { acc += double(v[i]) * v[i]; n++; }
    return n ? static_cast<float>(std::sqrt(acc / n)) : 0.f;
}

// Sample index of the loudest output after `from` - the echo of an impulse.
size_t peak_index(const std::vector<float>& v, size_t from) {
    size_t best = from;
    for (size_t i = from; i < v.size(); i++) if (std::fabs(v[i]) > std::fabs(v[best])) best = i;
    return best;
}

} // namespace

int main() {
    std::printf("=== granular audio path (load / play / record) ===\n");

    host::TimeSource time; host::HostArena arena;
    EngineContext ctx = host::make_context(arena, time);

    // A real platform Transport, injected BEFORE init() so granular Core can subscribe to its ticks.
    ZeroInit<Transport> transport_store;
    Transport& transport = transport_store.get();
    transport.init(host::kSampleRate, static_cast<float>(host::kBlock), &time);
    ctx.transport = &transport;

    ZeroInit<GranularEngine> engine_store;
    GranularEngine& e = engine_store.get();
    e.init(ctx);
    setup_wet(e);

    // --- an empty deck is silent, and says so -------------------------------------------------
    {
        check(e.audio_is_empty(DeckRef::A), "a fresh deck reports empty");
        const Stats s = render(transport, e, 60);
        check(s.finite, "an empty engine renders finite output");
        check(s.rms < 1e-5f, "an empty engine is silent");
    }

    // --- load -> play, and the output tracks the buffer ----------------------------------------
    Stats hi, lo, zero;
    {
        load_buffer(e, 0.5f, 48000);          // 1 s of DC at 0.5
        check(!e.audio_is_empty(DeckRef::A), "deck reports non-empty after a load");
        e.on_play_pad(DeckRef::A, false);
        e.on_play_pad(DeckRef::B, false);
        render(transport, e, 100);                        // settle (fades, smoothers)
        hi = render(transport, e, 200);

        load_buffer(e, 0.125f, 48000);        // a quarter of the level
        render(transport, e, 100);
        lo = render(transport, e, 200);

        load_buffer(e, 0.0f, 48000);
        render(transport, e, 100);
        zero = render(transport, e, 200);

        std::printf("  buffer=0.5   -> rms=%.5f peak=%.5f\n", hi.rms, hi.peak);
        std::printf("  buffer=0.125 -> rms=%.5f peak=%.5f\n", lo.rms, lo.peak);
        std::printf("  buffer=0.0   -> rms=%.5f peak=%.5f\n", zero.rms, zero.peak);

        check(hi.finite && lo.finite && zero.finite, "playback output is finite");
        check(hi.rms > 1e-3f, "a loaded, playing deck produces audio");
        check(lo.rms < hi.rms, "a quieter buffer plays back quieter");
        check(zero.rms < 1e-5f, "a silent buffer plays back silent");
        // The engine reads the buffer rather than synthesising: a 4x quieter buffer should be
        // roughly 4x quieter out. Loose bounds - windowing and fades move the exact ratio.
        const float ratio = hi.rms / (lo.rms > 0.f ? lo.rms : 1e-9f);
        std::printf("  level ratio (expect ~4) = %.2f\n", ratio);
        check(ratio > 2.f && ratio < 8.f, "output level scales with buffer content");
    }

    // --- record captures the input --------------------------------------------------------------
    // Recording is level-triggered (~-40 dB), so the arm is followed by real signal, not silence.
    {
        ZeroInit<GranularEngine> r_store;
        GranularEngine& r = r_store.get();
        host::HostArena arena2; host::TimeSource time2;
        EngineContext ctx2 = host::make_context(arena2, time2);
        ZeroInit<Transport> transport2_store;
        Transport& transport2 = transport2_store.get();
        transport2.init(host::kSampleRate, static_cast<float>(host::kBlock), &time2);
        ctx2.transport = &transport2;
        r.init(ctx2);
        setup_wet(r);

        check(r.audio_is_empty(DeckRef::A), "deck starts empty before recording");

        r.on_record_pad(DeckRef::A, false);       // arm
        render(transport2, r, 400, 0.4f);                     // feed signal: crosses the threshold and records
        r.on_play_pad(DeckRef::A, false);         // stop recording -> loop what was captured

        check(!r.audio_is_empty(DeckRef::A), "deck is non-empty after recording");

        r.set_param(ParamId::Mix, DeckRef::A, 1.0f);   // hear the loop, not the input
        render(transport2, r, 100, 0.f);
        const Stats played = render(transport2, r, 200, 0.f);
        std::printf("  recorded loop plays back rms=%.5f peak=%.5f\n", played.rms, played.peak);
        check(played.finite, "recorded playback is finite");
        check(played.rms > 1e-3f, "what was recorded plays back as audio");
    }

    // --- every mode renders ---------------------------------------------------------------------
    // Reel / Slice / Drift take different paths through Generator and Vox; Drift runs the grain
    // scheduler. A mode that produced NaN would otherwise only show up on hardware.
    {
        load_buffer(e, 0.5f, 48000);
        // on_play_pad TOGGLES. The decks are already playing from the section above, so pressing it
        // again would STOP them - which is what happened on the first draft of this test, and the
        // per-mode checks still "passed" because they only asserted finite-and-bounded. Silence is
        // finite. Hence the explicit non-silence assertion below, and this guard.
        for (auto d : { DeckRef::A, DeckRef::B })
            if (!e.play_leds(d).playing) e.on_play_pad(d, false);
        const struct { int sw; const char* name; } modes[] = {
            { kSwitchReel, "Reel" }, { kSwitchSlice, "Slice" }, { kSwitchDrift, "Drift" } };
        for (const auto& m : modes) {
            for (auto d : { DeckRef::A, DeckRef::B }) e.set_config(ConfigId::Mode, d, m.sw);
            // Generous settle: Slice queues playback until a key/bar boundary (2 s at 120 BPM, i.e.
            // ~1000 blocks), so a short render would read as silence for reasons that are not a bug.
            render(transport, e, 1200);
            // Slice is the TRIGGERED mode: Deck::play() deliberately dispatches nothing for it
            // ("going to be triggered from tick method"), so with an empty sequencer track it is
            // correctly silent until something fires a slice. Give it a gate, as a patched CV would.
            if (m.sw == kSwitchSlice) {
                for (auto d : { DeckRef::A, DeckRef::B }) e.on_gate_trigger(d);
                render(transport, e, 50);
            }
            const Stats s = render(transport, e, 400);
            std::printf("  mode %-5s rms=%.5f peak=%.5f\n", m.name, s.rms, s.peak);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "%s renders finite audio", m.name);
            check(s.finite, msg);
            std::snprintf(msg, sizeof(msg), "%s stays bounded (|x| <= 4)", m.name);
            check(s.peak <= 4.f, msg);
            std::snprintf(msg, sizeof(msg), "%s actually produces audio", m.name);
            check(s.rms > 1e-4f, msg);
        }
    }

    // --- Reel -> Drift while playing lands -------------------------------------------------------
    // A switch to Drift waits for the sounding voices to stop (Deck::_validate_mode). Upstream 1.3.1
    // only applied the wait from Slice, so from Reel the voice was re-dispatched in Reel and the
    // deck never left it.
    {
        load_buffer(e, 0.5f, 48000);
        for (auto d : { DeckRef::A, DeckRef::B }) e.set_config(ConfigId::Mode, d, kSwitchReel);
        for (auto d : { DeckRef::A, DeckRef::B })
            if (!e.play_leds(d).playing) e.on_play_pad(d, false);
        render(transport, e, 200);
        check(e.play_leds(DeckRef::A).mode == Mode::Reel, "deck plays in Reel before the switch");
        e.set_config(ConfigId::Mode, DeckRef::A, kSwitchDrift);
        const Stats s = render(transport, e, 1200);
        check(e.play_leds(DeckRef::A).mode == Mode::Drift, "Reel -> Drift while playing reaches Drift");
        check(e.play_leds(DeckRef::A).playing, "the deck keeps playing across the switch");
        check(s.finite, "the Reel -> Drift switch renders finite audio");
    }

    // --- Alt+POS start-offset walk ---------------------------------------------------------------
    // With the walk off, every trigger replays the slice POS points at - the quiet head of the ramp.
    // With it on, the start advances one size-worth of slices per trigger, so the louder tail of the
    // ramp is reached and the mean level over the same number of triggers is higher. Asserting the
    // direction rather than an exact position keeps this independent of the slice grid's size.
    {
        for (auto d : { DeckRef::A, DeckRef::B }) e.set_config(ConfigId::Mode, d, kSwitchSlice);
        load_ramp(e, 48000);
        for (auto d : { DeckRef::A, DeckRef::B })
            if (!e.play_leds(d).playing) e.on_play_pad(d, false);
        e.set_param(ParamId::Pos,  DeckRef::A, 0.f);    // start at the quiet end of the ramp
        e.set_param(ParamId::Size, DeckRef::A, 0.05f);  // a short slice, so the walk has room to move
        render(transport, e, 1200);                     // Slice waits for a bar before it will play

        // Fire `triggers` gates, averaging the level each one renders.
        auto walk_level = [&](float alt_pos) {
            e.on_play_pad(DeckRef::A, false);                    // stop: rewinds the walk
            e.set_param(ParamId::AltPos, DeckRef::A, alt_pos);
            e.on_play_pad(DeckRef::A, false);                    // play again
            render(transport, e, 1200);
            double acc = 0; int n = 0; bool finite = true;
            for (int i = 0; i < 24; i++) {
                e.on_gate_trigger(DeckRef::A);
                const Stats s = render(transport, e, 40);
                finite = finite && s.finite;
                acc += s.rms; n++;
            }
            return std::pair<float, bool>{ static_cast<float>(acc / n), finite };
        };

        const auto off = walk_level(0.f);        // interval 0 -> walk disabled
        const auto on  = walk_level(0.125f);     // round(0.125 * 8) -> interval 1, steps every trigger
        std::printf("  start-offset walk: off rms=%.5f, on rms=%.5f\n", off.first, on.first);
        check(off.second && on.second, "the start-offset walk renders finite audio");
        check(on.first > off.first, "the start-offset walk moves the start into the buffer");
    }

    // --- Flux clocked delay follows the clock (upstream v1.3.0, wired here) ----------------------
    // An impulse through an empty deck's dry path into the flux delay. In ClockedDelay the time is a
    // note value of the TRANSPORT tempo; upstream never fed it the tempo, so it stayed at 120 BPM.
    {
        auto echo_at = [](const float bpm) {
            Rig r(bpm);
            r.e.set_config(ConfigId::Mode, DeckRef::A, kSwitchReel);
            r.e.set_param(ParamId::Mix, DeckRef::A, 0.f);                // dry input through the fx
            r.e.toggle_flux_mode(DeckRef::A);                              // FreeDelay -> ClockedDelay
            r.e.set_param(ParamId::FluxIntensity, DeckRef::A, 5.f / 7.f);  // index 5 of 8: one quarter
            r.e.set_param(ParamId::FluxFb, DeckRef::A, 0.f);               // a single echo
            r.e.set_param(ParamId::FluxMix, DeckRef::A, 1.f);
            r.e.set_fx(DeckRef::A, FxKind::Flux, true);
            r.run_silence(24000);                                          // settle tempo, lag, send
            const auto out = r.run(96000, [](size_t i) { return i == 0 ? 1.f : 0.f; });
            return std::pair<size_t, bool>{ peak_index(out, 1000), r.e.fx_leds(DeckRef::A).flux_mode == FluxMode::ClockedDelay };
        };
        const auto e120 = echo_at(120.f), e60 = echo_at(60.f);
        std::printf("  clocked flux echo: 120 BPM at %zu, 60 BPM at %zu samples\n", e120.first, e60.first);
        check(e120.second, "Flux+Tap switches the delay to ClockedDelay");
        check(e120.first > 23500 && e120.first < 24500, "a quarter at 120 BPM echoes after ~0.5 s");
        check(e60.first > 47000 && e60.first < 49000, "a quarter at 60 BPM echoes after ~1 s");
    }

    // --- Grit DJ filter (upstream v1.3.0) ------------------------------------------------------
    // Grit cycles Drive -> Reduce -> Filter. Intensity below .5 is the low-pass half: at 0 it cuts
    // to 60 Hz, so a Nyquist-rate square all but vanishes; at .5 the high-pass is fully open.
    {
        auto level = [](const float intensity) {
            Rig r;
            r.e.set_config(ConfigId::Mode, DeckRef::A, kSwitchReel);
            r.e.set_param(ParamId::Mix, DeckRef::A, 0.f);
            r.e.toggle_grit_mode(DeckRef::A);
            const GritReseed g = r.e.toggle_grit_mode(DeckRef::A);
            r.e.set_param(ParamId::GritIntensity, DeckRef::A, intensity);
            r.e.set_param(ParamId::GritMix, DeckRef::A, 1.f);
            r.e.set_fx(DeckRef::A, FxKind::Grit, true);
            const auto out = r.run(24000, [](size_t i) { return (i & 1) ? .25f : -.25f; });
            return std::pair<float, bool>{ rms_of(out, 12000), r.e.fx_leds(DeckRef::A).grit_mode == GritMode::Filter && g.mix >= 0.f };
        };
        const auto shut = level(0.f), open = level(.5f);
        std::printf("  dj filter at Nyquist: lp-shut rms=%.5f, open rms=%.5f\n", shut.first, open.first);
        check(shut.second, "two Grit+Tap presses reach the DJ filter");
        check(open.first > .1f, "the filter at centre passes the signal");
        check(shut.first < open.first * .05f, "the filter fully left cuts the top end");
    }

    // --- Slice size snaps to 1/8, and to 1/16 with Alt (upstream v1.3.0) ----------------------
    // 1 s at 120 BPM is 4 eighths of 12000 frames. SIZE .3 asks for 15120 frames: one eighth by
    // default, three sixteenths with Alt.
    {
        Rig r(120.f);
        r.e.set_config(ConfigId::Mode, DeckRef::A, kSwitchSlice);
        r.run_silence(4800);                                           // mode lands, tempo arrives
        load_buffer(r.e, .5f, 48000);
        LEDRing ring;
        auto size_at = [&](const bool alt) {
            r.e.set_config(ConfigId::AltSize, DeckRef::A, alt ? 1 : 0);
            r.e.set_param(ParamId::Size, DeckRef::A, .3f);
            r.run_silence(960);
            return r.e.render_ring(ring, DeckRef::A, 1.f).size;
        };
        const float plain = size_at(false), fine = size_at(true);
        std::printf("  slice size snap: plain=%.4f alt=%.4f\n", plain, fine);
        check(std::fabs(plain - .25f) < .002f, "Slice SIZE snaps to whole eighths");
        check(std::fabs(fine - .375f) < .002f, "Alt+SIZE snaps to sixteenths");
    }

    // --- WAV cues slice the buffer (upstream v1.1.0 import, cue_sm_a 1) -------------------------
    // Markers at 0, 12000, 30000 plus the end marker on_wav_cues appends. POS .5 picks the middle cue
    // of the two starts the snap offers, so the segment starts at 12000/48000 instead of .5.
    {
        Rig r;
        r.e.set_config(ConfigId::Mode, DeckRef::A, kSwitchReel);
        r.run_silence(960);
        load_buffer(r.e, .5f, 48000);
        r.e.set_param(ParamId::Pos, DeckRef::A, .5f);
        LEDRing ring;
        r.run_silence(960);
        const float free_start = r.e.render_ring(ring, DeckRef::A, 1.f).start;

        const char on[] = "cue_sm_a\n1\n";
        Config::dynamic().fill(reinterpret_cast<const uint8_t*>(on), sizeof(on) - 1);
        WavCues cues; cues.frames[0] = 0; cues.frames[1] = 12000; cues.frames[2] = 30000; cues.count = 3;
        r.e.on_wav_cues(DeckRef::A, cues);
        r.run_silence(960);
        const float cue_start = r.e.render_ring(ring, DeckRef::A, 1.f).start;
        const char off[] = "cue_sm_a\n0\n";
        Config::dynamic().fill(reinterpret_cast<const uint8_t*>(off), sizeof(off) - 1);
        std::printf("  cue import: start without cues=%.4f, with cues=%.4f\n", free_start, cue_start);
        check(std::fabs(free_start - .5f) < .002f, "without cues POS .5 starts mid-buffer");
        check(std::fabs(cue_start - .25f) < .002f, "with cue_sm_a 1 the start snaps to a WAV cue");
    }

    // --- MIDI CC (upstream v1.1.0 map) ----------------------------------------------------------
    {
        Rig r;
        const uint8_t ch_a = Config::dynamic().midi_channel_a();
        r.e.handle_midi_message(0xB0 | ch_a, 22, 127);                 // CC 22: Size
        check(r.e.param(ParamId::Size, DeckRef::A) == 1.f, "CC 22 sets deck A Size");
        check(r.e.take_param_reseed(DeckRef::A), "a CC asks the platform to re-pick its knob pickup");
        check(!r.e.take_param_reseed(DeckRef::A), "the reseed request clears once taken");
        r.e.handle_midi_message(0xB0 | 9, 22, 0);                      // a channel no deck listens on
        check(r.e.param(ParamId::Size, DeckRef::A) == 1.f, "a CC on an unmapped channel is ignored");
        r.e.set_param(ParamId::PolySlice, DeckRef::A, 0.f);
        check(Config::dynamic().is_slice_mono(DeckRef::A), "PolySlice writes the per-deck slc_mn setting");
        r.e.set_param(ParamId::PolySlice, DeckRef::A, 1.f);
    }

    if (g_failures == 0) { std::printf("\nAll granular audio tests passed.\n"); return 0; }
    std::printf("\nFAILED: %d check(s)\n", g_failures);
    return 1;
}
