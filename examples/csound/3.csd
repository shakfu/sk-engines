<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Resonant "acid" voice: a saw through a self-oscillating Moog ladder filter, with an LFO sweeping the
; cutoff - the squelchy resonant character vco2+tone can't make. Demonstrates the core moogladder
; (resonant low-pass) and lfo opcodes, both table-less.
;   PITCH (speedA) = pitch        SIZE (sizeA) = base cutoff      FEEDBACK (fbA) = resonance
;   MODSPEED (modspA) = LFO rate  MODAMP (modampA) = sweep depth  MIX (mixA) = level

instr 1
  kp   chnget "speedA"
  ks   chnget "sizeA"
  kres chnget "fbA"
  kms  chnget "modspA"
  kma  chnget "modampA"
  km   chnget "mixA"
  kfreq = 40 + kp * 200
  klfo  lfo 1, 0.05 + kms * 8, 1                  ; triangle LFO, 0.05..8 Hz
  kcut  = 200 + ks * 4000 + klfo * kma * 3000     ; SIZE sets the floor, the LFO sweeps above it
  kcut  limit kcut, 60, 12000                     ; keep the cutoff in a sane range
  asaw  vco2 0.4, kfreq
  asig  moogladder asaw, kcut, 0.2 + kres * 0.75  ; FEEDBACK -> resonance (near 0.95 self-oscillates)
  kamp  = 0.1 + km * 0.8
  outs asig * kamp, asig * kamp
endin

; MIDI pluck through the same ladder, so notes share the patch's resonant character (p4 = Hz).
instr MidiNote
  ks   chnget "sizeA"
  kres chnget "fbA"
  aenv expon 1, p3, 0.001
  asaw vco2 0.5, p4
  asig moogladder asaw, 300 + ks * 5000, 0.2 + kres * 0.7
  outs asig * aenv, asig * aenv
endin

schedule(1, 0, 100000)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
