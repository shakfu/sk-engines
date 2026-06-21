// 6.ck - karp-o-matic (Ge Wang / Perry R. Cook), lightened for the Daisy. The stock patch runs a
// StifKarp through JCRev + three Echos; the network reverb (JCRev) is the budget killer on this MCU, so
// it's dropped here - a StifKarp through two cheap Echo delay lines (delay lines are inexpensive per
// sample; the network reverbs NRev/JCRev are not). Add controls for the panel.
//   PITCH (speedA) = octave, SIZE (sizeA) = echo amount, MIX (mixA) = level.

global float speedA;   // PITCH -> octave shift
global float sizeA;    // SIZE  -> echo mix
global float mixA;     // MIX   -> level

// one plucked-string voice through two echo delay lines (no JCRev)
StifKarp karp => Gain g => Echo a => Echo b => dac;
g => dac;                                  // dry path alongside the echoes
500::ms => a.max => b.max;
375::ms => a.delay => b.delay;

// scale (semitone offsets)
[ 0, 2, 4, 7, 9 ] @=> int scale[];

while( true )
{
    (0.2 + mixA * 0.6) => g.gain;          // MIX -> level
    (0.05 + sizeA * 0.5) => a.mix;         // SIZE -> echo amount
    (0.05 + sizeA * 0.4) => b.mix;

    Math.random2f( 0.2, 0.8 ) => karp.pickupPosition;
    scale[Math.random2(0,scale.size()-1)] => int deg;
    // PITCH -> octave: base 110 Hz, up to +3 octaves; plus a random octave + the scale degree
    110.0 * Math.pow( 2.0, speedA*2.0 )
          * Math.pow( 1.05946, (Math.random2(0,2)*12) + deg ) => karp.freq;
    0.0 => karp.stretch;
    Math.random2f( 0.2, 0.9 ) => karp.pluck;

    if( Math.randomf() > 0.9 )       500::ms => now;
    else if( Math.randomf() > .925 ) 250::ms => now;
    else                             .125::second => now;
}
