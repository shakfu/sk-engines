// osc.dsp - drone oscillator (0 inputs, 1 output).
//
// Stage 1 of the 'voice' SERIES dual-deck demo (driven by deck A). An instrument: it generates sound
// from its knobs with no audio input, so it also exercises the 0-input path of the chain runtime. Its
// output feeds stage 2 (the filter, deck B).
import("stdfaust.lib");

freq  = hslider("freq",  0.3, 0, 1, 0.001);               // 0..1 -> ~40 Hz .. ~2.5 kHz (exp)
shape = hslider("shape", 0.0, 0, 1, 0.001) : si.smoo;     // saw <-> square morph
level = hslider("level", 0.5, 0, 1, 0.001) : si.smoo;     // output level

f   = 40.0 * pow(2.0, freq * 6.0);
osc = os.sawtooth(f) * (1.0 - shape) + os.square(f) * shape;

process = osc * level;     // 0 inputs, 1 output
