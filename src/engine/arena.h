// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <cstddef>

#include "core/engine_context.h" // EngineArena

namespace spotykach {

// A trivial bump allocator over the EngineArena the platform provides at init(). An engine carves
// its buffers from the arena (sequentially, with per-allocation alignment) instead of relying on a
// granular-shaped EngineBuffers - so the platform's SDRAM pool need not know any engine's buffer
// layout. alloc() returns nullptr if the arena is exhausted (the caller should assert/handle).
class Arena {
public:
    explicit Arena(const EngineArena& a)
    : _p(a.base), _end(a.base ? a.base + a.bytes : nullptr) {}

    template <class T>
    T* alloc(size_t count, size_t align = alignof(T))
    {
        if (!_p) return nullptr;
        uintptr_t p = reinterpret_cast<uintptr_t>(_p);
        p = (p + (align - 1)) & ~static_cast<uintptr_t>(align - 1);
        uint8_t* next = reinterpret_cast<uint8_t*>(p) + count * sizeof(T);
        if (next > _end) return nullptr;
        _p = next;
        return reinterpret_cast<T*>(p);
    }

    size_t remaining() const { return _p ? static_cast<size_t>(_end - _p) : 0; }

private:
    uint8_t* _p;
    uint8_t* _end;
};

};
