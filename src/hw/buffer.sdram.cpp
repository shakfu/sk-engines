#include "buffer.sdram.h"

using namespace spotykach;

// Engine arena: one SDRAM block the active engine sub-allocates. Sized as a raw byte budget large
// enough for the heaviest engine (granular needs ~35 MB: 2x source + delay + detector + slices +
// track), with margin; the pool does NOT compute this from any engine's constants (decoupled). The
// 32 KB alignment matches the granular buffers' former alignment so sub-allocations can preserve it.
static constexpr size_t kEngineArenaBytes = 48u * 1024u * 1024u; // 48 MB (of 64 MB SDRAM)

#define ALIGN32K __attribute__((aligned(32768)))
#define ALIGN32  __attribute__((aligned(32)))

static uint8_t DSY_SDRAM_BSS ALIGN32K _arena[kEngineArenaBytes];
static uint8_t DSY_SDRAM_BSS ALIGN32  _card_buffer[Card::kChunk];

EngineArena SDRAMBuffer::engineArena()
{
    return { _arena, kEngineArenaBytes };
}

uint8_t* SDRAMBuffer::card_buffer() const
{
    return _card_buffer;
}
