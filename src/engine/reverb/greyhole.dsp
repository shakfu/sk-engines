// faust reverb engine DSP source. Regenerate the kernel with:
//   make faust-kernels       (wraps cyfaust; see Makefile / src/engine/reverb/README.md)
//
// Greyhole (re.greyhole): a diffusion network of modulated allpasses with a pitch-shifter in the
// feedback path - a lush, evolving, ambient reverb (originally a SuperCollider plugin by Nick Collins /
// Julian Parker). Much more "alive" than the static plate/hall. It has no internal dry/wet, so
// the .dsp adds the crossfade. Stereo in/out. Control labels for the reverb engine's CaptureUI:
//   "Feedback", "Size", "Damp", "Diffusion", "ModDepth", "Mix".
declare name "greyhole";
declare description "Greyhole modulated reverb (sk-engines faust engine).";

import("stdfaust.lib");

fb       = hslider("Feedback",  0.7, 0,   1,   0.001) : si.smoo;  // tail length (K_Decay / PITCH)
size     = hslider("Size",      1.0, 0.5, 3.0, 0.001) : si.smoo;  // network size (K_SizeA / SIZE)
damp     = hslider("Damp",      0.3, 0,   1,   0.001) : si.smoo;  // HF damping (K_Damp / ENV)
diff     = hslider("Diffusion", 0.7, 0,   1,   0.001) : si.smoo;  // smear/character (K_Tone / POS)
moddepth = hslider("ModDepth",  0.2, 0,   1,   0.001) : si.smoo;  // modulation depth (K_SizeB / MODAMT)
mix      = hslider("Mix",       0.33, 0,  1,   0.001) : si.smoo;  // dry/wet (K_Mix / SOS)

dt   = 0.2;   // base delay time (s), fixed
modf = 2.0;   // modulation frequency (Hz), fixed

wet = re.greyhole(dt, damp, size, diff, fb, moddepth, modf);

// Stereo dry/wet: scale the dry pair by (1-mix) and the reverberated pair by mix, then sum L+L, R+R.
process = _, _ <: (*(1.0 - mix), *(1.0 - mix)), (wet : (*(mix), *(mix))) :> _, _;
