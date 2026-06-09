// faust-spike DSP source. Regenerate the kernel with:
//   make faust-gen           (wraps cyfaust; see Makefile / src/engine/reverb/README.md)
//
// A representative DSP-heavy block standing in for "a new subtractive/physical-model engine"
// (the reso/Rings flavour): a sawtooth through a Moog ladder VCF, shaped by an ADSR, stereo out.
// Compute is closed-form (no large tables), so this measures generated *code* size on the H7.
import("stdfaust.lib");
freq = hslider("freq", 220, 40, 4000, 0.01);
cut  = hslider("cutoff", 0.5, 0, 1, 0.001);
res  = hslider("res", 0.3, 0, 1, 0.01);
gate = button("gate");
env  = en.adsr(0.005, 0.12, 0.7, 0.25, gate);
voice = os.sawtooth(freq) * env : ve.moog_vcf(res, cut*12000);
process = voice <: _, _;
