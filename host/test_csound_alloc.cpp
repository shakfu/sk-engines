// Host test for the Csound SDRAM allocator (CsoundPool, src/engine/csound/csound_pool.h). The pool
// is the one Csound component with no libcsound dependency, so it is exercised here off-target,
// against a small backing buffer, to the project's test standard. It is roadmap #2 in
// docs/dev/csound.md: a free-capable allocator that reclaims on free/realloc (the old bump pool did
// not), which is what makes csoundReset + recompile patch-swapping sustainable.
//
// The hard properties this proves: 16-byte alignment, splitting, full coalescing on free (forward +
// backward), in-place grow/shrink, exhaustion -> nullptr (the shim's SRAM-fallback signal), and -
// the whole point - that freeing everything returns the pool to a single max-size block (no leak,
// no fragmentation residue), validated by the boundary-tag invariant walk after every step.
// Build: `make -C host test-csound-alloc`.

#include "engine/csound/csound_pool.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

// A 16-aligned backing buffer for the pool (the real .sdram_bss array is alignas(16)).
struct Arena {
    static constexpr std::size_t kBytes = 1u << 16;   // 64 KiB
    alignas(16) std::uint8_t buf[kBytes];
    CsoundPool pool;
    Arena() { pool.init(buf, kBytes); }
};

bool aligned16(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) & 15u) == 0; }

// Fill a payload with a size-keyed byte ramp so a later compare catches any corruption/overlap.
void paint(void* p, std::size_t n, std::uint8_t seed) {
    auto* b = static_cast<std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; i++) b[i] = static_cast<std::uint8_t>(seed + i * 7u);
}
bool painted(const void* p, std::size_t n, std::uint8_t seed) {
    auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; i++)
        if (b[i] != static_cast<std::uint8_t>(seed + i * 7u)) return false;
    return true;
}

// ------------------------------------------------------------------------------------------------
void test_basic_alloc_align_validate() {
    std::printf("alloc / alignment / invariants\n");
    Arena a;
    check(a.pool.validate(), "fresh pool validates");
    check(a.pool.used_bytes() == 0, "fresh pool has nothing used");

    void* p1 = a.pool.alloc(100);
    void* p2 = a.pool.alloc(1);          // tiny -> rounded to the minimum block
    void* p3 = a.pool.alloc(4096);
    check(p1 && p2 && p3, "three allocations succeed");
    check(aligned16(p1) && aligned16(p2) && aligned16(p3), "every payload is 16-aligned");
    check(p1 != p2 && p2 != p3 && p1 != p3, "allocations are distinct");
    check(a.pool.validate(), "pool validates after allocations");
    check(a.pool.used_bytes() > 0, "used grew");

    paint(p1, 100, 0x11); paint(p3, 4096, 0x33);
    check(painted(p1, 100, 0x11) && painted(p3, 4096, 0x33), "payloads independent / not clobbered");

    a.pool.release(p1); a.pool.release(p2); a.pool.release(p3);
    check(a.pool.validate(), "pool validates after frees");
    check(a.pool.used_bytes() == 0, "everything reclaimed");
}

void test_zero_and_alloc_zero() {
    std::printf("alloc(0)\n");
    Arena a;
    void* p = a.pool.alloc(0);            // C malloc(0) is impl-defined; we hand back a usable block
    check(p != nullptr && aligned16(p), "alloc(0) returns a 16-aligned block");
    check(a.pool.validate(), "validates after alloc(0)");
    a.pool.release(p);
    check(a.pool.used_bytes() == 0, "alloc(0) block reclaimed");
}

void test_full_reclamation() {
    std::printf("full reclamation (the bump pool could not do this)\n");
    Arena a;
    const std::size_t empty_largest = a.pool.largest_free();
    check(empty_largest >= Arena::kBytes - 64, "fresh pool offers ~the whole arena as one block");

    // Many small allocations, then free them all: a bump pool would have leaked every one.
    std::vector<void*> ptrs;
    for (int i = 0; i < 200; i++) {
        void* p = a.pool.alloc(64 + (i % 7) * 16);
        check(p != nullptr, "small alloc in the fill loop");
        ptrs.push_back(p);
    }
    check(a.pool.validate(), "validates while full of small blocks");
    for (void* p : ptrs) a.pool.release(p);

    check(a.pool.validate(), "validates after releasing all");
    check(a.pool.used_bytes() == 0, "all 200 blocks reclaimed");
    check(a.pool.largest_free() == empty_largest, "coalesced back to one max block (no fragmentation residue)");
}

void test_coalesce_directions() {
    std::printf("coalesce forward + backward\n");
    Arena a;
    void* p1 = a.pool.alloc(1000);
    void* p2 = a.pool.alloc(1000);
    void* p3 = a.pool.alloc(1000);
    void* tail = a.pool.alloc(1000);     // pin the tail so p1..p3 are interior (real neighbours both sides)
    check(p1 && p2 && p3 && tail, "four adjacent blocks");

    // Free the outer two first, then the middle: the middle's release must coalesce BOTH neighbours.
    a.pool.release(p1);
    a.pool.release(p3);
    check(a.pool.validate(), "validates with a hole on each side of p2");
    a.pool.release(p2);
    check(a.pool.validate(), "validates after the three-way merge");

    // The merged free region must now satisfy a single allocation bigger than any one of them.
    void* big = a.pool.alloc(2800);
    check(big != nullptr, "merged region serves an alloc larger than any single freed block");
    check(a.pool.validate(), "validates after reusing the merged hole");
    a.pool.release(big);
    a.pool.release(tail);
    check(a.pool.used_bytes() == 0, "all reclaimed");
}

void test_realloc_grow_shrink_inplace() {
    std::printf("realloc grow/shrink in place\n");
    Arena a;
    void* p = a.pool.alloc(64);
    paint(p, 64, 0x5a);
    a.pool.alloc(64);                    // (intentionally leaked within the test) wall after p

    void* shrunk = a.pool.grow(p, 32);   // shrink 64->32: stays in place, splits off a free tail
    check(shrunk == p, "shrink keeps the same pointer");
    check(painted(shrunk, 32, 0x5a), "shrink preserves contents");
    check(a.pool.validate(), "validates after shrink");

    // Grow back into exactly the tail we just split off (64-byte payload again fits in p+tail, the
    // forward neighbour is free) -> stays in place. (A larger grow would hit the wall and relocate;
    // that case is covered in test_realloc_relocates_and_preserves.)
    void* grown = a.pool.grow(p, 64);
    check(grown == p, "in-place grow into the adjacent free tail keeps the pointer");
    check(painted(grown, 32, 0x5a), "in-place grow preserves the original bytes");
    check(a.pool.validate(), "validates after in-place grow");
}

void test_realloc_relocates_and_preserves() {
    std::printf("realloc relocates when it cannot grow in place\n");
    Arena a;
    void* p   = a.pool.alloc(128);
    void* wall = a.pool.alloc(64);       // immediately after p -> blocks in-place growth
    paint(p, 128, 0xa5);
    check(p && wall, "block plus a wall behind it");

    void* moved = a.pool.grow(p, 4096);  // can't extend into `wall` -> must relocate + copy
    check(moved != nullptr, "relocating realloc succeeds");
    check(moved != p, "realloc moved the block");
    check(painted(moved, 128, 0xa5), "relocated realloc preserved the original 128 bytes");
    check(a.pool.validate(), "validates after relocation");
}

void test_exhaustion_returns_null() {
    std::printf("exhaustion -> nullptr (the SRAM-fallback signal)\n");
    Arena a;
    check(a.pool.alloc(Arena::kBytes * 2) == nullptr, "an over-capacity request returns null, not garbage");
    check(a.pool.validate(), "a failed alloc leaves the pool intact");

    // Drain the pool with one big block, confirm the next request fails, then free and recover.
    void* big = a.pool.alloc(Arena::kBytes - 256);
    check(big != nullptr, "one near-full block");
    check(a.pool.alloc(2048) == nullptr, "no room left -> null");
    a.pool.release(big);
    check(a.pool.validate(), "validates after recovering");
    void* again = a.pool.alloc(Arena::kBytes - 256);
    check(again != nullptr, "the freed space is allocatable again");
    a.pool.release(again);
}

// Deterministic churn: a pseudo-random alloc/free/realloc mix, validating the invariant every step,
// then a full drain that must return to a single max block. Models repeated patch compile/reset.
void test_churn_then_full_drain() {
    std::printf("randomized churn, validate every step, then full drain\n");
    Arena a;
    const std::size_t empty_largest = a.pool.largest_free();

    std::vector<std::pair<void*, std::size_t>> live;
    std::uint32_t rng = 0xC0FFEEu;
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return rng; };

    for (int step = 0; step < 4000; step++) {
        const int op = rnd() % 3;
        if (op == 0 || live.size() < 4) {                 // alloc
            const std::size_t n = 8 + (rnd() % 800);
            if (void* p = a.pool.alloc(n)) {
                paint(p, n, static_cast<std::uint8_t>(rnd()));
                live.emplace_back(p, n);
            }
        } else if (op == 1) {                             // free a random live block
            const std::size_t i = rnd() % live.size();
            a.pool.release(live[i].first);
            live[i] = live.back();
            live.pop_back();
        } else {                                          // realloc a random live block
            const std::size_t i = rnd() % live.size();
            const std::size_t n = 8 + (rnd() % 1200);
            if (void* p = a.pool.grow(live[i].first, n)) {
                // Contents up to min(old,n) are preserved; just re-paint to keep the model honest.
                paint(p, n, static_cast<std::uint8_t>(rnd()));
                live[i] = {p, n};
            }
        }
        if (!a.pool.validate()) { check(false, "invariant held during churn"); break; }
    }

    for (auto& e : live) a.pool.release(e.first);
    check(a.pool.validate(), "validates after draining the churn");
    check(a.pool.used_bytes() == 0, "churn fully reclaimed");
    check(a.pool.largest_free() == empty_largest, "churn coalesced back to one max block");
}

} // namespace

int main() {
    std::printf("=== CsoundPool (Csound SDRAM allocator) ===\n");
    test_basic_alloc_align_validate();
    test_zero_and_alloc_zero();
    test_full_reclamation();
    test_coalesce_directions();
    test_realloc_grow_shrink_inplace();
    test_realloc_relocates_and_preserves();
    test_exhaustion_returns_null();
    test_churn_then_full_drain();

    if (g_failures) { std::printf("\nFAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("\nAll CsoundPool tests passed.\n");
    return 0;
}
