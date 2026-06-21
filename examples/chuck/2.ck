// 2.ck - concurrent generative pad. Several voice shreds run at once, each on its own clock, so the
// patch plays itself in polyrhythm. This is the patch that shows off ChucK's strength (and exercises
// the METER build's ring B = live shred count: you should see several cyan dots, not one). No MIDI.
//   PITCH (speedA) = tempo, SIZE (sizeA) = brightness, MIX (mixA) = level.

global float speedA;
global float mixA;
global float sizeA;

// Shared output: a summing bus into one low-pass that all voices share.
Gain bus => LPF f => dac;
0.5 => bus.gain;
1.0 => f.Q;

[0, 3, 5, 7, 10] @=> int scale[];   // minor-pentatonic semitone offsets
36 => int root;                     // base note

// Brightness follower - its own shred, so SIZE tracks live while the voices play.
fun void controls()
{
    while( true )
    {
        400.0 + sizeA * 6000.0 => f.freq;
        10::ms => now;
    }
}

// One voice: pluck random scale notes on its own clock. `oct` transposes it; `mult` skews its tempo,
// so the voices drift against each other (polyrhythm). Loops forever -> stays a live shred.
fun void voice( int oct, float mult )
{
    SawOsc s => ADSR e => bus;
    e.set( 4::ms, 140::ms, 0.0, 60::ms );
    while( true )
    {
        // PITCH -> tempo: higher speedA shortens the period (~360 ms down to ~60 ms), skewed per voice.
        (60.0 + (1.0 - speedA) * 300.0) * mult => float period;
        period * 0.8 => float on;
        period * 0.2 => float off;
        scale[Math.random2(0, scale.size() - 1)] + root + oct * 12 => int note;
        Std.mtof( note ) => s.freq;
        (0.04 + mixA * 0.22) => s.gain;
        e.keyOn();
        on::ms => now;
        e.keyOff();
        off::ms => now;
    }
}

spork ~ controls();
spork ~ voice( 1, 1.0 );
spork ~ voice( 2, 0.75 );
spork ~ voice( 3, 1.5 );

// Keep the main shred alive so the program (and its sporked children) keeps running.
while( true ) 1::second => now;
