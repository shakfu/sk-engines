// 3.ck - STK Rhodey (electric piano) auto-arpeggio. Shows that the STK instruments link into this
// build (not just the raw oscillators). Self-running, no MIDI needed.
//   PITCH (speedA) = tempo, SIZE (sizeA) = brightness, MIX (mixA) = level.

global float speedA;
global float mixA;
global float sizeA;

Rhodey ep => LPF f => dac;
2.0 => f.Q;

[0, 4, 7, 11, 12, 7] @=> int arp[];   // a major-7 arpeggio, up then partway down
48 => int root;
0 => int i;

while( true )
{
    400.0 + sizeA * 6000.0 => f.freq;            // SIZE -> brightness
    (0.2 + mixA * 0.7) => ep.gain;               // MIX -> level
    arp[i % arp.size()] + root => int note;
    Std.mtof( note ) => ep.freq;
    0.8 => ep.noteOn;                            // strike the note
    60.0 + (1.0 - speedA) * 340.0 => float period;   // PITCH -> tempo (~400 ms down to ~60 ms)
    period::ms => now;
    i++;
}
