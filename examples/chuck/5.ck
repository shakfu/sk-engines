// 5.ck - HonkyTonk 4op FM, SINGLE voice. HnkyTonk (4-op FM + LFO) is the heaviest instrument here: two
// dry voices overran the audio block (the overrun-mute caught it), whereas one fits - cf. 6.ck's single
// StifKarp. So this is a monophonic honkytonk line: one voice walking between bass roots and a random
// melody, through a cheap shared low-pass (no reverb). After honkeytonk-algo1.ck, Perry R. Cook.
//   PITCH (speedA) = tempo, SIZE (sizeA) = brightness, MIX (mixA) = level.

global float speedA;   // PITCH -> tempo
global float sizeA;    // SIZE  -> brightness (LP cutoff)
global float mixA;     // MIX   -> level

HnkyTonk v => LPF f => dac;
2.0 => f.Q;

[69,72,74,76,77,81] @=> int melody[];
[36, 41, 43, 36, 38, 41] @=> int roots[];   // walking bass roots
0 => int n;

while( true )
{
    500.0 + sizeA * 5000.0 => f.freq;        // SIZE -> brightness
    (0.15 + mixA * 0.6) => v.gain;           // MIX -> level

    // bass root on the beat
    Std.mtof( roots[n % roots.size()] ) => v.freq;
    1 => v.noteOn;
    beatMs()::ms => now;

    // a random melody note off the beat
    Std.mtof( melody[Math.random2(0, melody.size()-1)] ) => v.freq;
    0.8 => v.noteOn;
    beatMs()::ms => now;

    n++;
}

// PITCH -> tempo: one beat is ~120 ms (fast) up to ~480 ms (slow)
fun float beatMs() { return 120.0 + (1.0 - speedA) * 360.0; }
