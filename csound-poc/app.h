#ifndef CSOUND_APP_H
#define CSOUND_APP_H

#include <string>

// Our own orchestra (vs the bundled example's marimba): a single always-on sine
// voice whose pitch and level are driven live by the Pod's two knobs, read from
// the "knob1"/"knob2" control channels the host sets each loop.
//   knob1 -> pitch  (110..880 Hz)
//   knob2 -> level  (always audible floor, so there's sound even at zero)
static const std::string csdText = R"csd(
<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>

sr     = 48000
0dbfs  = 1
nchnls = 2

instr 1
  k1    chnget "knob1"
  k2    chnget "knob2"
  kfreq = 110 + k1 * 770        ; knob1 -> pitch 110..880 Hz
  kamp  = 0.05 + k2 * 0.35      ; knob2 -> level (audible floor so there's always sound)
  asig  vco2 kamp, kfreq        ; band-limited saw, table-less, k-rate modulatable
  outs  asig, asig
endin

schedule(1, 0, 100000)          ; trigger instr 1 from the orchestra (like the examples)

</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
)csd";

#endif
