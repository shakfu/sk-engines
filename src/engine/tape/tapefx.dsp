// Tape-effect kernel for the tape engine. Regenerate the kernel with `make faust-gen`.
//
// Signal chain (mono, one instance per deck): wow/flutter (pitch wobble via a modulated fractional
// delay) -> Jiles-Atherton hysteresis/saturation. The hysteresis is hysteresis.lib's hy.ja_processor
// (LGPLv2.1 WITH the Faust library exception, so the cyfaust-generated C++ ships under the project's
// MIT; (C) 2025 Thomas Mandolini) - the same Jiles-Atherton model as Chowdhury's ChowTapeModel, but
// self-contained (fixed 4 substeps + cubic-Hermite interpolation = built-in antialiasing, no extra
// oversampling). In this model the saturation IS the hysteresis nonlinearity: `drive` is the amount,
// `char` (the J-A reversibility c) is the tone.
//
// All four sliders are 0..1 - the C++ (FaustTapeFx in tape_engine.cpp) writes normalized knob values
// straight into the captured zones; the musical scaling lives here. Labels: "drive", "char", "wow",
// "rate".
declare name "tapefx";
declare description "Tape wow/flutter + Jiles-Atherton hysteresis (sk-engines tape engine).";

import("stdfaust.lib");
hy = library("hysteresis.lib");

drive = hslider("drive", 0.3, 0, 1, 0.001); // saturation amount (input drive into the J-A curve)
chr   = hslider("char",  0.3, 0, 1, 0.001); // tape character (J-A reversibility c)
wowf  = hslider("wow",   0.3, 0, 1, 0.001); // wow + flutter depth
rate  = hslider("rate",  0.4, 0, 1, 0.001); // wow + flutter rate

// --- wow & flutter: pitch wobble via a modulated fractional delay -------------------------------
// Periodic (reel rotation), not random - wow slow, flutter as f + 2f + 3f harmonics (cf. ChowTape
// FlutterProcess). maxdel is a compile-time buffer size; the platform runs at 48 kHz.
maxdel   = 2400;       // 50 ms @ 48 kHz
basedel  = 1200.0;     // 25 ms nominal (room to swing either way)
wowHz    = 0.5 + rate * 2.0;   // 0.5 .. 2.5 Hz
fltHz    = 6.0 + rate * 6.0;   // 6 .. 12 Hz
// os.oscrs (recursive "magic circle" sine) is table-free - os.osc would emit a 64K-entry static sine
// table (256 KB) that lands in SRAM and overflows the region; the LFOs need no such table.
wowLFO   = os.oscrs(wowHz);
fltLFO   = 0.6 * os.oscrs(fltHz) + 0.25 * os.oscrs(2.0 * fltHz) + 0.15 * os.oscrs(3.0 * fltHz);
moddepth = wowf * 600.0;       // up to ~+/-12.5 ms of delay swing
delsamp  = basedel + moddepth * (0.7 * wowLFO + 0.3 * fltLFO);
wowflutter = de.fdelay(maxdel, delsamp);

// --- hysteresis / saturation (Jiles-Atherton) ---------------------------------------------------
Ms = 380.0; a = 720.0; alpha = 0.015; k = 380.0;  // ferromagnetic params (hysteresis.lib example values)
c   = 0.25 + chr * 0.65;                           // 0.25 open/dynamic .. 0.9 compressed/controlled
// ja_processor scales the input by db2linear(-50) into the curve and gain-compensates `drive` on the
// way out, so drive sets saturation CHARACTER at ~constant level. A wide range (to ~54 dB) is needed to
// push an audio-level signal from clean into crunch; the core clamps magnetization to [-1,1] so it
// stays bounded even when driven hard.
sat = hy.ja_processor(Ms, a, alpha, k, c, ba.db2linear(drive * 54.0), 1.0);

process = wowflutter : sat;
