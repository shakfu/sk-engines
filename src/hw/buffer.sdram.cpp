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

#if defined(SPK_ENGINE_TAPE)
// Streaming rings for the tape engine: one ring PER DECK, 1 MB each (~5.5 s of mono read-ahead at
// 48 kHz, or ~2.7 s stereo) - a power of two as SpscRing requires - plus a 32 KB SD chunk scratch
// shared by both decks (the main-loop pump services them sequentially). ~2 MB on top of the 48 MB
// arena, well within the 64 MB SDRAM. Outside the engine arena so they never collide with engine buffers.
static constexpr size_t kStreamRingBytes = 1u * 1024u * 1024u;
static uint8_t DSY_SDRAM_BSS ALIGN32 _ring_a[kStreamRingBytes];
static uint8_t DSY_SDRAM_BSS ALIGN32 _ring_b[kStreamRingBytes];
static uint8_t DSY_SDRAM_BSS ALIGN32 _stream_scratch[Card::kChunk];

SDRAMBuffer::StreamMem SDRAMBuffer::streamMem() const
{
    return { _ring_a, kStreamRingBytes, _ring_b, kStreamRingBytes, _stream_scratch, Card::kChunk };
}
#endif
