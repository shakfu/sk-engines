#include "core.h"
#include "daisysp.h"
#include <functional>
#include <cstring>
#include "mode.h"
#include "expose.h"
#include "engine/arena.h"

using namespace spotykach;
using namespace daisysp;

Core::Core():
_driver { Driver(_decks[DeckRef::A], _decks[DeckRef::B], _click, _panner, _mod.data()) }
{
    _source.fill(Deck::Source::external);
    _reverb_in.fill(0);
    _reverb_out.fill(0);
    _bus.fill(0);
    _xfade.SetStage(.5f);
};
  
void Core::init(const EngineContext& ctx) {
    const auto sample_rate = ctx.sample_rate;
    _driver.init(sample_rate, ctx.block_size, ctx.time);
    _panner.init(sample_rate);
    _click.init(sample_rate);
    _mix_smooth.init(sample_rate);

    // Sub-allocate all granular buffers from the platform's opaque SDRAM arena (item: EngineBuffers
    // generalization, Stage 2). Sizes, 32K alignment, and source zeroing are preserved exactly from
    // the old SDRAM pool, so this is behaviour-identical - just a different backing layout.
    Arena arena(ctx.arena);
    const size_t source_frames = static_cast<size_t>(kSourceMaxSeconds) * static_cast<size_t>(sample_rate);

    for (auto d = 0; d < DeckRef::Count; d++) {
        auto ref = (DeckRef::Ref)d;
        deck(ref).ref = ref;

        auto* src = arena.alloc<Buffer::Frame>(source_frames, 32768);
        std::memset(src, 0, source_frames * sizeof(Buffer::Frame));
        _detect_buf[d][0] = arena.alloc<float>(Detector::kWindow, 32768);
        _detect_buf[d][1] = arena.alloc<float>(Detector::kWindow, 32768);
        _delay_buf[d][0]  = arena.alloc<float>(Fx::kEchoDelayBufferLength, 32768);
        _delay_buf[d][1]  = arena.alloc<float>(Fx::kEchoDelayBufferLength, 32768);

        Deck::Params p;
        p.sample_rate = sample_rate;
        p.main_buf_size = source_frames;
        p.main_buf = src;
        p.detect_buf = _detect_buf[d];
        p.delay_buf = _delay_buf[d];
        p.slice_buf = arena.alloc<size_t>(kMaxSlicePointCount, 32);
        p.track_buf = arena.alloc<Event>(Track::kLength, 32);
        deck(ref).init(p);

        _mod[ref].init(sample_rate);
    }
};

Panner::Mode _panner_mode(const spotykach::Mode mode)
{
    switch (mode) {
        case spotykach::Mode::Reel: return Panner::Mode::smooth;
        case spotykach::Mode::Slice: return Panner::Mode::step;
        default: return Panner::Mode::off;
    }
}
void Core::set_route(const Route val)
{
    if (val == _route) return;
    _route = val;

    infer_panner_mode();
}
void Core::infer_panner_mode()
{
    for (auto ref: { DeckRef::A, DeckRef::B }) {
        auto& deck = _decks[ref];
        auto pan_mode = Panner::Mode::off;
        auto wide = false;
        if (_route == Route::GenerativeStereo) {
            wide = deck.mode() == Mode::Drift;
            pan_mode = _panner_mode(deck.mode());
        }
        _panner.set_mode(pan_mode, ref);
        deck.voxs().set_is_wide(wide);
    }
}

void Core::prepare() 
{
    
}

void Core::process(const float* const* in, float** out, size_t size) 
{
    float in_a[2] = { 0, 0 };
    float in_b[2] = { 0, 0 };
    float out_a[2] = { 0, 0 };
    float out_b[2] = { 0, 0 };
    auto& deck_a = deck(DeckRef::A);
    auto& deck_b = deck(DeckRef::B);

    auto stereo = _route != Route::DoubleMono;

    for (size_t i = 0; i < size; i++) {
        if (stereo) {
            in_a[0] = in_b[0] = in[0][i];
            in_a[1] = in_b[1] = in[1][i];
        }
        else {
            in_a[0] = in_a[1] = in[0][i];
            in_b[0] = in_b[1] = in[1][i];
        }

        deck_a.process_out(in_a[0], in_a[1], out_a[0], out_a[1]);
        deck_b.process_out(in_b[0], in_b[1], out_b[0], out_b[1]);

        switch (_source[DeckRef::A]) {
            case Deck::Source::internal: deck_a.process_in(out_b[0], out_b[1]); break;
            case Deck::Source::external: deck_a.process_in(in_a[0], in_a[1]); break;
        }
        
        switch (_source[DeckRef::B]) {
            case Deck::Source::internal: deck_b.process_in(out_a[0], out_a[1]); break;
            case Deck::Source::external: deck_b.process_in(in_b[0], in_b[1]); break;
        }

        _mod[DeckRef::A].follow(out_a[0]);
        _mod[DeckRef::B].follow(out_b[0]);

        float* d_out[2] = { out_a, out_b };
        _panner.process(d_out, d_out);

        auto mix = _mix_smooth.process(_mix + _mix_mod);
        _xfade.SetStage(std::clamp(mix, 0.f, 1.f));
        
        if (stereo) {
            _xfade.Process(out_a[0], out_a[1], out_b[0], out_b[1], _bus[0], _bus[1]);
        }
        else {
            auto sum_a = (out_a[0] + out_a[1]) * 0.7079;
            auto sum_b = (out_b[0] + out_b[1]) * 0.7079;
            _xfade.Process(sum_a, 0, 0, sum_b, _bus[0], _bus[1]);
        }
        
        auto click = _click.process() * _click_mix;
        
        out[0][i] = SoftLimit(_bus[0] + click);
        out[1][i] = SoftLimit(_bus[1] + click);
    }
};
