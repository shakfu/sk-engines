// 4.ck - "a chuck is born" (the classic random-melody example), now driven by the panel knobs.
//   PITCH (speedA) = tempo, SIZE (sizeA) = reverb amount (space), MIX (mixA) = level.

global float speedA;   // PITCH -> tempo
global float sizeA;    // SIZE  -> reverb mix
global float mixA;     // MIX   -> level

SinOsc s => JCRev r => dac;

// a scale (semitone offsets) to pick notes from
[ 0, 2, 4, 7, 9, 11 ] @=> int hi[];

while( true )
{
    // controls, read live each step
    (0.05 + mixA * 0.45)  => s.gain;     // MIX  -> level
    (0.02 + sizeA * 0.60) => r.mix;      // SIZE -> reverb amount (dry..wet)

    // random note from the scale, across a few octaves
    Std.mtof( 45 + Math.random2(0,3) * 12 + hi[Math.random2(0,hi.size()-1)] ) => s.freq;

    // PITCH -> tempo: ~40 ms (fast) up to ~280 ms (slow)
    40.0 + (1.0 - speedA) * 240.0 => float period;
    period::ms => now;
}
