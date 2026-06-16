# graincloud - implementation notes

Developer notes for the `graincloud` engine (user doc: [`docs/engines/graincloud.md`](../engines/graincloud.md)).

## Architecture: a granular variant, not a standalone engine

`graincloud` is **the granular engine compiled with `SPK_GRAIN_GF`**, which swaps granular's grain DSP (`Generator`/`Vox`/`Window`) for a GrainflowLib cloud. It defines `SPK_ENGINE_GRANULAR` (so the platform treats it exactly as granular - recording, SD storage, dual-deck, crossfade, FX, UI all inherited unchanged) while `SPK_ENGINE_STR` is `"graincloud"`. This replaced an earlier *standalone* graincloud engine that reimplemented all that plumbing from scratch and kept hitting platform-integration bugs (record gesture, storage crash, crossfade traps); inheriting granular's proven plumbing removed that entire class of problems.

The single seam: `Deck::process_out()` calls `_generator.process(bus[0], bus[1])` once per sample. Under `SPK_GRAIN_GF`, `Generator::process()` (generator.cpp) sums a `GfCloud` instead of the `Vox` array; `Generator` keeps its whole interface/state so `Deck`/`Drifter`/`granular_engine` compile and drive it unchanged.

## GfCloud (`src/engine/granular/gf_cloud.{h,cpp}`)

The GrainflowLib cloud core, reading granular's `Buffer`:
- **Per-block/per-sample bridge.** Granular is per-sample; GrainflowLib is per-block (96). `GfCloud::process(out0,out1)` serves one sample, recomputing a 96-sample stereo block at each boundary (`compute_block`).
- **Buffer-reader seam.** `gf_i_buffer_reader<Buffer,float>` callbacks read granular's `Buffer` via `read_linear` (one channel each), report `rec_size()` as the buffer length, and use a Hann LUT for the envelope. A non-finite-position guard prevents `(int)NaN` OOB reads.
- **Memory.** Grain scratch (grain array + io_config arrays) are members of two **static** `GfCloud` instances in regular RAM (zero-initialized BSS, ctors run at startup when RAM is ready) - acquired per deck via `gf_cloud_acquire(ref)`. Only the audio `Buffer` is SDRAM. No arena threading (Generator::init has no arena), no SDRAM-static-ctor hazard, no platform-boundary breach.
- **Whole TU guarded** by `#ifdef SPK_GRAIN_GF`: it lives in the granular source wildcard, so for plain `ENGINE=granular` it compiles to an empty TU (no GrainflowLib include, granular unaffected).

The de-STL'd GrainflowLib itself is vendored under `src/engine/graincloud/thirdparty/grainflow/` (the de-STL changes - heap/throw/RTTI removed - are marked `// sk:` in those headers).

## Knob -> cloud param mapping

`Generator::process` (under the flag) reads granular's stored params and calls `GfCloud::set_params`:
- start (POS) -> cloud centre; size (SIZE) -> grain duration; shape (ENV) -> density; smoothed increment (Speed) -> transpose; spread (Drift) -> position spray.

`GfCloud` derives overlap = `round(onset(density) * duration)` clamped to `[2, kMaxGrains]` (min 2 avoids a single-grain tremolo), sets the grain-clock period to the duration, and normalizes the mixdown gain by the active grain count.

## Build

`ENGINE=graincloud` -> granular source wildcard + grainflow include, `-DSPK_ENGINE_GRANULAR -DSPK_GRAIN_GF -DM_PI=...`, **`-Os`** (granular + GrainflowLib templates overflow the 186 KB execution SRAM at `-O2`; fits at ~97% at `-Os`). Wired in `Makefile`, `CMakeLists.txt`, `Makefile.cmake`. The GrainflowLib DSP is host-tested standalone by `host/test_graincloud_kernel.cpp`; plain granular's host test (`test-granular`) covers the inherited plumbing.

## Risks / watch-items

- **On-device scattered-read cost unconfirmed** (no hardware profile yet). If high-density patches glitch, lower `kMaxGrains` in `gf_cloud.h` or run `METER=1`.
- **kMaxGrains=8/deck** is conservative for CPU; the per-sample `exp2f` in GrainflowLib's `increment` (fm is always 0 here) is the next optimization if more grains are wanted.
- **SDRAM is not zero-initialized**; the granular `Buffer` is memset by granular's own init, and GfCloud's scratch is regular zeroed BSS - so the uninitialized-memory class of bug does not apply here.
