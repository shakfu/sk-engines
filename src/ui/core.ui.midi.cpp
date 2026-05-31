#include "core.ui.h"
#include "config.h"
#include "daisy.h"

using namespace spotykach;
using namespace daisy;

// CLOCK ///////////////////////////////////
static bool clock_state = false;
void CoreUI::tick()
{
    auto&d = _core.driver();
    auto new_state = false;
    auto midi_state = _process_midi();
    switch (d.source()) {
        case Driver::Source::ts4: new_state = _hw.GetClockInputState(); break;
        case Driver::Source::midi: new_state = midi_state; break;
        default: break;
    }
    d.tick(new_state && !clock_state);
    clock_state = new_state;

    // Modified libDaisy MIDI handlers require explicit call to transmit
    // enqueued messages instead of blocking every time a message is sent
    _hw.midi_uart.TransmitEnqueuedMessages();
}

// MIDI /////////////////////////////////////
bool CoreUI::_process_midi()
{
    _hw.midi_uart.Listen();
    bool has_clock = false;
    while(_hw.midi_uart.HasEvents())
    {
        auto event = _hw.midi_uart.PopEvent();
        switch(event.type)
        {
            case MidiMessageType::SystemRealTime: {
                has_clock = _process_realtime(event) || has_clock; 
            }
            break;
            
            case MidiMessageType::NoteOn: {
                auto e = event.AsNoteOn();
                auto ref = _engine.handle_midi_note(e.channel, e.note);
                if (ref != Deck::Count) _show_gate_in(ref);
            }
            break;

            default: break;
        }
    }
    return has_clock;
}
bool CoreUI::_process_realtime(daisy::MidiEvent& event)
{
    switch (event.srt_type) {
        case SystemRealTimeType::TimingClock: return true;
        case SystemRealTimeType::Start:
        case SystemRealTimeType::Continue: _engine.handle_midi_transport(true); break;
        case SystemRealTimeType::Stop:     _engine.handle_midi_transport(false); break;
        default: break;
    }

    return false;
}

void CoreUI::_process_clock_out()
{
    _hw.midi_uart.EnqueueMessage(MidiTxMessage::SystemRealtimeClock());
}
