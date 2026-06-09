// gigaverb_engine.h - gen~ "gigaverb" bound to the generic GenEngine.
//
// Generated glue for the gen-dsp export in this directory. The export's
// wrapper_* C interface lives in namespace `gigaverb_daisy` (from
// -DDAISY_EXT_NAME=gigaverb). This file forwards to it and maps the platform's
// ParamId knobs onto the export's 8 parameters (see manifest.json).
//
// Parameter map (ParamId -> gen index, normalized 0..1 -> [min,max]). Every entry
// is a ParamId the platform delivers via set_param(): the six plain panel knobs,
// plus two modifier layers for the extras (see docs/engine-types/gen.md for the grammar):
//   Size     SIZE knob       -> 5 roomsize  [0.1..300]
//   Pos      POS knob        -> 4 revtime   [0.1..1]
//   Speed    PITCH knob      -> 0 bandwidth [0..1]
//   Env      ENV knob        -> 1 damping   [0..1]
//   Mix      SOS knob        -> 2 dry       [0..1]
//   ModAmp   MOD_AMT knob    -> 7 tail      [0..1]
//   Feedback SOS + Alt       -> 6 spread    [0..100]
//   EnvSize  ENV + chord     -> 3 early     [0..1]
// This mapping is the one spot to retune per engine; everything else is generic.

#pragma once

#include "engine/iengine.h"
#include "_ext_daisy.h"            // namespace gigaverb_daisy { wrapper_* }; GenState == void
#include "engine/gen/gen_engine.h"

namespace spotykach {

struct GigaverbWrap {
    static void* create(float sr, long block) {
        return gigaverb_daisy::wrapper_create(sr, block);
    }
    static void perform(void* st, float** in, long nin, float** out, long nout, long n) {
        gigaverb_daisy::wrapper_perform(st, in, nin, out, nout, n);
    }
    static int num_inputs()  { return gigaverb_daisy::wrapper_num_inputs(); }
    static int num_outputs() { return gigaverb_daisy::wrapper_num_outputs(); }

    // ParamId -> gen parameter index (-1 = unmapped).
    static int index_of(ParamId id) {
        switch (id) {
            case ParamId::Speed:    return 0;  // bandwidth (PITCH)
            case ParamId::Env:      return 1;  // damping   (ENV)
            case ParamId::Mix:      return 2;  // dry       (SOS)
            case ParamId::EnvSize:  return 3;  // early     (ENV + chord)
            case ParamId::Pos:      return 4;  // revtime   (POS)
            case ParamId::Size:     return 5;  // roomsize  (SIZE)
            case ParamId::Feedback: return 6;  // spread    (SOS + Alt)
            case ParamId::ModAmp:   return 7;  // tail      (MOD_AMT)
            default:                return -1;
        }
    }

    static void set_param(void* st, ParamId id, DeckRef::Ref deck, float v01) {
        if (deck == DeckRef::B) return;  // single stereo effect: ignore deck B
        const int i = index_of(id);
        if (i < 0) return;
        const float lo = gigaverb_daisy::wrapper_param_min(st, i);
        const float hi = gigaverb_daisy::wrapper_param_max(st, i);
        gigaverb_daisy::wrapper_set_param(st, i, lo + v01 * (hi - lo));
    }

    static float get_param(void* st, ParamId id, DeckRef::Ref /*deck*/) {
        const int i = index_of(id);
        if (i < 0) return 0.f;
        const float lo  = gigaverb_daisy::wrapper_param_min(st, i);
        const float hi  = gigaverb_daisy::wrapper_param_max(st, i);
        const float val = gigaverb_daisy::wrapper_get_param(st, i);
        return (hi > lo) ? (val - lo) / (hi - lo) : 0.f;
    }
};

using GigaverbEngine = GenEngine<GigaverbWrap>;

} // namespace spotykach
