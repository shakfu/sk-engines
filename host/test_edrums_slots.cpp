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
// Slot defaults seeded by init() (see EdrumsEngine::_init_slot):
//   deck A: slot0 Kick (Aux 0.00, Speed .50, Mix .50, Pos .30), slot1 Tom (Aux 1.00, Speed .40, Mix .55, Pos .30)
//   deck B: slot0 Snare(Aux 0.25, Speed .50, Mix .50, Pos .45), slot1 Hat (Aux 0.75, Speed .82, Mix .30, Pos .50)

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
    check(approx(e.param(ParamId::Pos, DeckRef::A), 0.30f), "A slot0 Pos seeded 0.30");
    check(approx(e.param(ParamId::Pos, DeckRef::B), 0.45f), "B slot0 Pos seeded 0.45");
    check(e.take_param_reseed(DeckRef::A) == false, "no reseed pending before any swap (A)");
    check(e.take_param_reseed(DeckRef::B) == false, "no reseed pending before any swap (B)");

    // --- 2. Rev swap on A focuses slot 1 (Tom) and requests one reseed -------------------------
    check(e.on_play_pad(DeckRef::A, /*reverse=*/true) == false, "Rev pad returns false (no empty flash)");
    check(e.take_param_reseed(DeckRef::A) == true,  "swap raises reseed for A exactly once");
    check(e.take_param_reseed(DeckRef::A) == false, "reseed self-clears after one read");
    check(approx(e.param(ParamId::Aux,   DeckRef::A), 1.00f), "A now addresses slot1 (Tom, Aux 1.0)");
    check(approx(e.param(ParamId::Speed, DeckRef::A), 0.40f), "A slot1 Speed seeded 0.40");
    check(approx(e.param(ParamId::Mix,   DeckRef::A), 0.55f), "A slot1 Mix seeded 0.55");

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

    // --- 6. Four-voice tick + process path stays finite ----------------------------------------
    TransportTick t; t.tick = true; t.tempo = 120.f;
    for (uint32_t i = 0; i < 64; i++) { t.index = i; transport.on_tick(t); } // step all four drums

    float in_l[host::kBlock] = {0}, in_r[host::kBlock] = {0};
    float out_l[host::kBlock], out_r[host::kBlock];
    const float* in_ptrs[2]  = { in_l, in_r };
    float*       out_ptrs[2] = { out_l, out_r };
    e.process(in_ptrs, out_ptrs, host::kBlock);
    bool finite = true;
    for (size_t i = 0; i < host::kBlock; i++)
        if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
    check(finite, "process() output is finite with four voices");

    if (g_failures == 0) { std::printf("OK: all edrums slot checks passed\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
