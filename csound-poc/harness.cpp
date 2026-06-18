// Thin QSPI harness that runs CsoundEngine through the real IEngine interface (vs app.cpp, which
// calls the Csound C API directly). It stands in for the spotykach platform: builds an
// EngineContext, init()s the engine, drives process() from the audio callback and set_param() from
// the Pod's two knobs. This is the minimal proof that CsoundEngine works behind the contract.
//
// Build (from csound-poc/):  make            (Makefile targets the harness)
// Flash:                     while ! make program-dfu; do sleep 0.2; done   (then tap RESET)

#include "daisy_seed.h"
#include "engine/csound/csound_engine.h"

using namespace daisy;
using namespace daisy::seed;

static const int kBlock = 256;   // Csound-friendly block; becomes ksmps inside the engine

DaisySeed               hw;
spotykach::CsoundEngine engine;

// Pod knob 1 -> D21, knob 2 -> D15.
static const int kNumKnobs            = 2;
static const Pin kKnobPins[kNumKnobs] = { D21, D15 };

// Daisy's non-interleaving buffers are already de-interleaved (InputBuffer = const float* const*,
// OutputBuffer = float**), which is exactly IEngine::process's shape - forward straight through.
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    engine.process(in, out, size);
}

int main(void)
{
    // Defensive: point the vector table at our QSPI app. A no-op if the bootloader already did
    // it (v5.4), but it makes this image bootloader-agnostic - needed to test whether the
    // spotykach v2 bootloader (which may not set VTOR) can run a QSPI app at all.
    SCB->VTOR = 0x90040000;
    __DSB();
    __ISB();

    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(kBlock);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    AdcChannelConfig adc[kNumKnobs];
    for (int i = 0; i < kNumKnobs; i++)
        adc[i].InitSingle(kKnobPins[i]);
    hw.adc.Init(adc, kNumKnobs);
    hw.adc.Start();

    // Build the context the platform would normally inject. CsoundEngine reads sample_rate and
    // block_size; its heap comes from the QSPI linker script, so the arena and the service pointers
    // are left empty/null here (a real platform would populate them).
    spotykach::EngineContext ctx{};
    ctx.sample_rate = 48000.f;
    ctx.block_size  = static_cast<float>(kBlock);
    ctx.arena       = { nullptr, 0 };
    ctx.time        = nullptr;
    ctx.transport   = nullptr;
    ctx.stream      = nullptr;
    ctx.qspi        = nullptr;
    engine.init(ctx);

    hw.StartAudio(AudioCallback);

    using spotykach::DeckRef;
    using spotykach::ParamId;
    while (1) {
        engine.set_param(ParamId::Speed, DeckRef::A, hw.adc.GetFloat(0));   // knob1 -> pitch
        engine.set_param(ParamId::Mix,   DeckRef::A, hw.adc.GetFloat(1));   // knob2 -> level
        engine.prepare();
    }
}
