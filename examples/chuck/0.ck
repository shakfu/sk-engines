// 0.ck - clean two-saw "fifth" drone for the chuck engine (ENGINE=chuck). A calm baseline patch,
// entirely knob-controlled. Same global vocabulary as the built-in program, so the same knobs drive it.
//   PITCH (speedA) = pitch, SIZE (sizeA) = brightness, MIX (mixA) = level.

global float speedA;
global float mixA;
global float sizeA;

SawOsc root => LPF f => dac;
SawOsc fifth => f;                 // a second saw, a perfect fifth above, into the same low-pass
0.7 => f.Q;

while( true )
{
    110.0 + speedA * 440.0 => float hz;     // PITCH -> root pitch (110..550 Hz)
    hz       => root.freq;
    hz * 1.5 => fifth.freq;                 // perfect fifth
    1500.0 + sizeA * 6000.0 => f.freq;      // SIZE -> brightness (undriven => 1500 Hz)
    (0.12 + mixA * 0.8) * 0.25 => float g;  // MIX -> level, with a floor so boot is audible
    g => root.gain;
    g => fifth.gain;
    10::ms => now;                          // block-rate control update (cheap)
}
