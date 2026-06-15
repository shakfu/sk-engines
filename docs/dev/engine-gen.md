# Design: manifest-driven engine generator (Faust, then unified with gen~)

**Status: v1 implemented** (the `chorus` demo). Goal: author a **new simple Faust effect or instrument**
with a `.dsp` + a small **JSON manifest** and **no hand-written C++** — `make`-it and it's a flashable
engine. This generalises the pattern the gen~ generator already uses (`scripts/gen_engine.py`: manifest +
marker-wired build + preserved glue) to the Faust path, which was previously all hand-written.

**What shipped:** the shared runtime (`src/engine/faust/{faust_capture.h,faust_fx.h}` —
`FaustEngine<Traits>` + the zone-capture `CaptureUI`), the generator
(`scripts/engine_gen_common.py` + `scripts/gen_faust_engine.py`, `make engine-gen`), and the `chorus`
demo (`src/engine/chorus/chorus.{dsp,json}` → generated `chorus_engine.h`, `ENGINE=chorus` at ~80.5%
SRAM_EXEC, `host/test_chorus.cpp`, the control diagram from the same manifest). The hand-written
`reverb`/`tape` wrappers were also **refactored onto the shared `faust_capture.h`** (their private
`CaptureUI`/`Bind`/`Role` deleted): one zone-capture implementation now serves the generator and both
rich engines, re-verified host-green (`reverb`/`tape`/`shuttle` host tests) with reverb's SRAM_EXEC cost
+288 B (a `CaptureUI<false>` template flag drops the generator-only slider-default capture from the
hand-written path, so they pay nothing extra for it).

This file scopes the design; the plan in [§7](#7-implementation-plan) was the build order.

## 1. Scope

**In scope (v1):** single-kernel, single-voice **mono or stereo** Faust effects/instruments — the common
case (a filter, drive, chorus, flanger, wavefolder, a knobs-only drone). Knobs → Faust sliders; the kernel
is placement-new'd into the SDRAM arena and driven block-by-block. The author writes only `<name>.dsp` +
`<name>.json`.

**Out of scope (v1), stays hand-written:** multi-kernel / runtime-switched / route-aware / dual-deck
engines. `reverb` is the exemplar — its wrapper is ~26% boilerplate (the bind tables + zone capture, which
this generator *would* emit) and ~74% real integration logic (the DoubleMono plate-only cap, per-deck
voice allocation, mode-switch reseed, peak metering). That 74% is genuine code, not data; v1 does not try
to express it in JSON. A later phase can emit the boilerplate + a preserved hand-glue region for those.

**Never in scope:** the DSP itself. Faust/gen~ *are* the high-level DSP description; JSON specifies the
**wrapper**, not the signal graph.

## 2. What the toolchain already gives us (so the manifest stays minimal)

The generated Faust kernel (`make faust-gen` → `faust_kernel_<name>.h`, `class mydsp` in
`namespace spotykach::<prefix><name>`, base types from `engine/faust_arch.h`) already exposes everything
structural:

- **`getNumInputs()` / `getNumOutputs()`** → mono vs stereo. *Not* stated in the manifest.
- **`buildUserInterface(UI*)`** → each slider's **label, range, default** via
  `addHorizontalSlider(label, zone*, init, min, max, step)`. So the **0..1 → native range** map is
  captured from the kernel at `init()` (exactly as `reverb_engine.cpp`'s `CaptureUI` does today). Ranges
  are **not** duplicated in the manifest — the `.dsp`/library is their single source.
- **`compute(int n, FAUSTFLOAT** in, FAUSTFLOAT** out)`**, `init(sr)` → the audio entry points.

So the manifest only carries what the kernel *cannot* tell us: **which platform control drives which
slider**, the capabilities, and a little metadata. Faust `.dsp` top-level `declare name/description` exist
but per-slider metadata is library-dependent and unreliable, so the binding lives in the side JSON, not in
`.dsp` metadata (decision in [§6](#6-open-decisions)).

## 3. The manifest

Minimal case — a flat-slider stereo FX (labels match the `hslider("…")` names in the `.dsp`):

```json
{
  "engine": "wfold",
  "backend": "faust",
  "title": "Wavefolder - drive + fold + tone",
  "knobs": {
    "Pitch":     "fold",
    "Position":  "drive",
    "Size":      "tone",
    "Envelope":  "symmetry",
    "Mix (SOS)": "mix",
    "Glow":      "bias"
  },
  "capabilities": ["CapOwnDisplay"],
  "features": { "wet_dry": "mix", "soft_limit": true }
}
```

- `knobs` maps the **platform control names** (same vocabulary as the control-diagram specs in
  `docs/diagrams/controls/`) → **Faust slider labels**. The generator resolves the control name to its
  `ParamId` (`Pitch`→`Speed`, `Position`→`Pos`, `Size`→`Size`, `Envelope`→`Env`, `Mix (SOS)`→`Mix`,
  `Cycle`→`ModSpeed`, `Glow`→`ModAmp`).
- `features` is a small, fixed vocabulary the generator expands: `wet_dry` (name a slider/knob as a
  software dry/wet crossfade — for kernels with no internal mix), `soft_limit` (cubic soft-clip the bus),
  later `meter` (own-display level ring). Anything outside the vocabulary → hand glue, not JSON.

Expanded entry for the rare **repeated-label** case (Dattorro's "Diffusion 1" lives in both an `Input` and
a `Feedback` box, and one knob can drive two zones). The dict value becomes an object/list:

```json
"Size": [ { "label": "Diffusion 1", "box": "Input", "slot": 0 },
          { "label": "Diffusion 2", "box": "Input", "slot": 1 } ],
"Envelope": { "label": "Wet/Dry Mix", "invert": true }
```

i.e. the binding key is `(box, label) → role(+slot, +invert)`, exactly the `Bind` struct reverb uses
(`{ section, label, role, slot, invert }`). Simple FX never need this; it exists so the schema can express
the hard case without a different format.

## 4. What the generator emits

For `<name>.dsp` + `<name>.json`:

1. **`faust_kernel_<name>.h`** — via the existing `make faust-gen` (cyfaust `compile -b cpp`). The
   generator registers `<name>` in the Makefile's `FAUST_KERNELS` list so this happens automatically.
2. **`src/engine/<name>/<name>_engine.h`** — the wrapper. For a simple FX this is fully generated and
   never edited: a `const Bind[]` table from the manifest, and a thin `class <Name>Engine : public IEngine`
   built on the shared Faust adapter ([§5](#5-shared-faust-runtime-new)) that captures zones, forwards
   `set_param`/`param` with range mapping, runs `compute` with mono/stereo marshalling from
   `getNumInputs/Outputs`, applies the `features`, and reports `capabilities()`.
3. **Build wiring**, idempotent and marker-delimited (reusing `gen_engine.py`'s `_upsert` + `>>> gen:<name>
   >>>` mechanism, renamed generically): the `ENGINE=<name>` block in `Makefile`
   (`-DSPK_ENGINE_<NAME>`, the kernel registration, `ENGINE_SOURCES`), the `#elif defined(SPK_ENGINE_<NAME>)`
   block in `src/engine/engine_select.h`, and the `CMakeLists.txt` branch.
4. **Validation at generate time**: the generator parses the slider labels out of the generated
   `faust_kernel_<name>.h` (`addHorizontal/VerticalSlider("…")`) and checks every manifest label exists —
   catching a typo or a renamed slider before the C++ ever compiles. It can also print a default
   `knobs` mapping (sliders in declaration order over the 6 plain knobs, like gen~'s `_PRIMARY`) to seed a
   new manifest.

The generated `<name>_engine.h` is **preserved across regeneration** unless `--force-glue` (same rule as
gen~), so a hand-edited engine survives a kernel re-export.

## 5. Shared Faust runtime (new): `src/engine/faust/`

Today the zone-capture `CaptureUI` is **duplicated** (a full version in `reverb_engine.cpp`, a trimmed one
in `tape/tapefx.h`). Factor it into a shared header, mirroring how `src/engine/gen/` shares `GenEngine<W>`
+ `genlib_arena`:

- **`faust/faust_capture.h`** — the generic `CaptureUI : UI` (label+box → zone\*, captures range) and the
  `Bind`/`Role` structs, lifted from `reverb_engine.cpp` verbatim.
- **`faust/faust_fx.h`** — a `FaustFx<Kernel>` helper: placement-new the kernel in the arena, bind a
  `Bind[]` table, `set(role, v01)` range-maps to the zone(s), `process(in, out, n)` with mono/stereo
  marshalling + optional wet/dry + soft-limit. The generated `<name>_engine.h` is then ~20 lines: the
  `Bind[]` table + a `FaustFx`-backed `IEngine`.

`reverb` and `tape` are refactored onto `faust_capture.h` (their `CaptureUI` deleted) and **re-verified
behaviour-identical / host-tests green** — a safe dedup, not a behaviour change, proving the shared
runtime against the existing rich users. (Done: reverb/tape/shuttle host-green, reverb SRAM_EXEC +288 B;
the `CaptureUI<false>` flag keeps the generator-only default-capture off the hand-written path.)

## 6. Open decisions

1. **Side JSON vs `.dsp` metadata.** Recommend **side JSON**: uniform across Faust *and* gen~,
   tooling-friendly, and Faust per-slider metadata is library-dependent. Cost: a second file to keep
   aligned with the `.dsp` — mitigated by the generate-time label validation (§4).
2. **Extend `gen_engine.py` vs a new front-end.** Recommend **a small shared module**
   (`scripts/engine_gen_common.py`) for the marker-wired build registration + preserved-glue, used by both
   the gen~ generator (unchanged behaviour) and a new **`scripts/gen_faust_engine.py`** Faust front-end.
   Same manifest *schema* across both backends (`"backend": "faust" | "gen"`); different kernel build step.
3. **Demo engine.** A small **flat-slider stereo FX** so the demo `.dsp` is ~10 lines of `hslider()` (the
   `tapefx.dsp` style — no boxes, no repeated labels) and the manifest is the §3 minimal form. Candidate: a
   **wavefolder + tone** or a **chorus/flanger**. Exact pick at implementation.

## 7. Implementation plan

1. **Shared Faust runtime** — `src/engine/faust/{faust_capture.h,faust_fx.h}`; refactor `reverb` + `tape`
   onto it; `make ENGINE=reverb` / `ENGINE=tape` + `make -C host test` re-verified unchanged.
2. **Generator** — `scripts/engine_gen_common.py` (markers/upsert lifted from `gen_engine.py`) +
   `scripts/gen_faust_engine.py` (parse manifest, parse kernel sliders to validate/seed bindings, emit
   `<name>_engine.h`, register the kernel + wire the build). A `make engine-gen` / one-shot target.
3. **Demo** — write `<demo>.dsp` + `<demo>.json`, run the generator, `make ENGINE=<demo>` links and fits
   (watch SRAM_EXEC), add a host test (param round-trip + finite/bounded audio), generate its control
   diagram from the same manifest.
4. **Convergence (small, high-value)** — the manifest already holds the `knobs` → `ParamId` → slider map,
   so the generator can also emit the **control-diagram spec** (`docs/diagrams/controls/<name>.json`) and
   the **doc control table**: one manifest → engine + registration + diagram + doc, closing the duplication
   the control-spec work started.
5. **Docs** — update `docs/engine-types/faust.md` (the new "drop a `.dsp` + manifest" path) and link this
   file.

## 8. Risks / limits

- **Label drift**: if a `.dsp` renames a slider, the binding breaks — caught at generate time by parsing
  the kernel's `addSlider` labels (§4), not at runtime.
- **Library-generated labels**: `dm.*` stdlib demos emit their own labels/boxes (the Dattorro case); a
  manifest over those needs the expanded `(box,label)` form. Flat hand-written `hslider()` FX (the v1 demo)
  avoid it.
- **SRAM_EXEC**: the kernel lives in the SDRAM arena (placement-new), as today — generation adds no SRAM
  pressure beyond the wrapper.
- **Instruments**: a Faust synth needs CV/gate/MIDI → slider bindings (`cv_voct`→a freq slider,
  gate→a gate button). v1 covers knob→slider (FX, and knobs-only drones); the input-binding vocabulary is a
  straightforward schema extension, called out but not built in v1.
- **ROI**: this is machinery for a backend with ~2 engines today. It's justified only by the stated goal —
  making *new* simple Faust FX near-zero-effort. If that goal holds, v1 pays for itself on the third engine.
