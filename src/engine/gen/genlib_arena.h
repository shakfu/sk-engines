// genlib_arena.h - Arena-bound genlib runtime for the gen~ engine family.
//
// Shared across all gen-dsp-generated engines (one per binary). Replaces
// gen-dsp's stock genlib_daisy.cpp two-tier malloc/SDRAM allocator with a
// bump allocator bound to the platform's EngineContext SDRAM arena, so gen~
// state lives in the same arena every other sk-engines engine uses.
//
// This header is genlib-free and safe to include from platform TUs: it only
// declares the C ABI used to point the runtime at the arena before
// wrapper_create() is called.

#ifndef GEN_GENLIB_ARENA_H
#define GEN_GENLIB_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bind the genlib bump allocator to a region of the platform SDRAM arena.
// Must be called before any genlib allocation (i.e. before wrapper_create()).
// Resets the bump offset to zero: a fresh bind reclaims the whole region.
void genlib_arena_bind(void* base, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif // GEN_GENLIB_ARENA_H
