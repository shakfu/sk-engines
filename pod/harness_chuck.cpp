// Thin QSPI harness that runs ChuckEngine through the real IEngine interface - the M1 vehicle from
// docs/dev/chuck-impl.md ("Pod tone from QSPI"). It stands in for the spotykach platform: builds an
// EngineContext, init()s the engine, drives process() from the audio callback and set_param() from
// the Pod's two knobs. This is the minimal proof that ChuckEngine makes sound from QSPI flash.
//
// Unlike the Csound harness, this one links the REAL SDRAM pool (chuck_alloc.cpp + --wrap), so
// ChuckEngine::init()'s chuck_heap_arm() arms the .sdram_bss pool - exactly the M2 spotykach heap
// model, exercised on the quick-iterate Pod. The platform heap stays in SRAM (alt_qspi.lds).
//
// Build (from pod/):  make -f Makefile.chuck
// Flash:              while ! make -f Makefile.chuck program-dfu; do sleep 0.2; done   (then tap RESET)
//
// Prereq (once): build libchuck.a with scripts/fetch_chuck.sh.

#include "daisy_seed.h"
#include "engine/chuck/chuck_engine.h"

using namespace daisy;
using namespace daisy::seed;

static const int kBlock = 256;   // matches ChuckEngine::kMaxBlock; == run() numFrames per callback

DaisySeed              hw;
spotykach::ChuckEngine engine;

// Pod knob 1 -> D21, knob 2 -> D15.
static const int kNumKnobs            = 2;
static const Pin kKnobPins[kNumKnobs] = { D21, D15 };

// Daisy's non-interleaving buffers are already de-interleaved (InputBuffer = const float* const*,
// OutputBuffer = float**), which is exactly IEngine::process's shape - forward straight through.
// Onboard-LED heartbeat for bring-up (the Pod's LED is visible - unlike the cased spotykach). Blink
// the seed LED n times so we can see how far init() got; blocking, bring-up only.
static void blink(int n)
{
    daisy::System::Delay(500);
    for (int i = 0; i < n; i++) {
        hw.SetLed(true);  daisy::System::Delay(160);
        hw.SetLed(false); daisy::System::Delay(160);
    }
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Slow onboard-LED toggle = the audio ISR is alive (~1.4 Hz at 256/48k).
    static uint32_t c = 0; static bool s = false;
    if (((c++) & 0x7F) == 0) { s = !s; hw.SetLed(s); }
    engine.process(in, out, size);
}

int main(void)
{
    // Defensive: point the vector table at our QSPI app. A no-op if the bootloader already did it,
    // but it makes this image bootloader-agnostic - the same VTOR inject the csound harness uses to
    // confirm a QSPI app boots SysTick + the audio DMA IRQ under the spotykach v2 bootloader.
    SCB->VTOR = 0x90040000;
    __DSB();
    __ISB();

    hw.Configure();
    hw.Init();                                  // powers the FMC -> SDRAM live (chuck_heap_arm needs it)
    hw.SetAudioBlockSize(kBlock);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    AdcChannelConfig adc[kNumKnobs];
    for (int i = 0; i < kNumKnobs; i++)
        adc[i].InitSingle(kKnobPins[i]);
    hw.adc.Init(adc, kNumKnobs);
    hw.adc.Start();

    blink(2);   // reached: hardware up, about to call engine.init() (new ChucK / init / compileCode)

    // Build the context the platform would normally inject. ChuckEngine reads sample_rate + block_size
    // and arms its own SDRAM pool; the arena and service pointers are unused on the Pod (null).
    spotykach::EngineContext ctx{};
    ctx.sample_rate = 48000.f;
    ctx.block_size  = static_cast<float>(kBlock);
    ctx.arena       = { nullptr, 0 };
    ctx.time        = nullptr;
    ctx.transport   = nullptr;
    ctx.stream      = nullptr;
    ctx.qspi        = nullptr;
    engine.init(ctx);                           // new ChucK / compileCode(kProgram) - allocates in SDRAM

    blink(3);   // reached: engine.init() RETURNED (ChucK create/init/compile did not crash)

    hw.StartAudio(AudioCallback);

    using spotykach::DeckRef;
    using spotykach::ParamId;
    while (1) {
        engine.set_param(ParamId::Speed, DeckRef::A, hw.adc.GetFloat(0));   // knob1 -> Speed (pitch)
        engine.set_param(ParamId::Mix,   DeckRef::A, hw.adc.GetFloat(1));   // knob2 -> Mix (level)
        engine.prepare();
    }
}
