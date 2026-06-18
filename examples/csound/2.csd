<CsoundSynthesizer>
<CsOptions>
</CsOptions>
<CsInstruments>
sr = 48000
0dbfs = 1
nchnls = 2

; MIDI-DRIVEN polyphonic lead/stab. The keyboard plays the main voice (no drone) - silent until you
; play (the patch IS loaded: centre LED cyan, Play LEDs lit). NoteOn-only, so each note is a fixed
; ~0.6 s event; every NoteOn spawns its own instance, so chords/polyphony work.
;
; POLYPHONY: Csound SUMS every active note into the output, so N notes at once can overflow 0dbfs and
; the DAC hard-clips (distortion). So voices do NOT call `outs` - they accumulate into a global bus
; (gaMix), and one always-on instrument (100) tanh-limits the SUM, keeping any number of notes bounded.
; The voice is also kept light (2 oscillators) so rapid retriggering doesn't spike the per-note CPU.
;
; Knobs while you play: SIZE (sizeA) = brightness, PITCH (speedA) = detune width, MIX (mixA) = drive.

gaMix init 0

; Cap simultaneous voices: each NoteOn spawns an instance (notes last ~0.6 s), so very fast playing
; can pile up many at once and overrun the CPU. Beyond this many active voices, new notes are skipped
; (a dropped note, not a glitch). Raise it if you have headroom, lower it if dense playing still chokes.
maxalloc "MidiNote", 6

instr MidiNote
  ifreq = p4
  ks chnget "sizeA"
  kp chnget "speedA"
  a1 vco2 0.2, ifreq
  a2 vco2 0.2, ifreq * (1 + 0.02 * kp)        ; PITCH widens the detune -> thicker
  acut tone (a1 + a2) * 0.5, 300 + ks * 8000   ; SIZE opens the low-pass
  aenv linseg 0, 0.008, 1, p3*0.25, 0.6, p3*0.7, 0
  gaMix = gaMix + acut * aenv                   ; accumulate into the shared bus (do not outs here)
endin

; Always-on output: soft-limit the SUM of all voices so polyphony can never hard-clip 0dbfs.
instr 100
  km chnget "mixA"
  kdrive = 0.4 + km * 1.2
  asig = tanh(gaMix * kdrive)
  outs asig, asig
  gaMix = 0                                     ; clear the bus for the next k-cycle
endin
schedule(100, 0, -1)                            ; run the output bus forever
</CsInstruments>
<CsScore>
</CsScore>
</CsoundSynthesizer>
