// gen_remap_inputs.h - Input-to-parameter remapping for gen-dsp wrappers
//
// When gen~ exports use signal-rate `in` objects for control data (e.g.,
// pitch, gate), --inputs-as-params converts them to plugin parameters.
// The gen~ perform function still receives its expected input buffers --
// this header fills the remapped input buffers with parameter values.
//
// Required defines when REMAP_INPUT_COUNT > 0:
//   REMAP_INPUT_COUNT        - number of remapped inputs
//   REMAP_GEN_TOTAL_INPUTS   - gen~'s original total input count
//   REMAP_INPUT_N_GEN_IDX    - gen~ input index for remap slot N
//   REMAP_INPUT_N_PARAM_IDX  - parameter index for remap slot N
//   REMAP_INPUT_N_NAME       - display name string for remap slot N
//
// Required from the including bridge:
//   perform()       - gen~'s perform function (via genlib_exportfunctions.h)
//   num_inputs()    - gen~'s original input count
//   num_params()    - gen~'s original param count
//   setparameter()  - genlib setparameter
//   getparameter()  - genlib getparameter
//   getparametername(), getparametermin(), getparametermax(),
//   getparameterhasminmax() - genlib parameter accessors
//   _silence[]      - zero-filled float buffer (from bridge)
//   CommonState, t_sample, t_param - genlib types

#ifndef GEN_REMAP_INPUTS_H
#define GEN_REMAP_INPUTS_H

#ifdef REMAP_INPUT_COUNT
#if REMAP_INPUT_COUNT > 0

// ---------------------------------------------------------------------------
// Remap table: compile-time mapping from gen~ input index to param index
// ---------------------------------------------------------------------------

struct RemapEntry { int gen_idx; int param_idx; };

static const RemapEntry _remap_table[] = {
#if REMAP_INPUT_COUNT > 0
    { REMAP_INPUT_0_GEN_IDX, REMAP_INPUT_0_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 1
    { REMAP_INPUT_1_GEN_IDX, REMAP_INPUT_1_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 2
    { REMAP_INPUT_2_GEN_IDX, REMAP_INPUT_2_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 3
    { REMAP_INPUT_3_GEN_IDX, REMAP_INPUT_3_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 4
    { REMAP_INPUT_4_GEN_IDX, REMAP_INPUT_4_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 5
    { REMAP_INPUT_5_GEN_IDX, REMAP_INPUT_5_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 6
    { REMAP_INPUT_6_GEN_IDX, REMAP_INPUT_6_PARAM_IDX },
#endif
#if REMAP_INPUT_COUNT > 7
    { REMAP_INPUT_7_GEN_IDX, REMAP_INPUT_7_PARAM_IDX },
#endif
};

// Display names for remapped params
static const char* _remap_param_names[] = {
#if REMAP_INPUT_COUNT > 0
    REMAP_INPUT_0_NAME,
#endif
#if REMAP_INPUT_COUNT > 1
    REMAP_INPUT_1_NAME,
#endif
#if REMAP_INPUT_COUNT > 2
    REMAP_INPUT_2_NAME,
#endif
#if REMAP_INPUT_COUNT > 3
    REMAP_INPUT_3_NAME,
#endif
#if REMAP_INPUT_COUNT > 4
    REMAP_INPUT_4_NAME,
#endif
#if REMAP_INPUT_COUNT > 5
    REMAP_INPUT_5_NAME,
#endif
#if REMAP_INPUT_COUNT > 6
    REMAP_INPUT_6_NAME,
#endif
#if REMAP_INPUT_COUNT > 7
    REMAP_INPUT_7_NAME,
#endif
};

// ---------------------------------------------------------------------------
// Remap parameter storage (values set by host, read during perform)
// ---------------------------------------------------------------------------

static float _remap_param_values[REMAP_INPUT_COUNT] = {0};

// Internal buffers for remapped inputs (filled with param values each block)
#define _REMAP_MAX_BLOCK 8192
static float _remap_bufs[REMAP_INPUT_COUNT][_REMAP_MAX_BLOCK];

// ---------------------------------------------------------------------------
// Helper: is this gen~ input index remapped?
// ---------------------------------------------------------------------------

static inline int _remap_slot_for_gen_idx(int gen_idx) {
    for (int r = 0; r < REMAP_INPUT_COUNT; r++) {
        if (_remap_table[r].gen_idx == gen_idx) return r;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Perform with remapped inputs
// ---------------------------------------------------------------------------

static inline void _remap_perform(
    CommonState* state,
    float** ins, long numins,
    float** outs, long numouts, long n
) {
    // Fill remapped input buffers with their parameter values
    for (int r = 0; r < REMAP_INPUT_COUNT; r++) {
        float val = _remap_param_values[r];
        long clamp_n = (n < _REMAP_MAX_BLOCK) ? n : _REMAP_MAX_BLOCK;
        for (long s = 0; s < clamp_n; s++) {
            _remap_bufs[r][s] = val;
        }
    }

    // Build full input array for gen~
    float* full_ins[REMAP_GEN_TOTAL_INPUTS > 0 ? REMAP_GEN_TOTAL_INPUTS : 1];
    int audio_idx = 0;
    for (int i = 0; i < REMAP_GEN_TOTAL_INPUTS; i++) {
        int slot = _remap_slot_for_gen_idx(i);
        if (slot >= 0) {
            full_ins[i] = _remap_bufs[slot];
        } else {
            full_ins[i] = (audio_idx < numins && ins) ? ins[audio_idx] : _silence;
            audio_idx++;
        }
    }

    perform(state, (t_sample**)full_ins, REMAP_GEN_TOTAL_INPUTS,
            (t_sample**)outs, numouts, n);
}

// ---------------------------------------------------------------------------
// Param wrappers: intercept synthetic params for remapped inputs
// ---------------------------------------------------------------------------

static inline int _remap_total_params() {
    return num_params() + REMAP_INPUT_COUNT;
}

static inline bool _is_remap_param(int index) {
    return index >= num_params() && index < num_params() + REMAP_INPUT_COUNT;
}

static inline int _remap_slot_from_param(int index) {
    return index - num_params();
}

#endif // REMAP_INPUT_COUNT > 0
#endif // REMAP_INPUT_COUNT

#endif // GEN_REMAP_INPUTS_H
