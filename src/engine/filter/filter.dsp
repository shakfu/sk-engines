// filter.dsp - mono resonant low-pass with drive.
//
// Stage of the 'filter' PARALLEL dual-deck demo: FaustEngine runs one instance of this kernel per deck
// (deck A on the left channel, deck B on the right), each with its own cutoff/reso/drive/mix - so the two
// channels are filtered fully independently. Mono (1 in, 1 out), as DoubleMono requires.
import("stdfaust.lib");

cutoff = hslider("cutoff", 1.0, 0, 1, 0.001) : si.smoo;   // ~40 Hz .. ~20 kHz (exp)
reso   = hslider("reso",   0.1, 0, 1, 0.001) : si.smoo;   // Q
drive  = hslider("drive",  0.0, 0, 1, 0.001) : si.smoo;   // pre-filter tanh saturation
mix    = hslider("mix",    1.0, 0, 1, 0.001) : si.smoo;   // dry/wet

fc = 40.0 * pow(2.0, cutoff * 9.0);
q  = 1.0 + reso * 24.0;
wet(x) = ma.tanh(x * (1.0 + drive * 6.0)) : fi.resonlp(fc, q, 1.0);

process = _ <: *(1.0 - mix), (wet : *(mix)) :> _;
