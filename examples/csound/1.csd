<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Fat detuned super-saw with a sub-octave - thicker and darker than 0.csd's clean fifth, so the
; difference is obvious when you Alt+PITCH between the two slots. Same control channels and the same
; safe opcode vocabulary as 0.csd (table-less vco2 + core tone), so it compiles the same way.
;   PITCH (speedA) = pitch, SIZE (sizeA) = brightness, MIX (mixA) = level.

instr 1
  kp chnget "speedA"
  km chnget "mixA"
  ks chnget "sizeA"
  kf = 40 + kp * 300              ; lower range -> bassier than 0.csd
  a1 vco2 0.22, kf
  a2 vco2 0.22, kf * 1.006        ; detune up   -> slow beating
  a3 vco2 0.22, kf * 0.994        ; detune down -> fatter
  asub vco2 0.30, kf * 0.5        ; sub-oscillator an octave below
  amix = (a1 + a2 + a3) * 0.33 + asub * 0.5
  acut tone amix, 180 + ks * 5000 ; SIZE opens the low-pass
  kamp = 0.1 + km * 0.8
  outs acut * kamp, acut * kamp
endin

; MIDI pluck: a brighter detuned two-saw stab so notes sit above the drone (p4 = Hz).
instr MidiNote
  ks chnget "sizeA"
  aenv expon 1, p3, 0.001
  a1 vco2 0.4, p4
  a2 vco2 0.4, p4 * 1.006
  asig tone (a1 + a2) * 0.5, 300 + ks * 6000
  outs asig * aenv, asig * aenv
endin

schedule(1, 0, 100000)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
