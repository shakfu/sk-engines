// gen_engine.h - Generic IEngine wrapper for gen-dsp-generated DSP.
//
// One IEngine implementation shared by every gen~ engine. It is parameterized
// by a per-export "wrap" traits struct W (generated alongside the export) that
// forwards to that export's gen-dsp wrapper_* C interface and owns the
// ParamId -> gen-parameter mapping. Only one gen engine is compiled per binary
// (build-time ENGINE selection), so there is no symbol-collision concern.
//
// W must provide these static members:
//   void* create(float sr, long block);
//   void  perform(void* st, float** in, long nin, float** out, long nout, long n);
//   int   num_inputs();
//   int   num_outputs();
//   void  set_param(void* st, ParamId, DeckRef::Ref, float v01);   // v01 in 0..1
//   float get_param(void* st, ParamId, DeckRef::Ref);              // returns 0..1
//
// The genlib runtime (genlib_arena.cpp) allocates gen~ state from the platform
// SDRAM arena; we bind it in init() before creating state.

#pragma once

#include "engine/iengine.h"
#include "engine/gen/genlib_arena.h"

#include <cstring>
#include <type_traits>
#include <utility>

namespace spotykach {

// Detect an optional static W::tick(void* st) hook. A wrap defines it when it needs to
// advance a parameter once per audio block independently of knob events - e.g. gigaverb
// slews its roomsize so the delay tank glides instead of stepping. Wraps without it are
// unaffected (the call is dropped at compile time).
template <class T, class = void>
struct gen_has_tick : std::false_type {};
template <class T>
struct gen_has_tick<T, std::void_t<decltype(T::tick(std::declval<void*>()))>>
    : std::true_type {};

// Max gen~ channels we marshal. gen-dsp caps buffers at 8; real exports are
// small. The first two channels map to the platform stereo bus; any extras are
// fed silence (inputs) or discarded (outputs).
inline constexpr int kGenMaxChannels = 8;
// Covers the platform's 96-sample audio block with headroom.
inline constexpr size_t kGenScratch = 128;

template <class W>
class GenEngine : public IEngine {
public:
    GenEngine() = default;
    ~GenEngine() override = default;

    void init(const EngineContext& ctx) override {
        // Route all gen~ allocation into the platform SDRAM arena, then create
        // state (which allocates from it).
        genlib_arena_bind(ctx.arena.base, ctx.arena.bytes);
        _state = W::create(ctx.sample_rate, static_cast<long>(ctx.block_size));
        _nin   = W::num_inputs();
        _nout  = W::num_outputs();
        std::memset(_zero, 0, sizeof(_zero));
    }

    void prepare() override {}

    void process(const float* const* in, float** out, size_t size) override {
        if (!_state) {
            std::memset(out[0], 0, size * sizeof(float));
            std::memset(out[1], 0, size * sizeof(float));
            return;
        }

        // Per-block parameter slew (optional; compiled away for wraps without tick()).
        if constexpr (gen_has_tick<W>::value) W::tick(_state);

        const float* gin[kGenMaxChannels];
        float* gout[kGenMaxChannels];
        const int nin  = _nin  < kGenMaxChannels ? _nin  : kGenMaxChannels;
        const int nout = _nout < kGenMaxChannels ? _nout : kGenMaxChannels;

        for (int i = 0; i < nin; i++)  gin[i]  = (i < 2) ? in[i]  : _zero;
        for (int i = 0; i < nout; i++) gout[i] = (i < 2) ? out[i] : _discard;

        // gen~ perform takes float** (GENLIB_USE_FLOAT32 so t_sample == float);
        // the input pointers are const on our side only.
        W::perform(_state, const_cast<float**>(gin), nin, gout, nout,
                   static_cast<long>(size));

        // Normalize to the stereo output bus.
        if (nout == 1) {
            std::memcpy(out[1], out[0], size * sizeof(float));
        } else if (nout <= 0) {
            std::memset(out[0], 0, size * sizeof(float));
            std::memset(out[1], 0, size * sizeof(float));
        }
    }

    void set_param(ParamId id, DeckRef::Ref deck, float v) override {
        if (_state) W::set_param(_state, id, deck, v);
    }

    float param(ParamId id, DeckRef::Ref deck) const override {
        return _state ? W::get_param(_state, id, deck) : 0.f;
    }

private:
    void* _state = nullptr;
    int   _nin = 0;
    int   _nout = 0;
    float _zero[kGenScratch];
    float _discard[kGenScratch];
};

} // namespace spotykach
