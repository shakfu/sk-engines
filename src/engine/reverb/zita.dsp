// faust reverb engine DSP source (hall). Regenerate with `make faust-gen` (see Makefile / README.md).
//
// Fons Adriaensen's Zita-rev1 (via Faust's reverbs.lib dm.zita_rev1) - the reference free FDN hall:
// an 8x8 feedback-delay-network with frequency-dependent decay (separate low/mid RT60), HF damping,
// and two output peaking EQs. Lusher and longer than the Dattorro plate; the two together give the
// CapAux selector a useful plate-vs-hall pair. MIT-licensed (demos.lib/zita_rev1).
//
// Control labels consumed by the reverb engine's zita bind table (linear-mapped from the 0..1 knobs into
// each slider's native range): "Wet/Dry Mix", "Mid RT60", "Low RT60", "HF Damping", "In Delay",
// "LF X". "Level" (output dB) is captured and held fixed; the EQ sections keep their flat defaults.
declare name "zita";
declare description "Zita-rev1 FDN hall reverb (sk-engines faust engine).";

import("stdfaust.lib");

process = dm.zita_rev1;
