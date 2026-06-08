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
// Streaming rings for the tape engine: 1 MB each (~2.7 s of float-stereo read-ahead at 48 kHz) - a
// power of two as SpscRing requires - plus a 32 KB SD chunk scratch. ~2 MB on top of the 48 MB arena,
// well within the 64 MB SDRAM. Outside the engine arena so they never collide with engine buffers.
static constexpr size_t kStreamRingBytes = 1u * 1024u * 1024u;
static uint8_t DSY_SDRAM_BSS ALIGN32 _play_ring[kStreamRingBytes];
static uint8_t DSY_SDRAM_BSS ALIGN32 _record_ring[kStreamRingBytes];
static uint8_t DSY_SDRAM_BSS ALIGN32 _stream_scratch[Card::kChunk];

SDRAMBuffer::StreamMem SDRAMBuffer::streamMem() const
{
    return { _play_ring, kStreamRingBytes, _record_ring, kStreamRingBytes, _stream_scratch, Card::kChunk };
}
#endif
