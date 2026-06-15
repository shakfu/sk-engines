// filter.dsp - resonant low-pass with drive (1 in, 1 out).
//
// Stage 2 of the 'voice' SERIES dual-deck demo (driven by deck B). It shapes whatever stage 1 (the osc)
// produces; the chain runtime feeds osc -> filter -> stereo bus.
import("stdfaust.lib");

cutoff = hslider("cutoff", 0.55, 0, 1, 0.001) : si.smoo;
reso   = hslider("reso",   0.2,  0, 1, 0.001) : si.smoo;
drive  = hslider("drive",  0.0,  0, 1, 0.001) : si.smoo;
mix    = hslider("mix",    1.0,  0, 1, 0.001) : si.smoo;

fc = 40.0 * pow(2.0, cutoff * 9.0);
q  = 1.0 + reso * 24.0;
wet(x) = ma.tanh(x * (1.0 + drive * 6.0)) : fi.resonlp(fc, q, 1.0);

process = _ <: *(1.0 - mix), (wet : *(mix)) :> _;
