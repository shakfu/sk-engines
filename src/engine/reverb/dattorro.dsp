// faust reverb engine DSP source. Regenerate the kernel with:
//   make faust-gen           (wraps cyfaust; see Makefile / src/engine/reverb/README.md)
//
// Jon Dattorro's plate reverb (JAES 1997, "Effect Design Part 1"), via Faust's reverbs.lib
// dattorro_rev demo wrapper. A modulated figure-8 tank: input pre-filter -> input diffusers ->
// decay-diffusion allpasses with LFO excursion -> damped feedback. The tank modulation is what gives
// the lush, non-metallic tail a static comb reverb lacks. Stereo in / stereo out.
//
// The dattorro_rev demo is MIT-licensed (author: Jakob Zerbian); the maths.lib it pulls is LGPL with
// the standard Faust runtime exception. Control labels consumed by the reverb engine's CaptureUI:
//   Input box:    "Prefilter", "Diffusion 1", "Diffusion 2"
//   Feedback box: "Diffusion 1", "Diffusion 2", "Decay Rate", "Damping"
//   Output box:   "Dry/Wet Mix" (-1 dry .. +1 wet), "Level" (dB, held fixed by the engine)
// "Diffusion 1/2" appear in BOTH the Input and Feedback boxes, so the engine disambiguates them by
// the enclosing box name (see CaptureUI in reverb_engine.cpp).
declare name "dattorro";
declare description "Dattorro plate reverb (sk-engines faust engine).";

import("stdfaust.lib");

process = dm.dattorro_rev_demo;
