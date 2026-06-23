//------------------------------------------------------------------
// name: oscillatronx (oscillator demo)
// desc: playing all of the different types of oscillator UGens
//       mixing together different timbres to create a sonic texture
//
// author: philipd
//------------------------------------------------------------------

// panel controls (A-deck globals read live in the loop)
global float speedA;   // PITCH    -> base note (transpose)
global float mixA;     // MIX      -> master level
global float sizeA;    // SIZE     -> FM index (timbre: clean .. clangorous)
global float modspA;   // ModSpeed -> event / wiggle rate (density)
global float modampA;  // ModAmp   -> pulse-width sweep depth

// scale degrees in semi-tones
[ 0, 2, 4, 7, 9 ] @=> int f[];

// master bus so MIX has a level to ride
Gain master => dac;

// various oscialltors
SinOsc s => master;
SawOsc saw => master;
TriOsc tri => master;
PulseOsc pul => master;
SqrOsc sqr => master;
// FM modulator and carrier
TriOsc trictrl => SinOsc sintri => master;
// interpret input as frequency modulation
2 => sintri.sync;
100  => trictrl.gain;

// array of Oscs
[ s, saw, tri, pul, sqr, trictrl ] @=> Osc oscillators[];

// set gains
0.2 => s.gain;
0.1 => saw.gain;
0.1 => tri.gain;
0.1 => pul.gain;
0.1 => sqr.gain;
0.1 => sintri.gain;

// infinite time-loop
while( true )
{
    // MIX -> master level (keep an audible floor)
    (0.3 + mixA * 0.7) => master.gain;
    // SIZE -> FM index: clean sine .. clangorous FM
    (20.0 + sizeA * 380.0) => trictrl.gain;
    // PITCH -> base note: ~2 octaves of transpose around MIDI 48
    48 + (speedA * 24.0) $ int => int base;
    // ModSpeed -> event/wiggle rate (higher = denser/faster)
    (0.40 - modspA * 0.32) => float step;
    (0.08 - modspA * 0.065) => float wig;
    // ModAmp -> pulse-width sweep depth around 0.5
    0.1 + modampA * 0.4 => float wd;

    // randomize
    Math.random2(0,7) => int select;
    // clamp (giving more weight to 5)
    if( select > 5 ) 5 => select;
    // generate new frequenc value
    Std.mtof( f[Math.random2( 0, 4 )] + base ) => float newnote;
    // set frequency
    newnote => oscillators[select].freq;
    // wait a bit
    step::second => now;
    // 10 times
    repeat(10)
    {
        Math.random2f( 0.5 - wd, 0.5 + wd ) => trictrl.width;
        // <<< "trictrl width:", trictrl.width() >>>;
        wig::second => now;
    }
}
