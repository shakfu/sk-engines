// Route ChucK's heap to SDRAM, leaving the platform's heap in SRAM.
//
// Identical strategy to csound_alloc.cpp (see its header comment for the full rationale): the
// linker's default heap must stay in SRAM, because global constructors malloc before _hw.Init()
// powers up the SDRAM controller - a heap in SDRAM faults there. But ChucK mallocs megabytes at
// `new ChucK()` / init() / compileCode(). So we keep the default heap in SRAM (alt_qspi.lds) and
// route ONLY ChucK's allocations to a dedicated SDRAM pool, armed by ChuckEngine::init() (which runs
// AFTER _hw.Init(), so SDRAM is live). We intercept the C malloc family via linker --wrap; when armed,
// allocations come from the SDRAM pool, otherwise they pass through to the real SRAM heap.
//
// The pool is the same free-capable coalescing allocator the Csound engine uses (csound_pool.h - it
// is engine-agnostic, depending only on the C++ stdlib). ChucK is STL/heap-heavy and the eventual M3
// live-recompile (removeAllShreds + compileCode) churns the pool, so a bump pool would leak; this one
// coalesces on free. (Roadmap: promote csound_pool.h to a shared SdramPool; reused as-is here to keep
// the proven Csound build byte-identical.) On pool exhaustion we fall back to the real SRAM heap so a
// request never hard-fails; in_pool() then routes each pointer's free/realloc back to its heap.
//
// Built only into the ENGINE=chuck target (see the Makefile chuck branch's --wrap LDFLAGS).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/csound/csound_pool.h"

#ifndef CHUCK_SDRAM_BSS
#define CHUCK_SDRAM_BSS __attribute__((section(".sdram_bss")))
#endif

extern "C" {
void* __real_malloc(size_t);
void  __real_free(void*);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
}

namespace {

constexpr size_t kPoolBytes = 12u * 1024u * 1024u;   // ChucK setup is a few MB; SDRAM has room
alignas(16) CHUCK_SDRAM_BSS std::uint8_t g_pool[kPoolBytes];
spotykach::CsoundPool g_alloc;                        // the shared, engine-agnostic SDRAM allocator
bool g_armed = false;

inline bool in_pool(const void* p) {
    return p >= static_cast<const void*>(g_pool) && p < static_cast<const void*>(g_pool + kPoolBytes);
}

} // namespace

namespace spotykach {
// Lay down the free pool over the SDRAM array and arm interception. Call once, after _hw.Init()
// (SDRAM live), before `new ChucK()`.
void chuck_heap_arm() noexcept {
    g_alloc.init(g_pool, kPoolBytes);
    g_armed = true;
}
}

extern "C" {

void* __wrap_malloc(std::size_t n) {
    if (!g_armed) return __real_malloc(n);
    void* p = g_alloc.alloc(n);
    return p ? p : __real_malloc(n);            // pool exhausted -> SRAM fallback
}

void* __wrap_calloc(std::size_t nmemb, std::size_t sz) {
    if (!g_armed) return __real_calloc(nmemb, sz);
    const std::size_t n = nmemb * sz;
    void* p = g_alloc.alloc(n ? n : 1);
    if (p) { std::memset(p, 0, n); return p; }
    return __real_calloc(nmemb, sz);            // fallback zeroes for us
}

void* __wrap_realloc(void* old, std::size_t n) {
    if (!old)          return __wrap_malloc(n);
    if (!in_pool(old)) return __real_realloc(old, n);   // a real (SRAM) block
    if (n == 0)        { g_alloc.release(old); return nullptr; }
    void* p = g_alloc.grow(old, n);
    if (p) return p;
    // Pool can't satisfy the grow (even after coalescing): relocate to the SRAM heap, then free the
    // old pool block. realloc must preserve contents up to the smaller of old/new size.
    p = __real_malloc(n);
    if (p) {
        const std::size_t oldn = g_alloc.payload(old);
        std::memcpy(p, old, oldn < n ? oldn : n);
        g_alloc.release(old);
    }
    return p;                                   // null leaves old intact (realloc contract)
}

void __wrap_free(void* p) {
    if (!p) return;
    if (in_pool(p)) g_alloc.release(p);
    else            __real_free(p);
}

} // extern "C"
