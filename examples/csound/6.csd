<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Self-playing generative line. A sample-and-hold random opcode (randomh) picks a new note on each
; metro tick, quantized to semitones; the same metro shapes a plucky amplitude. The patch plays itself
; with no input - showing Csound's algorithmic side - and MIDI plays over the top. Table-less throughout.
;   PITCH (speedA) = root      SIZE (sizeA) = brightness   MODSPEED (modspA) = note rate
;   MODAMP (modampA) = range   MIX (mixA) = level

instr 1
  kp  chnget "speedA"
  ks  chnget "sizeA"
  kms chnget "modspA"
  kma chnget "modampA"
  km  chnget "mixA"
  krate = 1 + kms * 10                          ; 1..11 notes / sec
  krng  = 5 + kma * 19                          ; pitch range in semitones
  ksemi randomh 0, krng, krate                  ; sample-and-hold random, new value each 1/krate s
  ksemi = int(ksemi)                            ; quantize to whole semitones
  kroot = 110 * (1 + kp)                        ; root 110..220 Hz
  kfreq = kroot * pow(2, ksemi / 12)            ; semitone -> frequency ratio
  ktrig metro krate
  kamp  port ktrig, 0.05                        ; trigger -> plucky decay
  asaw  vco2 0.5, kfreq
  asig  moogladder asaw, 300 + ks * 5000, 0.5   ; SIZE opens the filter
  aout  = asig * kamp * (0.2 + km * 0.8)
  outs aout, aout
endin

; MIDI plays the same timbre over the generative line (p4 = Hz).
instr MidiNote
  ks chnget "sizeA"
  aenv expon 1, p3, 0.001
  asaw vco2 0.5, p4
  asig moogladder asaw, 300 + ks * 5000, 0.4
  outs asig * aenv, asig * aenv
endin

schedule(1, 0, 100000)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
