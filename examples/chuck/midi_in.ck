// midi_in.ck - validates the re-introduced ChucK MidiIn device class on the Daisy build (not a
// numbered bank slot). Unlike midi.ck (which uses the host "global bridge" convention), this patch
// uses real ChucK MIDI: it opens a MidiIn device and drains MidiMsg with min.recv(), exactly as an
// unmodified desktop ChucK patch would. On this firmware the device is a single virtual UART (device
// 0); the host injects each incoming NoteOn into it (see chuck_engine.cpp / docs/dev/chuck-midi-in.md).
//
// To try it, copy this over any numbered slot on the card, e.g.:
//     cp examples/chuck/midi_in.ck <card>/chuck/3.ck
// then select that slot and play. Chords work (each NoteOn sporks its own voice). MIX sets the level.
//
// NOTE: the host currently injects NoteOn only, with a fixed velocity (the note bridge carries no
// velocity). Once the host forwards full MIDI, CC / pitch-bend / real velocity arrive here unchanged.

global float mixA;        // MIX knob -> level (same vocabulary as the other examples)

MidiIn min;
MidiMsg msg;

// Open the virtual UART MIDI device (device 0). If MidiIn is somehow unavailable, exit quietly so the
// slot just goes silent rather than erroring.
if( !min.open(0) ) me.exit();

// One self-contained, finite voice per note (mirrors midi.ck's voice): triangle -> ADSR -> dac.
fun void voice(int note, int vel)
{
    Std.mtof(note) => float hz;
    TriOsc s => ADSR e => dac;
    hz => s.freq;
    Math.max(0.02, mixA) * (vel / 127.0) * 0.3 => s.gain;   // MIX * velocity -> level
    e.set(2::ms, 90::ms, 0.0, 60::ms);                      // pluck-ish: self-terminating
    e.keyOn();
    140::ms => now;
    e.keyOff();
    70::ms => now;                                           // let the release finish
}

// Block on the MidiIn event, then drain every queued message. This is the wake path under test:
// `min => now` must resume when the host injects a message (no ChucK threads on this build).
while( true )
{
    min => now;                                  // wait for MIDI
    while( min.recv(msg) )                        // drain all messages delivered this wake
    {
        (msg.data1 & 0xf0) => int status;
        if( status == 0x90 && msg.data3 > 0 )    // NoteOn with non-zero velocity
            spork ~ voice(msg.data2, msg.data3);
    }
}
