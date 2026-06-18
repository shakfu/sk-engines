<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Clean two-saw "fifth" chord. Drives off the panel knobs via control channels:
;   PITCH (speedA) = pitch, SIZE (sizeA) = brightness, MIX (mixA) = level.
; Uses only table-less vco2 + the core one-pole tone filter (both proven on the Daisy build).

instr 1
  kp chnget "speedA"
  km chnget "mixA"
  ks chnget "sizeA"
  kf = 55 + kp * 440
  a1 vco2 0.3, kf
  a2 vco2 0.3, kf * 1.5            ; a fifth up -> a chord
  asig tone (a1 + a2) * 0.5, 400 + ks * 7000
  kamp = 0.1 + km * 0.8
  outs asig * kamp, asig * kamp
endin

; MIDI-playable voice: a finite, exponentially-decaying pluck at the note pitch (p4 = Hz).
; The engine schedules this instrument by name on a NoteOn (see kMidiInstrName).
instr MidiNote
  ks chnget "sizeA"
  aenv expon 1, p3, 0.001
  asig vco2 0.5, p4
  asig tone asig, 400 + ks * 7000
  outs asig * aenv, asig * aenv
endin

schedule(1, 0, 100000)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
