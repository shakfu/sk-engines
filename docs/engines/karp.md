# karp engine

A dual resonator / pluck voice (engine #1 in `docs/engine-ideas.md`, "Resonator / Pluck"), built on
the Mutable Instruments **Rings** DSP (`src/engine/karp/thirdparty/rings`) rather than a hand-rolled Karplus-Strong
loop. Each deck wraps one `rings::Part` (mono, polyphony 1): modal bodies, sympathetic strings,
plucked strings, and an FM voice.

The engine builds and runs, the host tests pass, the firmware links and fits, and - as of the pitch
investigation below - the PITCH knob tracks correctly on hardware. The one remaining item is a tuning
nicety (the pluck attack transient), not a defect. The long pitch hunt and its real root cause are
recorded under "Resolved: the PITCH-knob bug" at the end; read it before touching the pitch path.

---

## Status at a glance

- Implemented and integrated end to end (engine, build system, host test).
- Host test suite passes: `make -C host test` -> `OK: all karp checks passed`.
- Firmware links and fits: `make ENGINE=karp` -> SRAM_EXEC ~96% (183 KB / 186 KB), no overflow.
- ARM compile of all Rings/stmlib sources is clean.
- PITCH tracks on hardware (the original "knob does nothing" bug is fixed - see the end).
- One open tuning item: the pluck attack is bright/percussive ("thomp"); the pitched fundamental is
  clear but the broadband attack could be softened. Not a defect, just voicing.

---

## Concept and modes

The reel/slice/drift mode switch (`ConfigId::Mode`, per deck - mirrors granular's int mapping:
0 = Slice, 1 = Reel, 2 = Drift) selects how the resonator is excited. Alt+PITCH (`ParamId::Aux`,
`CapAux`) selects the Rings model. Two orthogonal axes.

- **Reel** - the resonator is fed from *outside* (Rings `internal_exciter = false`): the live input
  drives it continuously (a body you can play with audio), and each **trigger** injects a short ~4 ms
  noise burst so the pad/gate/MIDI plucks it. Silent when idle (no constant drone). `SIZE` (damping)
  sets how long a triggered note sustains.
- **Slice** - discrete **plucks** (`internal_exciter = true`): each trigger strums Rings' internal
  exciter at the current note. `MODFREQ` (`ModSpeed`) above a threshold turns on a tempo-synced
  triad+octave arp.
- **Drift** - a scatter cloud: a free-running internal scheduler auto-strums at randomized
  intervals/notes; `MODFREQ` = density, `MOD_AMT` = randomization spread.

Slice and Drift use Rings' internal exciter only - they feed **silence** into the resonator, not the
live input. Only Reel is fed from outside. (Feeding the live input into the internal-exciter modes
layered a continuous unpitched drone over every pluck; see the resolved-bug section.) `MODFREQ`/cycle
defaults to **off** (engine-seeded to 0 via the param cache); a non-zero default previously free-ran
the Slice arp from boot.

### Control map (per deck)

| Knob | `ParamId` | Rings target |
|---|---|---|
| PITCH | `Speed` | note (pitch) -> `performance_state.note` |
| SIZE | `Size` | `patch.damping` (decay/sustain) |
| POS | `Pos` | `patch.position` (excitation/pickup position) |
| ENV | `Env` | `patch.brightness` |
| MOD_AMT | `ModAmp` | `patch.structure` |
| SOS | `Mix` | dry/wet |
| MODFREQ | `ModSpeed` | Drift density / Slice arp rate |
| Alt+PITCH | `Aux` | model: modal / sympathetic-string / string / FM / string+reverb |

`capabilities() = CapOwnDisplay | CapDualDeck | CapAux | CapTransport`. Deck A -> left out, deck B ->
right out. Output is soft-clipped.

### Knob defaults (engine-seeded)

The platform seeds the SIZE/ENV/MODFREQ knob start positions from each engine's param cache (the same
`param()`-seed mechanism as POS/MOD_AMT/Alt+PITCH), so karp carries sensible defaults without changing
other engines:

- **ENV = 0.5.** ENV drives Rings brightness, and at 0 the excitation filter collapses the pluck to
  near-silence - a 0 default made the voice silent when a pad was pressed. 0.5 boots it audible. (Note:
  karp's ENV morphs brightness; it is not the granular amplitude-envelope-shape knob the manual
  describes for the same physical control.)
- **SIZE = 0.5.** SIZE is `patch.damping`, whose decay (rt60) is steep near the top, so the knob is very
  sensitive there; 0.5 centres it in a usable range. (Was 1.0.)
- **MODFREQ/cycle = 0.** Off by default so the Slice arp / Drift scatter does not free-run from boot.

Other engines are unchanged: granular and delay carry their own 1.0 SIZE / 0.3 cycle in their caches;
ENV stays 0.0 (granular's documented "envelope off" at fully CCW).

The display (`render`) draws, per deck: a mode-coloured energy meter (Reel yellow, Slice blue, Drift
purple), a white **pitch dot** whose ring position equals `pitch_n` (a live readout used as a
diagnostic), a play-LED flash on each trigger, and the mode L/C/R indicators.

**Model selector (Alt+PITCH).** Changing the Rings model has no dedicated hardware indicator, so on a
change `render` briefly (~0.7 s, `kModelShowFrames`) draws the five model options as evenly-spaced
points around the ring - the selected model bright, the rest dim - in place of the pitch dot. A
per-deck `model_show` countdown, tripped in `set_param(Aux)` only when the model actually changes,
gates it. This mirrors edrums' model-number flash; the host test asserts the selector lights more
points than the lone pitch dot and reverts after the window.

### Level note

Reel's external-exciter path is ~4x quieter than Rings' internal plucker, so the Reel trigger burst is
boosted (gain `2.0f` in the Reel branch of `process`) to match a Slice pluck (~0.4 peak). That `2.0f`
is the Reel level knob if it needs nudging.

---

## Architecture (PIMPL)

`KarpEngine` (`src/engine/karp/karp_engine.h`) exposes only the `IEngine` interface plus an opaque
`struct Impl* _p`. All Rings/stmlib types live in `karp_engine.cpp` inside `struct KarpEngine::Impl`.

This is mandatory, not stylistic: `stmlib/stmlib.h` declares a global `namespace impl` (for its
`STATIC_ASSERT` helper). The composition root `src/app.cpp` has a `static AppImpl impl;` instance, and
`app.cpp` includes the engine header via `engine_select.h`. If the header pulls Rings/stmlib, the
`namespace impl` collides with `app.cpp`'s `impl` and `app.cpp` fails to compile
("expected primary-expression before '.' token"). Keeping Rings out of the header avoids it.

`Impl` (and the two ~108 KB Parts + two 64 KB reverb buffers it owns) is placement-new'd into the
injected SDRAM arena at `init()`. Rings runs at 48 kHz with `kMaxBlockSize = 24`, so the 96-sample
platform block is processed in four chunks of <=24.

---

## Build system

Dependencies vendored under `src/engine/karp/thirdparty/` (colocated with the engine that owns them;
the `KARP_TP`/`KARP_INC` make vars scope the sources and `-I` to the karp build only):
- `rings` - Mutable Instruments Rings (only `dsp/` is used).
- `stmlib` - Mutable support library (hard dependency of Rings).

The vendored tree is **trimmed to the exact compile closure** - only the `.cc`/`.h` files karp actually
pulls in (computed from the compiler's own `.d` dependency output), plus `stmlib/LICENSE`/`README.md`.
This is ~34 files / ~320 KB, down from the ~50 MB full checkouts (the bulk was `stmlib/third_party` and
Rings' `hardware_design`/`drivers`/`ui`/`bootloader`, none of which the DSP needs). If you later add a
Rings model or stmlib helper that includes a header that was pruned, the build will fail with a
"file not found"; re-fetch that file from upstream. Rings' MIT notice lives in each source-file header
(retained by keeping the used files); stmlib keeps its top-level `LICENSE`.

Compiled sources for karp (engine + Rings DSP + stmlib): `karp_engine.cpp`,
`rings/dsp/{part,string,resonator,fm_voice}.cc`, `rings/resources.cc`,
`stmlib/dsp/{units,atan}.cc`, `stmlib/utils/random.cc`.

Three build gotchas, all handled:

1. **`-DTEST` on host only.** stmlib gates its ARM `ssat`/`usat`/`vsqrt` asm behind `#ifndef TEST`;
   the desktop build must define `TEST` to use the portable C paths. The firmware build must NOT
   define it (uses the asm). `host/Makefile` adds `-DTEST -I$(KARP_TP)`; the root `Makefile` adds
   `$(KARP_INC)` (`-I src/engine/karp/thirdparty`) without `-DTEST`.
2. **`-DM_PI` on the ARM build.** stmlib's filters use `M_PI`, which strict `-std=c++17` does not
   expose from `<cmath>` on `arm-none-eabi`. The karp branch of the root `Makefile` adds
   `-DM_PI=3.14159265358979323846`.
3. **`OPT = -Os` for karp only.** Rings (~30 KB of code+tables) overflows the 186 KB execution SRAM at
   `-O2` (overflow ~5.4 KB). The karp branch sets `OPT = -Os`, which fits (~96%) and leaves other
   engines at `-O2`. (`-Os` was briefly suspected of miscompiling the pitch path; it does not - the
   pitch bug was elsewhere. A Rings-only `-O2` override was tried and reverted.)

Registered in: `src/engine/engine_select.h`, root `Makefile` (`ENGINE=karp`, `engine-karp` flash
target), `CMakeLists.txt`, `host/Makefile` (`test-karp`).

The Daisy core Makefile already compiles `.cc` (not just `.cpp`), and object names are flat-by-basename
with no collisions among the Rings/stmlib files.

### A dead end to not repeat: QSPI table relocation

To recover SRAM at `-O2`, an attempt routed the Rings lookup tables (`resources.o`/`units.o` rodata,
~24 KB) into the `QSPIFLASH` region via `alt_sram.lds`. It linked (SRAM_EXEC dropped to 89%) but
produced a **1.7 GB `.bin`**: `BOOT_SRAM` builds one contiguous SRAM image, and `objcopy -O binary`
zero-filled the address gap from SRAM (0x24000000) to QSPI (0x90000000). Reverted. Any future
table-relocation needs proper LMA handling / a separate QSPI programming step, not a naive `> QSPIFLASH`.

---

## Files

- `src/engine/karp/karp_engine.h` - slim `IEngine` + opaque `Impl*` (no Rings includes).
- `src/engine/karp/karp_engine.cpp` - `Impl` with the Rings wiring, modes, schedulers, control map.
- `host/test_karp.cpp` - host tests (param round-trip, mode switch, Slice decay, Reel quiet-idle +
  sounds-on-trigger, Drift scatter, MIDI note, pitch-tracking, brightness control).
- Build edits: root `Makefile`, `CMakeLists.txt`, `host/Makefile`, `src/engine/engine_select.h`.
- Platform edit (affects all engines): `src/ui/core.ui.cpp` - see "absolute pitch" below.

Nothing is committed. `src/engine/karp/thirdparty/{rings,stmlib}` are vendored copies (MIT); consider
submodules or retaining their licenses.

---

## Resolved: the PITCH-knob bug

**Symptom.** On the device, turning the PITCH knob did not change the audible pitch, even though an
on-ring pitch dot tracked the knob. On the desktop host the full pitch path worked.

**Root cause.** `cv_voct()` (the V/Oct CV input handler) *overwrote* the knob's pitch every block.
`read_cv()` in `src/ui/core.ui.cpp` calls `_engine.cv_voct(...)` for both decks at ~500 Hz, and the
old handler did `deck[d].pitch_n = clampf(value, 0, 1)`. So `set_param(Speed)` would write `pitch_n`
from the knob and microseconds later the next `read_cv()` clobbered it with the V/Oct reading. With
nothing patched into the V/Oct jack, that reading is a constant, so `pitch_n` was pinned and the knob
was inert. Worse, `CalibratedVOct::Process()` returns a *semitone offset* (0 at 0V), which the old code
wrongly `clampf`-ed into `[0,1]` and treated as an absolute pitch - pinning the unpatched case to
`pitch_to_note(0)` = C1.

**Why the dot lied.** The engine's own `render()` draws the dot from `pitch_n`, and `render()` runs in
the same UI tick immediately after `set_param(Speed)` - before the next `read_cv()` clobber. So the dot
captured the knob value while the audio path (one clobber later) saw the pinned value. The moving dot
was the single most misleading signal in the whole hunt.

**Fix.** V/Oct CV is now an **additive transposition**, not an override. `cv_voct()` stores a per-deck
`cv_semis` and touches nothing else; the pluck note is `base_note(dk) = pitch_to_note(pitch_n) +
cv_semis`, routed through every pluck site (knob, gate/pad via `trigger_here`, Slice arp, Drift, MIDI).
Unpatched -> `cv_semis == 0` -> the knob alone controls pitch; patched -> CV transposes on top. It does
not rewrite `dk.note` per block, so it never clobbers a trigger's arp/drift offset.

**Regression test.** `host/test_karp.cpp` now calls `cv_voct()` around the pitch checks (the prior
tests never did, which is exactly why the bug was invisible on host): it asserts the knob still spans
pitch while `cv_voct(0)` is hammered in, and that `cv_voct(+12)` transposes up ~1 octave.

### Red herrings and secondary fixes found along the way

The hunt started by (wrongly) suspecting a gcc `-Os` miscompile of Rings and a frozen note->frequency
path. Disproving those turned up three *real* but separate issues, all fixed:

- **`-Os` miscompile - disproven.** A diagnostic that drove `dk.note` directly in `process()` produced a
  clean pitched sweep on device, so the note->frequency path was never frozen. The Rings-only `-O2`
  override tried for this is reverted; karp is back to plain `-Os`.
- **Input feed into Slice/Drift (real fix).** The internal-exciter modes were fed the live audio input,
  which Rings sums into the resonator on top of the pluck - layering a continuous unpitched drone over
  every note (audible on the live ADC, silent on the host's zero input). Now they feed silence; only
  Reel takes external input. This is the "much clearer pitch differentiation" improvement.
- **Cycle default (real fix).** `MODFREQ`/`ModSpeed` ("cycle") was seeded to 0.3 by a shared literal in
  `core.ui.cpp`, above the Slice arp threshold, so the arp free-ran from boot (the "replucking"). The
  seed is now engine-derived (like Pos/ModAmp/Aux): karp seeds 0 (arp off), granular carries its own
  0.3 so it is unchanged, edrums keeps its 0.0.

### Still open (voicing, not a bug)

The pluck attack is bright/percussive - a residual "thomp" over the clear fundamental. It is the Rings
exciter transient (the excitation filter cutoff is `8 * filter_cutoff`, very bright). Tunable by
lowering the exciter brightness coupling or level if a softer attack is wanted.

---

## Other notes / TODO

- The pitch dot in `render()` is drawn from `pitch_n`; note it reflects the *knob*, not the V/Oct CV
  transposition. It misled the pitch hunt (it tracks the knob even when the audio pitch was pinned);
  keep or restyle.
- Reel "drone": for a sustained drone, set `SIZE` high and trigger - it rings for a long time (there
  is no longer an unconditional drone).
- Drift randomization uses the engine's own LCG; Slice arp reads `ITransport::tempo()`.
- Two full `rings::Part` instances run every block; at `-Os` on the 480 MHz M7 there should be ample
  headroom, but CPU has not been measured on hardware (`Meter::cpu`).
- Models 4/5 (FM, string+reverb) are wired via `Aux`; karp stays at `-Os` (fits ~96%), so there is no
  pressure to drop them for SRAM.
