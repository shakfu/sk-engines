//---------------|
// karp-o-matic!
// by: Ge Wang (gewang@cs.princeton.edu)
//     Perry R. Cook (prc@cs.princeton.edu)
//------------------|
// Full network-reverb version (StifKarp -> JCRev -> three Echos), with panel controls added.
// PITCH (speedA) = octave shift, SIZE (sizeA) = echo amount, MIX (mixA) = level.

global float speedA;   // PITCH -> octave shift
global float sizeA;    // SIZE  -> echo amount (echo mix depth)
global float mixA;     // MIX   -> level

// our patch (master Gain g carries the MIX -> level control)
StifKarp karp => JCRev r => Echo a => Echo b => Echo c => Gain g => dac;
// set the gain
.95 => r.gain;
// set the reverb mix
.02 => r.mix;
// set max delay for echo
1000::ms => a.max => b.max => c.max;
// set delay for echo
750::ms => a.delay => b.delay => c.delay;
// set the initial effect mix
0.0 => a.mix => b.mix => c.mix;

// scale
[ 0, 2, 4, 7, 9 ] @=> int scale[];

// our main loop
while( true )
{
    // MIX -> level
    (0.2 + mixA * 0.6) => g.gain;
    // SIZE -> echo amount: set echo mix depth directly
    (0.05 + sizeA * 0.5) => a.mix;
    (0.05 + sizeA * 0.4) => b.mix;
    (0.05 + sizeA * 0.3) => c.mix;
    // position
    Math.random2f( 0.2, 0.8 ) => karp.pickupPosition;
    // frequency...
    scale[Math.random2(0,scale.size()-1)] => int freq;
    // PITCH -> octave: base 220 Hz shifted up to +3 octaves; plus a random octave + the scale degree
    220.0 * Math.pow( 2.0, speedA*2.0 )
          * Math.pow( 1.05946, (Math.random2(0,2)*12)
                      +freq ) => karp.freq;
    // pluck it!
    0.0 => karp.stretch;
    Math.random2f( 0.2, 0.9 ) => karp.pluck;

    // note: Math.randomf() return value between 0 and 1
    if( Math.randomf() > 0.9 )
    { 500::ms => now; }
    else if( Math.randomf() > .925 )
    { 250::ms => now; }
    else if( Math.randomf() > .05 )
    { .125::second => now; }
    else
    {
        1 => int i => int pick_dir;
        // how many times
        4 * Math.random2( 1, 5 ) => int pick;
        0.0 => float pluck;
        0.7 / pick => float inc;
        // time loop
        for( ; i < pick; i++ )
        {
            75::ms => now;
            Math.random2f(.2,.3) + i*inc => pluck;
            i * 0.025 => karp.stretch;
            pluck + -.2 * pick_dir => karp.pluck;
            // simulate pluck direction
            !pick_dir => pick_dir;
        }
        // let time pass for final pluck
        75::ms => now;
    }
}
