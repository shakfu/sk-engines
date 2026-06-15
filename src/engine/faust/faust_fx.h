// faust_fx.h - generic IEngine wrapper for a SINGLE cyfaust-generated kernel.
//
// One IEngine implementation shared by every simple Faust engine (the analogue of GenEngine<W> for
// gen~). It is parameterized by a per-engine Traits struct (emitted by the generator from the .dsp +
// manifest) that names the kernel type, the ParamId->slider Bind table, the capabilities, and a few
// optional feature flags. The kernel is placement-new'd into the SDRAM arena at init() (its delay-line
// state is far too big for SRAM); set_param writes 0..1 platform values through the captured zones.
//
// Traits must provide:
//   using Kernel = <namespaced ::mydsp>;
//   static const faustgen::Bind* binds();   static int nbinds();   // ParamId-keyed bind table
//   static constexpr Capabilities caps;
//   static constexpr int  wet_dry_role;   // -1, or (int)ParamId for a SOFTWARE dry/wet crossfade
//   static constexpr bool soft_limit;     // cubic soft-clip the stereo bus
//   static constexpr bool meter;          // own-display: output-peak level ring
//   static constexpr uint32_t color;      // ring colour when meter

#pragma once

#include "engine/iengine.h"
#include "engine/engine_params.h"
#include "engine/display_model.h"
#include "engine/arena.h"
#include "engine/faust/faust_capture.h"
#include "daisysp.h"   // daisysp::SoftLimit (soft_limit feature)

#include <new>
#include <cstring>
#include <cmath>

namespace spotykach {

template <class Traits>
class FaustEngine : public IEngine {
    using Kernel = typename Traits::Kernel;
    static constexpr int    kRoles    = static_cast<int>(ParamId::Count);
    static constexpr size_t kMaxBlock = 128;   // platform block is 96
    static constexpr int    kMaxCh    = 8;     // Faust kernels are small; first 2 map to the stereo bus

public:
    FaustEngine() = default;
    ~FaustEngine() override = default;

    void init(const EngineContext& ctx) override {
        Arena ar(ctx.arena);
        if (void* m = ar.alloc<uint8_t>(sizeof(Kernel), alignof(Kernel))) _k = new (m) Kernel();
        if (!_k) return;
        _k->init(static_cast<int>(ctx.sample_rate));
        faustgen::CaptureUI<true> ui;  // generator path: capture slider defaults to seed the boot cache
        ui.roles = _role; ui.binds = Traits::binds(); ui.nbinds = Traits::nbinds();
        _k->buildUserInterface(&ui);
        _nin  = _k->getNumInputs();
        _nout = _k->getNumOutputs();
        // Seed the param cache from each bound slider's own default, so param() reports the kernel's real
        // boot state and the platform's pickup seeds with no jump.
        for (int r = 0; r < kRoles; r++) if (_role[r].bound()) _v[r] = _role[r].def01();
    }

    void prepare() override {}

    void process(const float* const* in, float** out, size_t size) override {
        const size_t n = size > kMaxBlock ? kMaxBlock : size;
        if (!_k) { std::memset(out[0], 0, size * sizeof(float)); std::memset(out[1], 0, size * sizeof(float)); return; }

        FAUSTFLOAT* cin[kMaxCh];
        FAUSTFLOAT* cout[kMaxCh];
        const int nin  = _nin  < kMaxCh ? _nin  : kMaxCh;
        const int nout = _nout < kMaxCh ? _nout : kMaxCh;
        for (int i = 0; i < nin;  i++) cin[i]  = const_cast<float*>((i < 2 && in) ? in[i] : _zero);
        for (int i = 0; i < nout; i++) cout[i] = (i < 2) ? out[i] : _discard;

        _k->compute(static_cast<int>(n), cin, cout);

        if (nout == 1) std::memcpy(out[1], out[0], n * sizeof(float));       // mono kernel -> dup to stereo
        else if (nout <= 0) { std::memset(out[0], 0, n * sizeof(float)); std::memset(out[1], 0, n * sizeof(float)); }

        if constexpr (Traits::wet_dry_role >= 0) {                           // software dry/wet crossfade
            if (in && in[0] && in[1]) {
                const float w = _v[Traits::wet_dry_role], d = 1.f - w;
                for (size_t i = 0; i < n; i++) { out[0][i] = w * out[0][i] + d * in[0][i];
                                                 out[1][i] = w * out[1][i] + d * in[1][i]; }
            }
        }
        if constexpr (Traits::soft_limit) {
            for (size_t i = 0; i < n; i++) { out[0][i] = daisysp::SoftLimit(out[0][i]);
                                             out[1][i] = daisysp::SoftLimit(out[1][i]); }
        }
        if constexpr (Traits::meter) {
            float p = 0.f;
            for (size_t i = 0; i < n; i++) { p = std::fmax(p, std::fabs(out[0][i])); p = std::fmax(p, std::fabs(out[1][i])); }
            _peak = p;
        }
    }

    Capabilities capabilities() const override { return Traits::caps; }

    void set_param(ParamId id, DeckRef::Ref, float v) override {
        const int r = static_cast<int>(id);
        if (r < 0 || r >= kRoles) return;
        _v[r] = v;
        if (_role[r].bound()) _role[r].set(v);
    }

    // The MODFREQ knob is delivered here (not via set_param), so a binding onto ParamId::ModSpeed - the
    // natural home for a Faust LFO-rate slider - works. A no-op if nothing binds ModSpeed.
    void set_mod_speed(DeckRef::Ref, float v, bool /*sync*/) override {
        const int r = static_cast<int>(ParamId::ModSpeed);
        _v[r] = v;
        if (_role[r].bound()) _role[r].set(v);
    }

    float param(ParamId id, DeckRef::Ref) const override {
        const int r = static_cast<int>(id);
        return (r >= 0 && r < kRoles) ? _v[r] : 0.f;
    }

    void render(DisplayModel& m) override {
        if constexpr (Traits::meter) {
            m.clear();
            const float lvl = _peak > 1.f ? 1.f : _peak;
            for (int d = 0; d < 2; d++) {
                if (lvl > 1e-4f) { m.ring[d].set_hex_color(Traits::color); m.ring[d].set_segment(0.f, lvl * 0.999f); }
                m.ring[d].set_updated();
                m.play[d] = { Traits::color, lvl > 1e-4f ? 1.f : 0.f };
            }
        }
    }

private:
    Kernel*        _k = nullptr;
    faustgen::Role _role[kRoles];
    float          _v[kRoles] = {};
    int            _nin = 0, _nout = 0;
    float          _peak = 0.f;
    float          _zero[kMaxBlock] = {};
    float          _discard[kMaxBlock];
};

} // namespace spotykach
