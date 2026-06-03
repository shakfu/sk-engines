#include "buffer.sdram.h"

using namespace spotykach;

// Main //
static constexpr size_t kSourceBufferLength = kSourceMaxSeconds * kSampleRate;

#define ALIGN32K __attribute__((aligned(32768)))
inline constexpr size_t aligned(size_t in)
{
    return in + (32768 - in % 32768);
}

static Buffer::Frame DSY_SDRAM_BSS ALIGN32K _srcBuf1[aligned(kSourceBufferLength)];
static Buffer::Frame DSY_SDRAM_BSS ALIGN32K _srcBuf2[aligned(kSourceBufferLength)];

// Delay //
static float DSY_SDRAM_BSS ALIGN32K _dlyBuf1L[aligned(Fx::kEchoDelayBufferLength)];
static float DSY_SDRAM_BSS ALIGN32K _dlyBuf1R[aligned(Fx::kEchoDelayBufferLength)];

static float DSY_SDRAM_BSS ALIGN32K _dlyBuf2L[aligned(Fx::kEchoDelayBufferLength)];
static float DSY_SDRAM_BSS ALIGN32K _dlyBuf2R[aligned(Fx::kEchoDelayBufferLength)];

static constexpr size_t kDelayBufsCount = kDecksCount * kBuffersPerDeckCount * kChannelsCount;
static float* _delayBufs[kDelayBufsCount] = {
    _dlyBuf1L,
    _dlyBuf1R,

    _dlyBuf2L,
    _dlyBuf2R
};

// Detector //
static float DSY_SDRAM_BSS ALIGN32K _detectBuf1L[aligned(Detector::kWindow)];
static float DSY_SDRAM_BSS ALIGN32K _detectBuf1R[aligned(Detector::kWindow)];

static float DSY_SDRAM_BSS ALIGN32K _detectBuf2L[aligned(Detector::kWindow)];
static float DSY_SDRAM_BSS ALIGN32K _detectBuf2R[aligned(Detector::kWindow)];

static constexpr size_t kDetectorBufsCount = kDecksCount * kBuffersPerDeckCount * kChannelsCount;
static float* _detectorBufs[kDetectorBufsCount] = {
    _detectBuf1L,
    _detectBuf1R,
    _detectBuf2L,
    _detectBuf2R
};

// Undo //
static Buffer::Frame DSY_SDRAM_BSS ALIGN32K _srcBuf3[aligned(kSourceBufferLength)];

static constexpr size_t kSrcBufsCount = (kDecksCount + 1) * kBuffersPerDeckCount;
static Buffer::Frame* _srcBufs[kSrcBufsCount] = {
    _srcBuf1,
    _srcBuf2,
    _srcBuf3
};

// Slices //
#define ALIGN32 __attribute__((aligned(32)))
static size_t DSY_SDRAM_BSS ALIGN32 _slices_a[kMaxSlicePointCount];
static size_t DSY_SDRAM_BSS ALIGN32 _slices_b[kMaxSlicePointCount];

// Sequence //
static Event DSY_SDRAM_BSS ALIGN32 _trackBufA[Track::kLength];
static Event DSY_SDRAM_BSS ALIGN32 _trackBufB[Track::kLength];

// Card //
static uint8_t DSY_SDRAM_BSS ALIGN32 _card_buffer[Card::kChunk];

SDRAMBuffer::SDRAMBuffer() 
{
    auto srcbs = kSourceBufferLength * sizeof(Buffer::Frame);
    for (size_t i = 0; i < kSrcBufsCount; i++) {
        std::memset(_srcBufs[i], 0, srcbs);
    }
};

Buffer::Frame* SDRAMBuffer::sourceBuffer() 
{
    assert(_providedSourceBufCount < kSrcBufsCount);
    return _srcBufs[_providedSourceBufCount++];
};

size_t SDRAMBuffer::sourceBufferSize() 
{
    return kSourceBufferLength;
}

float* SDRAMBuffer::detectorBuffer() 
{
    assert(_providedDetectorBufCount < kDetectorBufsCount);
    return _detectorBufs[_providedDetectorBufCount++];
};

float* SDRAMBuffer::delayBuffer() 
{
    assert(_providedDelayBufCount < kDelayBufsCount);
    return _delayBufs[_providedDelayBufCount++];
};

uint8_t* SDRAMBuffer::card_buffer() const
{
    return _card_buffer;
}

EngineArena SDRAMBuffer::engineArena()
{
    // Reuse the unused third source buffer (~16 MB) as the engine arena (Stage 1).
    return { reinterpret_cast<uint8_t*>(_srcBuf3), sizeof(_srcBuf3) };
}

Event* SDRAMBuffer::track_buffer_a() const 
{ 
    return _trackBufA; 
}

Event* SDRAMBuffer::track_buffer_b() const 
{ 
    return _trackBufB; 
}

size_t* SDRAMBuffer::slices_a() const
{
    return _slices_a;
}

size_t* SDRAMBuffer::slices_b() const
{
    return _slices_b;
}

