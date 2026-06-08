#include "app.h"

#include <functional>

#include "common.h"
#include "settings.h"
#include "hw/hardware.h"
#include "hw/buffer.sdram.h"
#include "ui/core.ui.h"
#include "engine/itimesource.h"
#include "engine/engine_select.h"  // ActiveEngine (build-time engine selection, item 3b)
#include "transport/transport.h"   // platform clock/transport service (shared across engines)
#include "memory/storage.h"
#include "expose.h"
#if defined(SPK_ENGINE_TAPE)
#include "hw/stream_deck.h"   // SD streaming service (tape engine only)
#endif

#define STORAGE

// #define METER
#ifdef METER
#include "meter.h"
#endif

using namespace daisy;
using namespace infrasonic;

namespace spotykach {

// Hardware-backed clock for the DSP core. Off-target builds (host harness) supply their own.
struct DaisyTimeSource : ITimeSource {
    uint32_t now_ms() const override { return daisy::System::GetNow(); }
    uint32_t now_us() const override { return daisy::System::GetUs(); }
};

class AppImpl {
  public:
    AppImpl():
    _ui     { CoreUI(_hw, _engine, _transport, _settings, _storage) }
    {}

    ~AppImpl() = default;

    void Init();
    void Loop();
    // DAC modulation outputs. One block-rate engine call (no per-sample virtual on the ISR), then
    // convert float CV to the DAC's 12-bit range. set_lfo caches the block's last sample for the
    // cycle LED (read asynchronously at 62 Hz, so last-of-block is equivalent to the old per-sample).
    void process_cv(uint16_t** out, size_t size) {
        float cv0[kDacBufSize];
        float cv1[kDacBufSize];
        _engine.process_cv(cv0, cv1, size);
        for (size_t i = 0; i < size; i++) {
            out[0][i] = __USAT(cv0[i] * (1 << 12), 12);
            out[1][i] = __USAT(cv1[i] * (1 << 12), 12);
        }
        _ui.set_lfo(cv0[size - 1], cv1[size - 1]);
    }
    CoreUI& ui() { return _ui; }

    void ProcessAudio(AudioHandle::InputBuffer  in,
                      AudioHandle::OutputBuffer out,
                      size_t                    size);

  private:
    NOCOPY(AppImpl)

    #if DEBUG
    StopwatchTimer _log_timer;
    void logDebugInfo();
    #endif
    bool _log_enabled;

    DaisyTimeSource _time_source;
    Transport       _transport; // platform clock; injected into the engine + driven by CoreUI
    ActiveEngine    _engine;  // concrete engine chosen at build time; platform sees only IEngine
    CoreUI      _ui;
    Hardware    _hw;
    Settings    _settings;
    Storage     _storage;
#if defined(SPK_ENGINE_TAPE)
    StreamDeck  _stream;  // SD play/record streaming for the tape engine (pumped in Loop)
#endif
};
};

using namespace spotykach;

static AppImpl impl;

static int8_t leds_update_counter = 0;
void T5Callback(void* data) 
{
    impl.ui().process_gate_in();
    if (leds_update_counter++ == 3) {
        leds_update_counter = 0;
        impl.ui().render_leds();
    }
};

//According to GetPClk2Freq docs, timers run at the frequency twice faster
//as their peripheral frequency. So call_freq_hz should be twise smaller 
//then synclock period.
TimerHandle tim5_handle;
void StartT5Callback(TimerHandle::PeriodElapsedCallback cb, uint32_t call_freq_hz) {
    TimerHandle::Config timcfg;
    timcfg.periph = TimerHandle::Config::Peripheral::TIM_5;
    timcfg.dir = TimerHandle::Config::CounterDir::UP;
    timcfg.period = System::GetPClk2Freq() / call_freq_hz;
    timcfg.enable_irq = true;
    tim5_handle.Init(timcfg);
    tim5_handle.SetCallback(cb);
    tim5_handle.Start();
};

void DACCallback(uint16_t **out, size_t size)
{
    impl.process_cv(out, size);
};

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    impl.ProcessAudio(in, out, size);
};

void AppImpl::Init() 
{
    auto sample_rate = 48000;
    auto block_size = 96;
    _hw.Init(sample_rate, block_size);

    // Hand the engine the SDRAM arena + clock; the engine sub-allocates whatever buffers it needs
    // (item: EngineBuffers generalization). The platform/HAL no longer knows any engine's layout.
    // The platform clock comes up first: the engine subscribes to its ticks during init().
    _transport.init(sample_rate, block_size, &_time_source);

    EngineContext ctx;
    ctx.sample_rate = sample_rate;
    ctx.block_size = block_size;
    ctx.time = &_time_source;
    ctx.transport = &_transport;
    ctx.arena = SDRAMBuffer::pool().engineArena();
#if defined(SPK_ENGINE_TAPE)
    {
        const auto sm = SDRAMBuffer::pool().streamMem();
        _stream.init({ sm.ring_a, sm.ring_a_bytes, sm.ring_b, sm.ring_b_bytes,
                       sm.scratch, sm.scratch_bytes });
        ctx.stream = &_stream;   // engine reads this in init()
    }
#endif
    _engine.init(ctx);

    _ui.init();
    #ifdef STORAGE
    _storage.init(_engine);
    _storage.read_settigs();
    #endif

    Log::StartLog(false);
#if DEBUG
    _log_timer.Init();
#endif

    StartT5Callback(T5Callback, 250);

    _hw.StartDAC(DACCallback);

    auto& audio = _hw.seed.audio_handle;
    audio.SetSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    audio.SetBlockSize(block_size);
    audio.Start(AudioCallback);

    _settings.init(_hw);
    _settings.read();

    _ui.calibrate(false);

    #ifdef METER
    Meter::cpu().load.Init(sample_rate, block_size);
    #endif
}

void AppImpl::Loop()
{
    while(true) {
        // If boot button held for 3s, reset into bootloader mode for update
        if (_hw.GetBootButtonHeldTime() >= 3000)
        {
            System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
        }

        _ui.process();
        _engine.prepare();
        #ifdef STORAGE
        _storage.process();
        #endif
        #if defined(SPK_ENGINE_TAPE)
        _stream.process();   // pump the slow SD play/record I/O for the streaming engine
        #endif
        
        #if DEBUG
        if(_log_timer.HasPassedMs(250))
        {
            logDebugInfo();
            _log_timer.Restart();

            #ifdef METER
            auto& loadMeter = Meter::cpu().load;
            const float avgLoad = loadMeter.GetAvgCpuLoad();
            const float maxLoad = loadMeter.GetMaxCpuLoad();
            const float minLoad = loadMeter.GetMinCpuLoad();
            Log::PrintLine("Processing Load %:");
            Log::PrintLine("Max: " FLT_FMT3, FLT_VAR3(maxLoad * 100.0f));
            Log::PrintLine("Avg: " FLT_FMT3, FLT_VAR3(avgLoad * 100.0f));
            Log::PrintLine("Min: " FLT_FMT3, FLT_VAR3(minLoad * 100.0f));
            #endif
        }
        #endif
    }
}

void AppImpl::ProcessAudio(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    #ifdef METER
    Meter::cpu().load.OnBlockStart();
    #endif
    
    _hw.ProcessAnalogControls();
    _ui.tick();
    _ui.read_cv();
    _engine.process(in, out, size);

    #ifdef METER
    Meter::cpu().load.OnBlockEnd();
    #endif
}

#if DEBUG
void AppImpl::logDebugInfo()
{
    Expose::values().print();

    // float val = hw.GetAnalogControlValue(Hardware::CTRL_PITCH_A);
    // float val = hw.GetControlVoltageValue(Hardware::CV_V_OCT_A);
    // Log::PrintLine(FLT_FMT(5), FLT_VAR(5, val));
    // uint16_t touch = hw.GetMpr121TouchStates();
}
#endif

void Application::Init() 
{
    impl.Init();
}

void Application::Loop() 
{
    impl.Loop();
}
