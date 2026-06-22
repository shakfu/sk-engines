<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; Dub echo. A slow self-plucking source AND the MIDI notes feed one feedback delay line
; (delayr/delayw with a movable tap), so the patch sounds without MIDI but is also playable. SIZE sets
; the echo time, FEEDBACK the number of repeats, MODAMP darkens each repeat. Demonstrates core delay
; lines + feedback + a variable delay tap (deltapi). The wet sum is tanh-limited so feedback can't blow up.
;   PITCH (speedA) = pitch   SIZE (sizeA) = echo time   FEEDBACK (fbA) = repeats   MODAMP (modampA) = repeat tone   MIX (mixA) = wet

gaSrc init 0

instr 1
  kp chnget "speedA"
  ktrig metro 1.5                       ; a pulse every ~0.67 s
  kamp  port ktrig, 0.04                ; smooth the pulse into a plucky decay
  asig  vco2 0.5, 110 + kp * 220
  gaSrc = gaSrc + asig * kamp
endin

; MIDI notes feed the same delay (p4 = Hz).
instr MidiNote
  aenv expon 1, p3, 0.001
  asig vco2 0.45, p4
  gaSrc = gaSrc + asig * aenv
endin

; Always-on output: one shared feedback delay, dry+wet out, clear the bus.
instr 100
  ks  chnget "sizeA"
  kfb chnget "fbA"
  kma chnget "modampA"
  km  chnget "mixA"
  kdt = 0.08 + ks * 0.42                ; echo time 80..500 ms
  adump delayr 0.6                      ; 0.6 s delay buffer
  atap  deltapi kdt                     ; read a movable tap
  atap  tone atap, 800 + kma * 6000     ; darken each repeat (a tape-ish high-cut)
  delayw gaSrc + atap * (kfb * 0.95)    ; write source + feedback (capped < 1 so it decays)
  aout = gaSrc + atap * km              ; dry + wet
  outs tanh(aout), tanh(aout)
  gaSrc = 0
endin
schedule(100, 0, -1)
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
