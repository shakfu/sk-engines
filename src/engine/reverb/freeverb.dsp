// faust reverb engine DSP source. Regenerate the kernel with:
//   make faust-kernels       (wraps cyfaust; see Makefile / src/engine/reverb/README.md)
//
// Freeverb (Schroeder-Moorer): 8 damped combs -> 4 allpasses per channel, via Faust's reverbs.lib
// re.stereo_freeverb. A warmer, more diffuse character than the modulated Dattorro plate or the Zita FDN
// hall - the third reverb voice (replaces the former gen~ gigaverb). A pre-delay feeds the tank and a
// low-pass tones the wet tail, so all six of the engine's knob roles map to a real control. Stereo in/out.
//
// Freeverb is public domain (Jezar at Dreampoint); reverbs.lib is LGPL with the standard Faust runtime
// exception, so the cyfaust-generated C++ ships under the project's MIT. Control labels consumed by the
// reverb engine's CaptureUI: "RoomSize", "Damp", "Spread", "Tone", "PreDelay", "Mix".
declare name "freeverb";
declare description "Freeverb (Schroeder-Moorer) reverb (sk-engines faust engine).";

import("stdfaust.lib");

roomsize = hslider("RoomSize", 0.7,  0, 1, 0.001) : si.smoo;   // comb feedback -> tail length
damp     = hslider("Damp",     0.5,  0, 1, 0.001) : si.smoo;   // HF damping in the combs
spread   = hslider("Spread",   0.5,  0, 1, 0.001);             // stereo spread (L/R comb detune)
tone     = hslider("Tone",     0.85, 0, 1, 0.001) : si.smoo;   // wet-tail low-pass (darkness)
predelay = hslider("PreDelay", 0.1,  0, 1, 0.001) : si.smoo;   // gap before the reverb starts
mix      = hslider("Mix",      0.33, 0, 1, 0.001) : si.smoo;   // dry/wet

fb1  = 0.7 + roomsize * 0.28;          // comb feedback ~0.70 .. 0.98
fb2  = 0.5;                            // allpass feedback (Freeverb's fixed 0.5)
sprd = spread * 23.0;                  // stereo spread in samples (Freeverb's default max is 23)
fc   = 400.0 * pow(2.0, tone * 5.3);   // wet low-pass ~400 Hz .. ~16 kHz
pdn  = predelay * 0.05 * ma.SR;        // pre-delay in samples (0 .. ~50 ms)

pd = de.fdelay(4096, pdn);             // fractional pre-delay, 1-in/1-out
lp = fi.lowpass(1, fc);                // 1-pole, 1-in/1-out

// pre-delay both channels, reverberate, then low-pass the wet pair
wet = (pd, pd) : re.stereo_freeverb(fb1, fb2, damp, sprd) : (lp, lp);

// Stereo dry/wet: scale the dry pair by (1-mix) and the reverberated pair by mix, then sum L+L, R+R.
process = _, _ <: (*(1.0 - mix), *(1.0 - mix)), (wet : (*(mix), *(mix))) :> _, _;
