// Route Csound's heap to SDRAM, leaving the platform's heap in SRAM.
//
// THE PROBLEM (spotykach QSPI build): the linker's default heap must stay in SRAM, because global
// constructors malloc before _hw.Init() powers up the SDRAM controller - a heap in SDRAM faults
// there. But Csound mallocs megabytes at csoundCreate/CompileCSD, far more than the ~270 KB SRAM
// heap holds.
//
// THE FIX: keep the default heap in SRAM (alt_qspi.lds), and route ONLY Csound's allocations to a
// dedicated SDRAM bump pool, armed by CsoundEngine::init() (which runs AFTER _hw.Init(), so SDRAM
// is live by then). We intercept the C malloc family via linker --wrap; when armed, allocations
// bump from SDRAM, otherwise they pass through to the real SRAM heap. Csound never frees its setup
// allocations (it lives for the whole session), so a bump allocator with no real free is correct.
//
// Built only into the ENGINE=csound target (see the Makefile csound branch's --wrap LDFLAGS).

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CSOUND_SDRAM_BSS
#define CSOUND_SDRAM_BSS __attribute__((section(".sdram_bss")))
#endif

extern "C" {
void* __real_malloc(size_t);
void  __real_free(void*);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
}

namespace {

constexpr size_t kPoolBytes = 12u * 1024u * 1024u;   // Csound setup is a few MB; SDRAM has room
alignas(16) CSOUND_SDRAM_BSS std::uint8_t g_pool[kPoolBytes];
std::size_t g_off   = 0;
bool        g_armed = false;

// 16-byte header before each block stores its size (for realloc copy); keeps payload 16-aligned.
struct Hdr { std::size_t size; std::size_t _pad; };

inline bool in_pool(const void* p) {
    return p >= static_cast<void*>(g_pool) && p < static_cast<void*>(g_pool + kPoolBytes);
}
inline void* pool_alloc(std::size_t n) {
    g_off = (g_off + 15u) & ~static_cast<std::size_t>(15);
    if (g_off + 16u + n > kPoolBytes) return __real_malloc(n);   // pool exhausted -> SRAM fallback
    Hdr* h        = reinterpret_cast<Hdr*>(g_pool + g_off);
    h->size       = n;
    void* payload = g_pool + g_off + 16u;
    g_off += 16u + ((n + 15u) & ~static_cast<std::size_t>(15));
    return payload;
}
inline std::size_t pool_size(void* p) {
    return reinterpret_cast<Hdr*>(static_cast<std::uint8_t*>(p) - 16u)->size;
}

} // namespace

namespace spotykach {
void csound_heap_arm() noexcept { g_armed = true; }   // call once, after _hw.Init(), before csoundCreate
}

extern "C" {

void* __wrap_malloc(std::size_t n) {
    return g_armed ? pool_alloc(n ? n : 1) : __real_malloc(n);
}

void* __wrap_calloc(std::size_t nmemb, std::size_t sz) {
    if (!g_armed) return __real_calloc(nmemb, sz);
    const std::size_t n = nmemb * sz;
    void* p = pool_alloc(n ? n : 1);
    if (in_pool(p)) std::memset(p, 0, n);
    return p;
}

void* __wrap_realloc(void* old, std::size_t n) {
    if (!old)          return __wrap_malloc(n);
    if (!in_pool(old)) return __real_realloc(old, n);   // a real (SRAM) block
    if (n == 0)        return nullptr;
    void*       np   = pool_alloc(n);
    std::size_t oldn = pool_size(old);
    if (in_pool(np)) std::memcpy(np, old, oldn < n ? oldn : n);
    return np;
}

void __wrap_free(void* p) {
    if (!p) return;
    if (in_pool(p)) return;                             // bump pool: no individual free
    __real_free(p);
}

} // extern "C"
