// Csound foundation app + audible 3-way diagnostic (no serial/LED needed):
//   LOW 220 Hz buzz  -> csoundCompileCSD FAILED (orchestra/build problem)
//   clean 440 Hz sine -> Csound compiled and is synthesizing (success)
//   silence           -> compiled OK but perform/spout produces nothing
// Orchestra is currently a fixed 440 sine (knobs removed to isolate Csound from chnget).

#include "daisy_seed.h"
#include "app.h"
#include "csound.h"

using namespace daisy;
using namespace daisy::seed;

DaisySeed hw;
CSOUND*   csound = nullptr;
int       g_cnt  = 0;

// Csound buffered de-interleaved callback (ksmps 512 > block 256).
static void CsoundCallback(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    const MYFLT* spout = csoundGetSpout(csound);
    const int    end   = csoundGetKsmps(csound);
    for (size_t i = 0; i < size; i++) {
        if (g_cnt == 0) csoundPerformKsmps(csound);
        out[0][i] = static_cast<float>(spout[g_cnt]     * 0.5);
        out[1][i] = static_cast<float>(spout[g_cnt + 1] * 0.5);
        g_cnt = (g_cnt + 2) % (end * 2);
    }
}

// Audible "Csound failed" flag: a low 220 Hz buzz, generated in C.
static void FailCallback(AudioHandle::InputBuffer  in,
                         AudioHandle::OutputBuffer out,
                         size_t                    size)
{
    static float ph = 0.f;
    const float  inc = 2.f * 3.14159265358979f * 220.f / 48000.f;
    for (size_t i = 0; i < size; i++) {
        const float s = 0.25f * __builtin_sinf(ph);
        ph += inc;
        if (ph > 6.28318530718f) ph -= 6.28318530718f;
        out[0][i] = s;
        out[1][i] = s;
    }
}

// Pod knob 1 -> D21, knob 2 -> D15, pushed to the "knob1"/"knob2" control channels.
static const int   kNumKnobs             = 2;
static const Pin   kKnobPins[kNumKnobs]  = { D21, D15 };
static const char* kKnobChans[kNumKnobs] = { "knob1", "knob2" };

int main(void)
{
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(256);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    AdcChannelConfig adc[kNumKnobs];
    for (int i = 0; i < kNumKnobs; i++)
        adc[i].InitSingle(kKnobPins[i]);

    CSOUND* cs = csoundCreate(NULL, NULL);
    if (!cs) { hw.StartAudio(FailCallback); while (1) {} }
    csound = cs;

    csoundSetHostData(cs, (void*)&hw);
    csoundSetHostAudioIO(cs);
    csoundSetOption(cs, "-n");
    csoundSetOption(cs, "--ksmps=512");
    csoundSetOption(cs, "-dm0");

    if (csoundCompileCSD(cs, csdText.c_str(), 1, 0) != 0) {
        hw.StartAudio(FailCallback);           // LOW buzz = compile failed
        while (1) {}
    }
    csoundStart(cs);

    hw.adc.Init(adc, kNumKnobs);
    hw.adc.Start();
    hw.StartAudio(CsoundCallback);             // the orchestra (knob-controlled saw)

    while (1) {
        for (int i = 0; i < kNumKnobs; i++)
            csoundSetControlChannel(csound, kKnobChans[i], hw.adc.GetFloat(i));
    }
}
