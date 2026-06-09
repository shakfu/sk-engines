// genlib_arena.cpp - Arena-bound genlib runtime for the gen~ engine family.
//
// Adapted from gen-dsp's stock genlib_daisy.cpp: identical genlib C ABI
// (sysmem_*, set_zero64, genlib data objects, buffer/state stubs) EXCEPT the
// allocator backend, which is a bump allocator over the platform's
// EngineContext SDRAM arena instead of a private malloc'd SRAM pool + static
// 64MB SDRAM pool. The engine calls genlib_arena_bind(base, bytes) at init()
// before wrapper_create(); all gen~ allocations (state, delay lines, [data])
// then land in the arena.
//
// Compiled per-engine with that engine's gen/ + gen/gen_dsp include paths.

#include "engine/gen/genlib_arena.h"

// libDaisy/this project may define ARM_MATH_CM7, which makes genlib_platform.h
// enable GENLIB_USE_ARMMATH / GENLIB_USE_FASTMATH (arm_sqrtf, fasterpow, ...)
// that are not available here. Undef before including genlib so it falls back
// to standard <cmath>.
#ifdef ARM_MATH_CM7
#undef ARM_MATH_CM7
#endif
#ifdef ARM_MATH_CM4
#undef ARM_MATH_CM4
#endif

#include "genlib.h"
#include "genlib_exportfunctions.h"

#include <cstring>
#include <cstdlib>

#ifndef MSP_ON_CLANG
#include <cmath>
#endif

// No filesystem on the target: JSON state save/restore is disabled via the
// build's -DGENLIB_NO_JSON.

// DATA_MAXIMUM_ELEMENTS * sizeof(t_sample) = max [data]/[buffer] size
#define DATA_MAXIMUM_ELEMENTS (33554432)

// ---------------------------------------------------------------------------
// Bump allocator bound to the platform SDRAM arena
// ---------------------------------------------------------------------------

static unsigned char* g_arena_base = nullptr;
static size_t         g_arena_size = 0;
static size_t         g_arena_off  = 0;

extern "C" void genlib_arena_bind(void* base, size_t bytes) {
    g_arena_base = static_cast<unsigned char*>(base);
    g_arena_size = base ? bytes : 0;
    g_arena_off  = 0;
}

// Align to 8 bytes for ARM Cortex-M7 (double-word aligned).
static inline size_t align8(size_t n) {
    return (n + 7) & ~(size_t)7;
}

static void* gen_arena_allocate(size_t size) {
    size = align8(size);
    if (!g_arena_base) return nullptr;
    if (g_arena_off + size > g_arena_size) return nullptr;  // arena exhausted
    void* ptr = g_arena_base + g_arena_off;
    g_arena_off += size;
    return ptr;
}

// ---------------------------------------------------------------------------
// Memory allocation functions (names match genlib_exportfunctions.h)
// genlib.h macros: genlib_sysmem_newptr(s) -> sysmem_newptr(s), etc.
// ---------------------------------------------------------------------------

t_ptr sysmem_newptr(t_ptr_size size) {
    return (t_ptr)gen_arena_allocate((size_t)size);
}

t_ptr sysmem_newptrclear(t_ptr_size size) {
    t_ptr p = (t_ptr)gen_arena_allocate((size_t)size);
    if (p) {
        memset(p, 0, (size_t)size);
    }
    return p;
}

t_ptr sysmem_resizeptr(void* ptr, t_ptr_size newsize) {
    // Allocate new block (old block is wasted -- bump allocator).
    (void)ptr;
    return (t_ptr)gen_arena_allocate((size_t)newsize);
}

t_ptr sysmem_resizeptrclear(void* ptr, t_ptr_size newsize) {
    (void)ptr;
    t_ptr p = (t_ptr)gen_arena_allocate((size_t)newsize);
    if (p) {
        memset(p, 0, (size_t)newsize);
    }
    return p;
}

t_ptr_size sysmem_ptrsize(void* ptr) {
    // Cannot determine size from bump allocator.
    (void)ptr;
    return 0;
}

void sysmem_freeptr(void* ptr) {
    // No-op: bump allocator does not free individual allocations.
    (void)ptr;
}

void sysmem_copyptr(const void* src, void* dst, t_ptr_size bytes) {
    memcpy(dst, src, (size_t)bytes);
}

// ---------------------------------------------------------------------------
// Utility functions (names match genlib_exportfunctions.h)
// ---------------------------------------------------------------------------

void set_zero64(t_sample* memory, long size) {
    for (long i = 0; i < size; i++) {
        memory[i] = 0;
    }
}

void genlib_report_error(const char* s) {
    (void)s;
}

void genlib_report_message(const char* s) {
    (void)s;
}

unsigned long systime_ticks(void) {
    return 0;
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------

t_sample gen_msp_pow(t_sample value, t_sample power) {
    return (t_sample)powf((float)value, (float)power);
}

// ---------------------------------------------------------------------------
// String/reference stubs (no Max runtime on embedded)
// ---------------------------------------------------------------------------

void* genlib_obtain_reference_from_string(const char* name) {
    (void)name;
    return 0;
}

char* genlib_reference_getname(void* ref) {
    (void)ref;
    return 0;
}

// ---------------------------------------------------------------------------
// Buffer stubs (no Max buffer~ on embedded)
// ---------------------------------------------------------------------------

t_genlib_buffer* genlib_obtain_buffer_from_reference(void* ref) {
    (void)ref;
    return 0;
}

t_genlib_err genlib_buffer_edit_begin(t_genlib_buffer* b) {
    (void)b;
    return 0;
}

t_genlib_err genlib_buffer_edit_end(t_genlib_buffer* b, long valid) {
    (void)b; (void)valid;
    return 0;
}

t_genlib_err genlib_buffer_getinfo(t_genlib_buffer* b, t_genlib_buffer_info* info) {
    (void)b; (void)info;
    return 0;
}

void genlib_buffer_dirty(t_genlib_buffer* b) {
    (void)b;
}

t_genlib_err genlib_buffer_perform_begin(t_genlib_buffer* b) {
    (void)b;
    return 0;
}

void genlib_buffer_perform_end(t_genlib_buffer* b) {
    (void)b;
}

// ---------------------------------------------------------------------------
// Data object support (for gen~ delay lines, etc.)
// t_genlib_data is opaque; we define the actual struct here (same as genlib.cpp)
// ---------------------------------------------------------------------------

typedef struct {
    t_genlib_data_info info;
    t_sample cursor;
} t_dsp_gen_data;

void genlib_data_setbuffer(t_genlib_data* b, void* ref) {
    (void)b; (void)ref;
    genlib_report_error("not supported for export targets\n");
}

t_genlib_data* genlib_obtain_data_from_reference(void* ref) {
    (void)ref;
    t_dsp_gen_data* self = (t_dsp_gen_data*)sysmem_newptrclear(sizeof(t_dsp_gen_data));
    self->info.dim = 0;
    self->info.channels = 0;
    self->info.data = 0;
    self->cursor = 0;
    return (t_genlib_data*)self;
}

t_genlib_err genlib_data_getinfo(t_genlib_data* b, t_genlib_data_info* info) {
    t_dsp_gen_data* self = (t_dsp_gen_data*)b;
    info->dim = self->info.dim;
    info->channels = self->info.channels;
    info->data = self->info.data;
    return GENLIB_ERR_NONE;
}

void genlib_data_release(t_genlib_data* b) {
    // No-op on bump allocator (cannot free individual allocations).
    (void)b;
}

long genlib_data_getcursor(t_genlib_data* b) {
    t_dsp_gen_data* self = (t_dsp_gen_data*)b;
    return long(self->cursor);
}

void genlib_data_setcursor(t_genlib_data* b, long cursor) {
    t_dsp_gen_data* self = (t_dsp_gen_data*)b;
    self->cursor = t_sample(cursor);
}

void genlib_data_resize(t_genlib_data* b, long s, long c) {
    t_dsp_gen_data* self = (t_dsp_gen_data*)b;

    t_sample* old = self->info.data;
    int olddim = self->info.dim;
    int oldchannels = self->info.channels;

    // Limit data size
    if (s * c > DATA_MAXIMUM_ELEMENTS) {
        s = DATA_MAXIMUM_ELEMENTS / c;
    }

    size_t sz = sizeof(t_sample) * s * c;
    size_t oldsz = sizeof(t_sample) * olddim * oldchannels;

    if (old && sz == oldsz) {
        // Same size, just re-zero and update dims
        if (s > olddim) {
            self->info.channels = c;
            self->info.dim = s;
        } else {
            self->info.dim = s;
            self->info.channels = c;
        }
        set_zero64(self->info.data, s * c);
        return;
    }

    // Allocate new
    t_sample* replaced = (t_sample*)sysmem_newptr(sz);
    if (replaced == 0) {
        genlib_report_error("allocating [data]: out of memory");
        if (s > 512 || c > 1) {
            genlib_data_resize((t_genlib_data*)self, 512, 1);
        } else {
            genlib_data_resize((t_genlib_data*)self, 4, 1);
        }
        return;
    }

    // Fill with zeroes
    set_zero64(replaced, s * c);

    // Copy old data
    if (old) {
        int copydim = olddim > s ? s : olddim;
        if (c == oldchannels) {
            size_t copysz = sizeof(t_sample) * copydim * c;
            memcpy(replaced, old, copysz);
        } else {
            int copychannels = oldchannels > c ? c : oldchannels;
            for (int i = 0; i < copydim; i++) {
                for (int j = 0; j < copychannels; j++) {
                    replaced[j + i * c] = old[j + i * oldchannels];
                }
            }
        }
    }

    // Update info (order matters for thread safety)
    if (old == 0) {
        self->info.data = replaced;
        self->info.dim = s;
        self->info.channels = c;
    } else {
        if (oldsz > sz) {
            if (s > olddim) {
                self->info.channels = c;
                self->info.dim = s;
            } else {
                self->info.dim = s;
                self->info.channels = c;
            }
            self->info.data = replaced;
        } else {
            self->info.data = replaced;
            if (s > olddim) {
                self->info.channels = c;
                self->info.dim = s;
            } else {
                self->info.dim = s;
                self->info.channels = c;
            }
        }
        // Old pointer is wasted (bump allocator cannot free)
    }
}

// ---------------------------------------------------------------------------
// Reset / state
// ---------------------------------------------------------------------------

void genlib_reset_complete(void* data) {
    (void)data;
}

// State save/restore (stubbed for embedded -- no JSON on the target)
size_t genlib_getstatesize(CommonState* cself, getparameter_method getmethod) {
    (void)cself; (void)getmethod;
    return 0;
}

short genlib_getstate(CommonState* cself, char* state, getparameter_method getmethod) {
    (void)cself; (void)state; (void)getmethod;
    return 0;
}

short genlib_setstate(CommonState* cself, const char* state, setparameter_method setmethod) {
    (void)cself; (void)state; (void)setmethod;
    return 0;
}
