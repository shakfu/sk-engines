# Csound engine example patches

Reference orchestras for the `csound` engine (`ENGINE=csound`). Each is a full, correctly-structured
CSD that compiles on the Daisy build.

## Use

Copy the files into a `csound/` folder at the **root of the SD card**, keeping the numbered names:

```
<card>/csound/0.csd
<card>/csound/1.csd
```

Boot the unit (with the `csound` firmware flashed): it auto-loads the lowest-numbered patch. **Hold Alt
and turn PITCH** to scroll the selector (a dot per patch around the rings); **release** to switch live.
The centre mode LED is cyan for an SD patch, white for the built-in. MIDI NoteOn plays each patch's
`MidiNote` instrument over the top.

- `0.csd` — clean two-saw "fifth" chord (drone, knob-controlled; MIDI adds plucks over the top).
- `1.csd` — fat detuned super-saw with a sub-octave (drone; obviously different, for testing live switching).
- `2.csd` — **MIDI-driven, polyphonic**: the keyboard plays the main voice, no drone. Silent until you
  play. NoteOn-only means each note is a fixed ~0.6 s stab (chords work), not a hold-while-pressed pad.
  Voices accumulate into a global bus that an always-on instrument `tanh`-limits, so polyphony can't
  hard-clip `0dbfs` (the standard Csound pattern — a per-voice `outs` would sum to distortion). A
  `maxalloc` caps simultaneous voices so very fast playing drops notes instead of overrunning the CPU.

## Writing your own

Control channels the panel drives (read with `chnget`): `speedA` (PITCH), `mixA` (MIX), `sizeA` (SIZE),
`envA`, `fbA`, `modspA`, `modampA`, and the `B`-deck equivalents. Define `instr MidiNote` (p4 = freq Hz)
to be keyboard-playable.

Format rules that matter (the on-device CSD parser is line-oriented):
- Each section tag on its **own line** — `<CsScore>` and `</CsScore>` separately, never
  `<CsScore></CsScore>` on one line.
- Core opcodes only — no plugin opcodes, no soundfile I/O (`diskin`/`GEN01`); prefer table-less
  oscillators (`vco2`). A UTF-8 BOM and CRLF line endings are tolerated (the engine normalizes them).

See `docs/engines/csound.md` (user guide), `docs/dev/csound-impl.md` (internals), and the official orchestras in
`thirdparty/csound/Daisy/DaisyCsoundExamples/` for more.
