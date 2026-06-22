<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Lush stereo reverb pad. A soft detuned-saw chord AND the MIDI notes both feed a shared bus that an
; always-on instrument runs through reverbsc (Csound's high-quality stereo reverb), so the tail blooms
; and rings out. Demonstrates a real built-in stereo effect + a long decay tail + the shared-bus routing
; pattern (so one effect processes the whole patch). The wet sum is tanh-limited so it can't hard-clip.
;   PITCH (speedA) = pitch   SIZE (sizeA) = reverb decay   ENV (envA) = damping (tone)   MIX (mixA) = dry/wet

gaL init 0
gaR init 0

instr 1
  kp chnget "speedA"
  kfreq = 80 + kp * 300
  a1 vco2 0.10, kfreq
  a2 vco2 0.10, kfreq * 1.005           ; slight detune -> slow chorus beating
  a3 vco2 0.10, kfreq * 1.5             ; a fifth -> a soft chord
  asig = a1 + a2 + a3
  gaL = gaL + asig
  gaR = gaR + asig
endin

; MIDI notes feed the same reverb bus (p4 = Hz), so played notes ring in the same space as the drone.
instr MidiNote
  aenv expon 1, p3, 0.001
  asig vco2 0.3, p4
  gaL = gaL + asig * aenv
  gaR = gaR + asig * aenv
endin

; Always-on output: reverberate the summed bus, blend dry/wet, soft-limit, then clear the bus.
instr 100
  ks chnget "sizeA"
  ke chnget "envA"
  km chnget "mixA"
  kfb  = 0.6 + ks * 0.39                ; SIZE -> feedback level (decay length), up to ~0.99
  kfco = 2000 + ke * 8000               ; ENV  -> damping cutoff (darker..brighter tail)
  aL, aR reverbsc gaL, gaR, kfb, kfco
  aoutL = gaL * (1 - km) + aL * km      ; MIX -> dry/wet
  aoutR = gaR * (1 - km) + aR * km
  outs tanh(aoutL), tanh(aoutR)
  gaL = 0
  gaR = 0
endin
schedule(100, 0, -1)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
