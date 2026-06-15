# gen~ engines (gen-dsp)

Author the DSP in **Max/MSP [gen~]**, export it to C++, and translate the export into an `IEngine` module with [gen-dsp](https://github.com/shakfu/gen-dsp). It is the gen~ counterpart of the [Faust](faust.md) method, but with **the least hand-written firmware**: the wrapper is fully generic, and one host command emits a whole engine directory. The trade-off is that it is currently capability-minimal - stereo audio + knob params only (`capabilities() = 0`).

**Implementation:** [gigaverb](../engines/gigaverb.md) (`ENGINE=gigaverb`). Each gen~ export becomes its own engine, `ENGINE=<name>`. For the `IEngine` contract and knob grammar, see [../engines/README.md](../engines/README.md).

## Concept

gen~ exports a self-contained C++ DSP kernel plus **genlib**, its small runtime (allocation, math, `[data]`/`[buffer]` support). gen-dsp's Daisy backend already compiles that kernel for the H7 and bridges it to a host through a tiny C interface. We reuse exactly that bridge and **discard gen-dsp's generated firmware** (its own `main()`, audio callback, and board knob-mapping) and **its private allocator** - the Spotykach platform owns the board, the audio loop, and the arena.

So a gen~ engine is: gen-dsp's genlib-isolation bridge + the shared `src/engine/gen/` family (a generic `IEngine` wrapper and an arena-bound genlib runtime) + a small generated per-engine glue file that binds the two and maps knobs to gen~ parameters.

```text
Max gen~ export ──gen-dsp──▶ _ext_daisy.{cpp,h}  (wrapper_* C interface, opaque GenState*)
(gen_exported.cpp           + gen/ (the export)  } header-isolated genlib side
 + gen_dsp/ genlib)         + gen_buffer.h, daisy_buffer.h
                                     │
   src/engine/gen/  ┌───────────────┴────────────────┐
   (shared family)  │ genlib_arena.cpp  gen_engine.h  │  GenEngine<W> : IEngine
                    └───────────────┬────────────────┘
   <name>/<name>_engine.h ──────┘  traits W -> <name>_daisy::wrapper_*  +  ParamId map
```

## Header isolation (why it is split this way)

genlib's headers (`genlib_ops.h`, `genlib_platform.h`) define `exp2`/`trunc` and enable ARM fast-math paths that collide with libDaisy and modern `<cmath>`. They are therefore quarantined: **only** `<name>/_ext_daisy.cpp` and `src/engine/gen/genlib_arena.cpp` include genlib (with gen-dsp's `WIN32` / `GENLIB_NO_DENORM_TEST` / `ARM_MATH_CM7`-undef guards). The platform side - `app.cpp` -> `engine_select.h` -> `<name>_engine.h` - sees only `_ext_daisy.h`: an opaque `typedef void GenState` and `wrapper_*` functions taking `float**`. The two sides meet at that C boundary and never share a translation unit. `t_sample` is `float` (genlib self-defines `GENLIB_USE_FLOAT32` for `__arm__`), so the audio buffers pass straight through.

## Memory model

gen~ allocates its `CommonState`, delay lines, and any `[data]`/`[buffer]` storage through genlib's `sysmem_*` C ABI. `src/engine/gen/genlib_arena.cpp` is gen-dsp's embedded genlib runtime with **only the allocator backend replaced**: a bump allocator over the platform's `EngineContext` SDRAM arena. `GenEngine::init()` calls `genlib_arena_bind(ctx.arena.base, ctx.arena.bytes)` **before** `wrapper_create()`, so every gen~ allocation lands in the same arena the reverb and reso engines use - no `malloc`, no static pool, `freeptr` is a no-op (bump). Consequence, as with the Faust reverbs: `SRAM` (data) is not the constraint; **`SRAM_EXEC` (code) is**. A large gen~ patch may need this engine's branch built at `-Os` (as reso is).

I/O is marshalled to the platform stereo bus in `GenEngine::process()`: the first two channels map through; extra gen~ inputs are fed silence and extra outputs are discarded; a mono gen~ output is duplicated to both channels.

## Control map

`GenEngine` forwards `set_param(ParamId, deck, v01)` to the per-engine glue, which maps each `ParamId` to a gen~ parameter index and linear-maps the normalized 0..1 value into that param's declared `[min,max]` (from `manifest.json`). That `index_of()` switch is **generated from a hand-authored `<name>.json` manifest** - the same declarative method as the Faust generator. The manifest's `knobs` block maps a control name to a gen~ param **name**:

```json
{ "engine": "gigaverb", "backend": "gen", "export": "src/engine/gigaverb/gen",
  "knobs": { "Pitch": "bandwidth", "Position": "revtime", "Size": "roomsize",
             "Envelope": "damping", "Mix (SOS)": "dry", "Glow": "tail",
             "Feedback": "spread", "EnvSize": "early" } }
```

The generator resolves each control name to a `ParamId` (shared with Faust; modifier-layer params with no friendly label - `Feedback`, `EnvSize`, ... - are written as their `ParamId` name), looks the param name up in the export's `manifest.json` for its index + range, **validates** it exists (a typo errors at generate time, not runtime), and emits `index_of()`. Bind only the `ParamId`s the platform delivers to a single-deck engine via `set_param()` - the six plain panel knobs (`Size/Pos/Speed/Env/Mix/ModAmp`) plus the no-capability modifier layers. `ModSpeed` is unreachable (MODFREQ routes to `set_mod_speed()`, which the generic wrapper does not override); `Aux`/`AltPos` need `CapAux`/`CapAltPos`. Without a manifest the generator falls back to a positional default (`gen param i -> _PRIMARY[i]`) to **bootstrap** a new engine. See [gigaverb](../engines/gigaverb.md) for a worked control map.

## Files

- `src/engine/gen/gen_engine.h` - generic `GenEngine<W> : IEngine` (lifecycle, arena bind, I/O marshalling, param scaling). Shared by every gen engine.

- `src/engine/gen/genlib_arena.{h,cpp}` - the arena-bound genlib runtime. Shared. The only hand-written divergence from gen-dsp's stock `genlib_daisy.cpp` is the allocator backend.

- `src/engine/<name>/` - one per export:

  - `gen/` - the copied gen~ export (`gen_exported.cpp/.h`, `gen_dsp/` genlib). **Generated; do not edit.**

  - `_ext_daisy.{cpp,h}`, `gen_ext_common_daisy.h`, `gen_buffer.h`, `daisy_buffer.h`, `gen_remap_inputs.h` - gen-dsp's isolation bridge. **Generated.**

  - `manifest.json` - the front-end-agnostic IR (I/O counts, params with min/max/default, buffers).

  - `<name>.json` - the **hand-authored manifest** (`knobs` map + the `export` path); the one file you write.
  - `<name>_engine.h` - the per-engine glue (traits struct + `ParamId` map), **generated from the manifest** (or a positional default to bootstrap); re-running the generator preserves it unless `--force-glue`.

- `scripts/gen_engine.py` - the host-side code generator (driver around gen-dsp).

- Registered in `src/engine/engine_select.h` and the root `Makefile` (`ENGINE=<name>` block, `gen-engines` codegen target) - both in marker-delimited blocks the generator upserts.

## Prerequisite (one-time)

gen-dsp lives in the same repo-local `.venv` as cyfaust (`.venv` is gitignored):

```text
.venv/bin/pip install -e /path/to/gen-dsp     # editable, so backend changes are picked up
```

gen-dsp runs only at codegen time on the host; the firmware build is plain `arm-none-eabi-g++`. The generated files are checked in, so a normal build needs no gen-dsp.

## Adding a gen~ engine (the process)

1. **Author + export in Max.** Build the patch in `gen~`, then export the code (the gen~ export inspector, or the `exportcode` message) to a directory containing `gen_exported.cpp` and `gen_dsp/`.

2. **Bootstrap once to read the params.** Run the generator positionally to run gen-dsp, keep the isolation bridge, drop gen-dsp's board `main()` + private allocator, sync the export's `manifest.json` (the param names + ranges), and emit a starter `<name>_engine.h` + wiring:

   ```text
   .venv/bin/python scripts/gen_engine.py /path/to/export myverb
   ```

   `<name>` must be lowercase `[a-z0-9_]`; the engine becomes `ENGINE=myverb`. This positional form emits a positional-default knob map - enough to read the gen~ param names out of `src/engine/myverb/manifest.json`.

3. **Map the knobs in a manifest.** Write `src/engine/myverb/myverb.json` with `"backend": "gen"`, an `export` path, and a `knobs` map (control name -> gen~ param name, from the synced `manifest.json`), then re-emit the glue from it:

   ```text
   make gen-engine MANIFEST=src/engine/myverb/myverb.json FORCE=1          # gen-dsp + glue
   make gen-engine MANIFEST=src/engine/myverb/myverb.json NOGEN=1 FORCE=1  # glue only, after a knob edit
   ```

   The generator validates every param name against the export and emits `index_of()` - you never hand-edit the C++. Batch-regenerate every gen~ engine with `make gen-engines` (analogue of `make faust-kernels`).

4. **Build + flash.** Engine switches go through `make clean` (see below):

   ```text
   make clean && make -j8 ENGINE=myverb     # the link prints SRAM_EXEC usage
   make ENGINE=myverb program-dfu           # flash (device in DFU mode first)
   ```

**Re-generating** after editing the patch or the manifest: re-run `make gen-engine MANIFEST=... FORCE=1` (drop `FORCE` to preserve the generated header).

**Removing** an engine: `scripts/gen_engine.py myverb --remove` deletes `src/engine/myverb/` and unwires both files. Run `make clean` once afterward (see below).

## Engine switching (use `make clean`)

Like every engine in this project, switch with a clean build - the `engine-<name>` convenience targets all `make clean` first. Two gen-specific reasons make this the guaranteed path:

- All gen engines share the wrapper object basenames `_ext_daisy.o` / `genlib_arena.o` (gen-dsp fixes the filenames). The `.engine-stamp` mechanism forces them to rebuild on a switch, but the project's pre-existing same-second `.engine-stamp` mtime race can still leave `app.o` stale on rapid back-to-back switches - a `make clean` is the reliable cure.

- After `--remove`, the deleted engine's stale `build/_ext_daisy.d` still names its now-deleted source, which make rejects until `make clean` clears it.

The platform/engine boundary guard is unaffected: gen sources live under `src/engine/`, and the platform (`hw/ui/memory/transport`) includes none of them. `make check-boundary` passes.

## Notes / limitations

- `capabilities() = 0`: no custom display, MIDI, CV-out, or sequencing. A gen engine that wants a meter could override `render()` (see passthrough); CV-out would need `process_cv()`. The generic wrapper is deliberately minimal.

- The control surface is designed in `<name>.json` (the `knobs` map); the positional default is only a bootstrap starting point. The generated `<name>_engine.h` is derived from the manifest, not hand-edited.

- gen~ buffers (`[buffer]`) are instantiated but nothing loads samples into them yet; a sample-loading path is future work.
