declare name "chorus";
declare description "Stereo chorus - sk-engines Faust demo engine (generated wrapper).";

import("stdfaust.lib");

// Flat hsliders (no group boxes), each 0..1 like the platform knobs; the wrapper linear-maps the
// platform 0..1 into these ranges automatically. Labels are the manifest binding keys.
rate  = hslider("rate",  0.30, 0, 1, 0.001) : si.smoo;   // LFO rate
depth = hslider("depth", 0.50, 0, 1, 0.001) : si.smoo;   // modulation depth
del   = hslider("delay", 0.40, 0, 1, 0.001) : si.smoo;   // base delay
mix   = hslider("mix",   0.50, 0, 1, 0.001) : si.smoo;   // dry/wet

rateHz = 0.05 + rate * 5.0;                  // 0.05 .. 5 Hz
baseSamp = (0.005 + del * 0.015) * ma.SR;    // 5 .. 20 ms, in samples
modSamp  = depth * 0.005 * ma.SR;            // +/- 5 ms, in samples

// two slightly-detuned LFOs for stereo width
delL = baseSamp + modSamp * (0.5 + 0.5 * os.osc(rateHz));
delR = baseSamp + modSamp * (0.5 + 0.5 * os.osc(rateHz * 1.03));

wet(d, x) = de.fdelay(2048, d, x);           // 2048-sample max (compile-time), fractional delay

chL(x) = x * (1.0 - mix) + wet(delL, x) * mix;
chR(x) = x * (1.0 - mix) + wet(delR, x) * mix;

process = chL, chR;
