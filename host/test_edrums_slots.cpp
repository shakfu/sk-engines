// Headless test for the edrums 4-drum slot model (two decks x two slots).
//
// The platform stays 2-deck (DeckRef); the engine maps each deck to one of two internal "slots"
// (drums), the active one being the editable/shown drum. The Rev pad (on_play_pad reverse=true)
// swaps the active slot and asks the platform to re-seed that deck's knob pickup. This pins, through
// the public IEngine surface only:
//   1. set_param/param address the deck's ACTIVE slot, and the two slots are independent.
//   2. on_play_pad(reverse=true) toggles the active slot (and only the addressed deck); reverse=false
//      does not. Both return false (no "empty" flash).
//   3. take_param_reseed reports a swap exactly once, then self-clears, per deck.
//   4. The four-voice process()/tick path stays finite.
//
// Slot defaults seeded by init() (see EdrumsEngine::_init_slot). All four boot with Pos=0 (zero
// onsets) so the kit starts SILENT; the player raises POS to build it up. Models/pitch/decay are
// still seeded, so a raised drum is in tune immediately.
//   deck A: slot0 Kick (Aux 0.00, Speed .50, Mix .50, Pos 0), slot1 Tom (Aux 1.00, Speed .40, Mix .55, Pos 0)
//   deck B: slot0 Snare(Aux 0.25, Speed .50, Mix .50, Pos 0), slot1 Hat (Aux 0.75, Speed .82, Mix .30, Pos 0)

#include <cmath>
#include <cstdio>
#include <functional>

#include "engine/edrums/edrums_engine.h"
#include "engine/display_model.h"
#include "engine/itransport.h"
#include "host_setup.h"

using namespace spotykach;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Minimal transport: edrums init() subscribes via set_on_tick; the tests drive the captured callback
// directly to step the sequencer. The read-only queries are never exercised by the slot logic.
struct StubTransport : ITransport {
    std::function<void(const TransportTick&)> on_tick;
    float               tempo() const override { return 120.f; }
    ClockSource::Source source() const override { return ClockSource::internal; }
    bool                is_external_sync() const override { return false; }
    uint8_t             key_interval() const override { return 4; }
    bool                is_key_sub_quarter() const override { return false; }
    void set_on_tick(std::function<void(const TransportTick&)> cb) override { on_tick = std::move(cb); }
};

} // namespace

int main() {
    host::TimeSource time;
    host::HostArena  arena;
    StubTransport    transport;

    EngineContext ctx = host::make_context(arena, time);
    ctx.transport = &transport;

    EdrumsEngine e;
    e.init(ctx);

    // --- 1. Boot state addresses slot 0 of each deck -------------------------------------------
    check(approx(e.param(ParamId::Aux, DeckRef::A), 0.00f), "A active = slot0 (Kick) at boot");
    check(approx(e.param(ParamId::Aux, DeckRef::B), 0.25f), "B active = slot0 (Snare) at boot");
    check(approx(e.param(ParamId::Pos, DeckRef::A), 0.00f), "A slot0 Pos seeded 0 (silent boot)");
    check(approx(e.param(ParamId::Pos, DeckRef::B), 0.00f), "B slot0 Pos seeded 0 (silent boot)");
    check(e.take_param_reseed(DeckRef::A) == false, "no reseed pending before any swap (A)");
    check(e.take_param_reseed(DeckRef::B) == false, "no reseed pending before any swap (B)");

    // --- 2. Rev swap on A focuses slot 1 (Tom) and requests one reseed -------------------------
    check(e.on_play_pad(DeckRef::A, /*reverse=*/true) == false, "Rev pad returns false (no empty flash)");
    check(e.take_param_reseed(DeckRef::A) == true,  "swap raises reseed for A exactly once");
    check(e.take_param_reseed(DeckRef::A) == false, "reseed self-clears after one read");
    check(approx(e.param(ParamId::Aux,   DeckRef::A), 1.00f), "A now addresses slot1 (Tom, Aux 1.0)");
    check(approx(e.param(ParamId::Speed,   DeckRef::A), 0.40f), "A slot1 Speed seeded 0.40");
    check(approx(e.param(ParamId::Mix,     DeckRef::A), 0.80f), "A slot1 Mix (gain) seeded 0.80");
    check(approx(e.param(ParamId::GritMix, DeckRef::A), 0.55f), "A slot1 decay (grit+SOS) seeded 0.55");

    // Deck B is untouched by a deck-A swap.
    check(approx(e.param(ParamId::Aux, DeckRef::B), 0.25f), "B unaffected by A swap (still Snare)");
    check(e.take_param_reseed(DeckRef::B) == false, "no reseed pending for B after A swap");

    // --- 3. Swap A back to slot 0 --------------------------------------------------------------
    e.on_play_pad(DeckRef::A, true);
    check(e.take_param_reseed(DeckRef::A) == true,  "swap-back raises reseed again");
    check(approx(e.param(ParamId::Aux,   DeckRef::A), 0.00f), "A back to slot0 (Kick)");
    check(approx(e.param(ParamId::Speed, DeckRef::A), 0.50f), "A slot0 Speed still 0.50");

    // --- 4. The two slots are independent: editing one never touches the other ----------------
    e.set_param(ParamId::Speed, DeckRef::A, 0.70f);          // edits slot0 (active)
    e.on_play_pad(DeckRef::A, true);                         // -> slot1
    check(approx(e.param(ParamId::Speed, DeckRef::A), 0.40f), "slot1 unchanged by a slot0 edit");
    e.set_param(ParamId::Speed, DeckRef::A, 0.10f);          // edits slot1
    e.on_play_pad(DeckRef::A, true);                         // -> slot0
    check(approx(e.param(ParamId::Speed, DeckRef::A), 0.70f), "slot0 preserved across the round trip");
    e.on_play_pad(DeckRef::A, true);                         // -> slot1
    check(approx(e.param(ParamId::Speed, DeckRef::A), 0.10f), "slot1 holds its own edit");
    e.on_play_pad(DeckRef::A, true);                         // -> slot0 (restore focus)
    e.take_param_reseed(DeckRef::A);                         // drain the pending flags from these swaps

    // --- 5. Plain Play pad (reverse=false) neither swaps nor requests a reseed -----------------
    const float aux_before = e.param(ParamId::Aux, DeckRef::A);
    check(e.on_play_pad(DeckRef::A, /*reverse=*/false) == false, "Play pad returns false");
    check(e.take_param_reseed(DeckRef::A) == false, "Play pad raises no reseed");
    check(approx(e.param(ParamId::Aux, DeckRef::A), aux_before), "Play pad does not change the active slot");

    // --- 6. The kit boots SILENT (Pos=0 everywhere -> no onsets), and raising POS makes it sound --
    float in_l[host::kBlock] = {0}, in_r[host::kBlock] = {0};
    float out_l[host::kBlock], out_r[host::kBlock];
    const float* in_ptrs[2]  = { in_l, in_r };
    float*       out_ptrs[2] = { out_l, out_r };

    auto peak = [&]() {
        float pk = 0.f;
        for (size_t i = 0; i < host::kBlock; i++) {
            pk = std::fmax(pk, std::fabs(out_l[i]));
            pk = std::fmax(pk, std::fabs(out_r[i]));
        }
        return pk;
    };
    auto drive = [&](uint32_t steps) {
        TransportTick t; t.tick = true; t.tempo = 120.f;
        for (uint32_t i = 0; i < steps; i++) { t.index = i; transport.on_tick(t); }
    };

    // Step all four drums through a full cycle with no onsets seeded: the bus must be pure silence.
    drive(64);
    e.process(in_ptrs, out_ptrs, host::kBlock);
    check(peak() == 0.f, "kit boots silent: no output before any POS is raised");

    // Raise POS on deck A's focused drum (slot 0, kick) to full density so every step is an onset,
    // then step once and render: a hit must now reach the bus, and output stays finite.
    e.set_param(ParamId::Pos, DeckRef::A, 1.0f);
    drive(1);
    e.process(in_ptrs, out_ptrs, host::kBlock);
    bool finite = true;
    for (size_t i = 0; i < host::kBlock; i++)
        if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
    check(finite, "process() output is finite once a drum sounds");
    check(peak() > 0.f, "raising POS makes the focused drum audible");

    // --- 7. Per-model voicing: each of the 5 models triggers a finite, bounded, audible hit, and the
    // hat's high-passed noise is far brighter than the kick's sine body (proves the model wiring + the
    // band/high-pass split). Deck A slot 0 is the only onset-active drum (POS=1 above; the rest silent),
    // so the bus is just this one voice; Aux selects its model, then one tick triggers a single hit.
    constexpr int kNumModels = 5;       // kick, snare, clap, hat, tom (EdrumsEngine::kModelCount)
    auto analyze = [&](int model, float& pk, float& bright, bool& fin) {
        e.set_param(ParamId::Aux, DeckRef::A, static_cast<float>(model) / (kNumModels - 1)); // select model
        drive(1);                                                                            // one onset -> trigger
        pk = 0.f; fin = true; double energy = 0.0, diffs = 0.0; float prev = 0.f; bool first = true;
        for (int b = 0; b < 48; b++) {                          // ~48 blocks of single-hit decay (no more ticks)
            e.process(in_ptrs, out_ptrs, host::kBlock);
            for (size_t i = 0; i < host::kBlock; i++) {
                const float x = out_l[i];
                if (!std::isfinite(x)) fin = false;
                pk = std::fmax(pk, std::fabs(x));
                energy += std::fabs(x);
                if (!first) diffs += std::fabs(x - prev);       // first-difference = crude HF content
                prev = x; first = false;
            }
        }
        bright = static_cast<float>(diffs / (energy + 1e-9));   // brightness = HF / total energy
    };
    float pk[kNumModels], br[kNumModels];
    bool  fin[kNumModels];
    for (int m = 0; m < kNumModels; m++) {
        analyze(m, pk[m], br[m], fin[m]);
        check(fin[m],            "model renders finite output");
        check(pk[m] > 0.f,       "model is audible when triggered");
        check(pk[m] < 1.2f,      "model output stays bounded (per-voice drive + bus SoftLimit)");
    }
    std::printf("voicing: brightness kick=%.3f snare=%.3f clap=%.3f hat=%.3f tom=%.3f\n",
                br[0], br[1], br[2], br[3], br[4]);
    check(br[3] > 2.f * br[0], "hat (high-passed noise) is far brighter than the kick (sine body)");

    // --- 8. Live sound macros reach the focused drum: SOS is now per-drum GAIN (was decay), decay
    // moved to grit+SOS, and the grit/flux timbre macros stay finite/bounded. (Deck A slot 0, POS=1.)
    {
        e.set_param(ParamId::Aux, DeckRef::A, 0.f);   // kick, deterministic
        auto energy = [&](float& pk, bool& fin) {
            drive(1); pk = 0.f; fin = true; double e_acc = 0.0;
            for (int b = 0; b < 48; b++) {
                e.process(in_ptrs, out_ptrs, host::kBlock);
                for (size_t i = 0; i < host::kBlock; i++) {
                    const float x = out_l[i];
                    if (!std::isfinite(x)) fin = false;
                    pk = std::fmax(pk, std::fabs(x));
                    e_acc += std::fabs(x);
                }
            }
            return static_cast<float>(e_acc);
        };
        float pk; bool fin;
        // SOS = gain
        e.set_param(ParamId::Mix, DeckRef::A, 0.f);   const float en_silent = energy(pk, fin);
        check(pk == 0.f, "SOS (gain) at 0 silences the drum");
        e.set_param(ParamId::Mix, DeckRef::A, 0.4f);  const float en_soft = energy(pk, fin);
        e.set_param(ParamId::Mix, DeckRef::A, 1.0f);  const float en_loud = energy(pk, fin);
        check(en_loud > en_soft && en_soft > en_silent, "SOS sets per-drum gain (louder with more SOS)");
        // decay relocated to grit+SOS: a longer decay rings out more total energy
        e.set_param(ParamId::Mix, DeckRef::A, 0.8f);
        e.set_param(ParamId::GritMix, DeckRef::A, 0.1f); const float en_short = energy(pk, fin);
        e.set_param(ParamId::GritMix, DeckRef::A, 0.9f); const float en_long = energy(pk, fin);
        check(en_long > 1.5f * en_short, "decay on grit+SOS lengthens the tail");
        // timbre macros (drive / pitch-sweep / brightness / body-noise) stay finite + bounded
        e.set_param(ParamId::GritMix,       DeckRef::A, 0.5f);
        e.set_param(ParamId::GritIntensity, DeckRef::A, 1.0f);  // drive
        e.set_param(ParamId::FluxIntensity, DeckRef::A, 1.0f);  // pitch-sweep
        e.set_param(ParamId::FluxMix,       DeckRef::A, 1.0f);  // body-noise (full noise)
        e.set_param(ParamId::FluxFb,        DeckRef::A, 0.0f);  // brightness (dark)
        energy(pk, fin);
        check(fin, "output finite with all timbre macros pushed");
        check(pk < 1.2f, "output bounded with all timbre macros pushed");
    }

    // --- 9. Preset persistence: serialize() / apply() round-trip the whole kit ------------------
    {
        StubTransport tr2;
        EngineContext c1 = host::make_context(arena, time); c1.transport = &tr2;
        EdrumsEngine src; src.init(c1);

        // Mutate a spread of params across the four drums (focused-slot writes; Rev-swap to reach slot 1).
        src.set_config(ConfigId::Route, DeckRef::A, 1);            // DoubleMono
        src.set_param(ParamId::Mix,           DeckRef::A, 0.30f);  // A slot0 gain
        src.set_param(ParamId::GritMix,       DeckRef::A, 0.70f);  // A slot0 decay
        src.set_param(ParamId::GritIntensity, DeckRef::A, 0.90f);  // A slot0 drive
        src.set_param(ParamId::FluxMix,       DeckRef::A, 0.20f);  // A slot0 body/noise
        src.set_param(ParamId::Pos,           DeckRef::A, 0.50f);  // A slot0 density
        src.set_param(ParamId::Aux,           DeckRef::A, 0.50f);  // A slot0 model -> clap (2)
        src.set_param(ParamId::Speed,         DeckRef::B, 0.70f);  // B slot0 pitch
        src.on_play_pad(DeckRef::A, true);                          // focus A slot1
        src.set_param(ParamId::FluxFb,        DeckRef::A, 0.10f);  // A slot1 brightness
        src.set_param(ParamId::Mix,           DeckRef::A, 0.60f);  // A slot1 gain
        src.on_play_pad(DeckRef::A, true);                          // back to slot0

        EdrumsEngine::KitData kd; src.serialize(kd);

        // A fresh engine fed the blob must reproduce the same serialized state.
        StubTransport tr3;
        EngineContext c2 = host::make_context(arena, time); c2.transport = &tr3;
        EdrumsEngine dst; dst.init(c2);
        dst.apply(kd);
        EdrumsEngine::KitData kd2; dst.serialize(kd2);

        check(kd2.route == kd.route,                                         "route round-trips");
        check(kd2.active[0] == kd.active[0] && kd2.active[1] == kd.active[1], "focus round-trips");
        check(kd2.drum[0][0].model == 2,                                     "A slot0 model (clap) round-trips");
        check(approx(kd2.drum[0][0].gain,   0.30f),                          "A slot0 gain round-trips");
        check(approx(kd2.drum[0][0].decay,  0.70f),                          "A slot0 decay round-trips");
        check(approx(kd2.drum[0][0].drive,  0.90f),                          "A slot0 drive round-trips");
        check(approx(kd2.drum[0][0].tone,   0.20f),                          "A slot0 body/noise round-trips");
        check(approx(kd2.drum[0][0].pos,    0.50f),                          "A slot0 density round-trips");
        check(approx(kd2.drum[0][1].bright, 0.10f),                          "A slot1 brightness round-trips");
        check(approx(kd2.drum[0][1].gain,   0.60f),                          "A slot1 gain round-trips");
        check(approx(kd2.drum[1][0].pitch,  0.70f),                          "B slot0 pitch round-trips");

        // apply() also rebuilds live state: param() on the focused drum reads back the applied values.
        check(approx(dst.param(ParamId::Mix,     DeckRef::A), 0.30f), "applied gain reaches param()");
        check(approx(dst.param(ParamId::GritMix, DeckRef::A), 0.70f), "applied decay reaches param()");

        // A blob with an unknown version is ignored (state stays at fresh defaults).
        EdrumsEngine::KitData bad = kd; bad.version = 99;
        StubTransport tr4; EngineContext c3 = host::make_context(arena, time); c3.transport = &tr4;
        EdrumsEngine def; def.init(c3);
        EdrumsEngine::KitData d0; def.serialize(d0);
        def.apply(bad);
        EdrumsEngine::KitData d1; def.serialize(d1);
        check(approx(d1.drum[0][0].gain, d0.drum[0][0].gain) && d1.route == d0.route,
              "unknown-version blob is rejected (state unchanged)");
    }

    if (g_failures == 0) { std::printf("OK: all edrums slot checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
