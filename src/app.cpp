#include "app.h"

#include <functional>

#include "common.h"
#include "version.h"
#include "settings.h"
#include "hw/hardware.h"
#include "hw/buffer.sdram.h"
#include "ui/core.ui.h"
#include "engine/itimesource.h"
#include "engine/engine_select.h"  // ActiveEngine (build-time engine selection, item 3b)
#include "transport/transport.h"   // platform clock/transport service (shared across engines)
#include "memory/storage.h"
#include "expose.h"
#if defined(SPK_USE_STREAM)
#include "hw/stream_deck.h"   // SD streaming service (any SPK_USE_STREAM engine: tape, shuttle)
#endif
#ifdef METER
#include "hid/usb.h"   // daisy::UsbHandle - direct non-blocking CDC for the CPU-load meter
#include <cstdio>      // snprintf
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

    #if DEBUG || defined(METER)
    StopwatchTimer _log_timer; // throttles the serial log (debug info and/or the CPU meter)
    #endif
    #ifdef METER
    daisy::UsbHandle _meter_usb; // non-blocking CDC for the load meter (no Logger -> can't spin/hang)
    #endif
    #if DEBUG
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
#if defined(SPK_USE_STREAM)
    StreamDeck  _stream;  // SD play/record streaming for streaming engines (pumped in Loop)
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
    ctx.qspi = &_hw.seed.qspi;   // QSPI flash handle for engines that persist a kit preset (edrums)
#if defined(SPK_USE_STREAM)
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
    // Touch the build banner through a volatile so it is retained even in release builds, where the
    // LOG_TAGGED below compiles to nothing (so `strings firmware.bin` can still report the version).
    volatile const char* fw_banner = firmware_banner();
    (void)fw_banner;
    LOG_TAGGED("boot", "%s", firmware_banner());
#if DEBUG || defined(METER)
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
    _meter_usb.Init(daisy::UsbHandle::FS_EXTERNAL); // CDC for the load meter (LOGGER_EXTERNAL port)
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
        #if defined(SPK_USE_STREAM)
        _stream.process();   // pump the slow SD play/record I/O for the streaming engine
        #endif
        
        #if DEBUG || defined(METER)
        if(_log_timer.HasPassedMs(250))
        {
            #if DEBUG
            logDebugInfo();
            #endif
            _log_timer.Restart();

            #ifdef METER
            auto& loadMeter = Meter::cpu().load;
            const int mx = (int)(loadMeter.GetMaxCpuLoad() * 10000.f + 0.5f); // hundredths of a percent
            const int av = (int)(loadMeter.GetAvgCpuLoad() * 10000.f + 0.5f);
            const int mn = (int)(loadMeter.GetMinCpuLoad() * 10000.f + 0.5f);
            char line[80];
            const int n = snprintf(line, sizeof(line),
                                   "load%% max=%d.%02d avg=%d.%02d min=%d.%02d\r\n",
                                   mx / 100, mx % 100, av / 100, av % 100, mn / 100, mn % 100);
            // Direct, NON-BLOCKING CDC write: drop the line if the host isn't draining the buffer, so the
            // meter can never hang the main loop (the daisy Logger spins after its first 2 packets).
            if (n > 0) _meter_usb.TransmitExternal((uint8_t*)line, (size_t)n);
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
