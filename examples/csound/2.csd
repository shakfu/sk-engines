<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; MIDI-DRIVEN variant: the keyboard plays the MAIN voice (no always-on drone). Silent until you play
; MIDI - that is expected; the centre LED is still cyan and the Play LEDs are lit (the patch IS loaded).
;
; The platform delivers NoteOn only (no note-off), so each note is a fixed ~0.6 s event: this is a
; polyphonic STAB/LEAD (every NoteOn spawns its own instance, so chords work), not a hold-while-pressed
; pad. Knobs shape the timbre while you play:
;   SIZE (sizeA) = brightness (filter cutoff), MIX (mixA) = level, PITCH (speedA) = sub-octave body.
; (There is no instr 1 / schedule - nothing sounds on its own.)

instr MidiNote
  ifreq = p4
  ks chnget "sizeA"
  kp chnget "speedA"
  km chnget "mixA"

  ; detuned 3-saw super-saw at the played pitch, plus a PITCH-knob-controlled sub-octave for body
  a1 vco2 0.25, ifreq
  a2 vco2 0.25, ifreq * 1.005
  a3 vco2 0.25, ifreq * 0.995
  asub vco2 0.20 * kp, ifreq * 0.5
  amix = (a1 + a2 + a3) * 0.33 + asub

  acut tone amix, 300 + ks * 8000          ; SIZE opens the low-pass
  aenv linseg 0, 0.008, 1, p3*0.25, 0.6, p3*0.7, 0   ; attack / decay-to-sustain / fade, within the note
  kamp = 0.2 + km * 0.8
  outs acut * aenv * kamp, acut * aenv * kamp
endin
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
