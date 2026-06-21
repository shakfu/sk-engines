// 1.ck - fat detuned super-saw with a sub-octave. Thicker and bassier than 0.ck, so the difference is
// obvious when you Alt+PITCH between the two slots (a good live-switch test). Knob-controlled.
//   PITCH (speedA) = pitch, SIZE (sizeA) = brightness, MIX (mixA) = level.

global float speedA;
global float mixA;
global float sizeA;

SawOsc a => LPF f => dac;
SawOsc b => f;
SawOsc c => f;
SawOsc sub => f;
0.7 => f.Q;

while( true )
{
    40.0 + speedA * 300.0 => float hz;      // lower range -> bassier than 0.ck
    hz         => a.freq;
    hz * 1.006 => b.freq;                   // detune up   -> slow beating
    hz * 0.994 => c.freq;                   // detune down -> fatter
    hz * 0.5   => sub.freq;                 // sub-oscillator an octave below
    180.0 + sizeA * 5000.0 => f.freq;       // SIZE -> brightness
    (0.1 + mixA * 0.8) * 0.18 => float g;   // MIX -> level (lower base gain: four oscillators sum)
    g       => a.gain;
    g       => b.gain;
    g       => c.gain;
    g * 1.2 => sub.gain;
    10::ms => now;
}
