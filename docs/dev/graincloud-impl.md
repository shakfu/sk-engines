# graincloud - implementation notes

Developer notes for the `graincloud` engine (user-facing doc: [`docs/engines/graincloud.md`](../engines/graincloud.md)). It is a polyphonic grain cloud (idea-doc #11) built on a de-STL'd port of **GrainflowLib**.

## Architecture

PIMPL, exactly like `reso`: `GraincloudEngine` (header, no GrainflowLib types) forwards to `Impl`, which is placement-new'd into the injected SDRAM arena at `init()`. All GrainflowLib template types are confined to `graincloud_engine.cpp` so the composition root (`engine_select.h` -> `app.cpp`) never sees them.

Per deck (`Impl::Deck`, two of them):
- a `RecBuffer` - a self-contained float-stereo record buffer in the arena (~20 s) with a cubic read;
- a `gf_grain_collection` (placement-new'd in the arena) owning a fixed array of `kMaxGrains` (16) `gf_grain`s, also arena-allocated and placement-constructed;
- per-stream grain-clock / traversal phasor state, knob caches, and a per-grain equal-power pan table.

A single `IoScratch` (the GrainflowLib `gf_io_config` plus its input/output sample arrays) is shared by both decks since they are processed sequentially.

## The GrainflowLib port (`thirdparty/grainflow/`)

GrainflowLib is the *inner per-grain kernel of a Max/PD patch*, not a stereo cloud: `gf_grain::process()` consumes audio-rate `grain_clock`, `traversal_phasor`, `fm`, `am` signals and writes **eight separate per-grain output arrays**; it never sums or pans. The port therefore vendors only the kernel and rebuilds the surrounding patch glue in the engine.

**Dropped** (replaced in-engine): `gfGenericBufferReader.h` (AudioFile/`unique_ptr`/`vector`), `gfRecord.h` (vector recording), `gfPanner.h` (vector panner), `gfFilters.h`.

**De-STL edits** (all marked `// sk:` in the vendored headers):
- `gfGrainCollection.h` - `std::unique_ptr<gf_grain[]>` -> raw pointer into caller storage via `set_storage()`; `resize()` no longer `new[]`s (it clamps to the storage capacity and re-inits in place); destructor frees nothing; all `grains_.get()[i]` -> `grains_[i]`.
- `gfGrain.h` - the `std::unique_ptr<phasor>` vibrato member -> an inline `phasor` value (the phasor is a flat struct, so the heap allocation was gratuitous); `make_unique` removed; the destructor's six `delete`s on **non-owned** buffer pointers removed (a latent double-free upstream); the `throw("invalid type")` in `param_set` -> a no-op (`-fno-exceptions`).
- `gfParam.h` - unused `<map>` dropped; the two string reflection helpers marked `static inline` (unused; elided).

Result: no heap, no exceptions, no RTTI on the audio path. It compiles under the firmware's `-fno-exceptions -fno-rtti -std=c++17` and fits SRAM_EXEC at ~87% (no `-Os` needed).

## The buffer-reader seam

`gf_i_buffer_reader<RecBuffer, float>` is filled with free-function callbacks (`cb_*` in the .cpp). `T` is `RecBuffer`, so each grain's `buffer_ref_` points at the deck's record buffer. `sample_buffer` does the scattered cubic read; `sample_envelope` is the built-in Hann; `update_buffer_info` reports the recorded length (returns false when empty -> grains stay silent); the param/rate/window/delay buffer callbacks return false so grains use their own base/random/offset values.

## The patch glue (per block, per deck)

1. If recording, write the live input into `RecBuffer`.
2. Generate ONE grain-clock phasor whose **period is the grain duration** (`f_clock = 1/duration`), and a constant traversal value = the POS knob (the cloud centre; spray comes from the `delay` random param). `fm`/`am` are zero (pitch is the `transpose` param, not audio-rate fm). `auto_overlap` evenly phase-staggers the active grains off this one clock.
3. `collection.process(io)`.
4. Sum the per-grain mono outputs through the per-grain equal-power pan into stereo, scale by `1.4/sqrt(kMaxGrains)`.
5. The engine then applies per-deck dry/wet and the A/B crossfade, and soft-clips.

## Knob -> GrainflowLib param mapping

`apply_params()` pushes the knob caches into the gf param model (target 0 = all grains):
- PITCH -> `transpose.base` (+/-24 semis) + V/Oct; MOD_AMT -> `transpose.random` (pitch spray) and the engine-side pan spread.
- SIZE -> `delay.random` (ms; scatters each grain's birth position back from the playhead).
- ENV -> grain **duration** (8 ms .. 1.5 s) -> grain-clock period (`f_clock = 1/duration`); `space = 0`.
- MODFREQ -> **density** = onset rate (1 .. 80 grains/sec).
- Aux -> `direction.base` / `glisson.base` (fwd / reverse / random / glisson).

**Decoupling duration from density.** In GrainflowLib a grain's lifetime is its grain-clock period (`duration = (1-space)/f_clock`), so duration and density are naturally coupled. We decouple them: the grain-clock period IS the duration (ENV), and the onset rate (MODFREQ) is achieved by deriving the **overlap** = number of simultaneously-live grains, `active = round(onset_hz * duration)`, applied via `set_active_grains` (`update_overlap()`, called whenever ENV or density changes). With one shared clock + `auto_overlap` staggering the active grains by `1/active`, onsets land every `duration/active` s, so onset rate = `active/duration = onset_hz` exactly - until `active` hits `kMaxGrains`, beyond which the onset rate saturates (the overlap cap). The mixdown gain is normalized by `active` so overlap doesn't change loudness. Pans are per-grain *random* (not index-ordered) so a small active set still spreads across the field.

## File map

```
src/engine/graincloud/
  graincloud_engine.h          # IEngine, PIMPL handle
  graincloud_engine.cpp        # RecBuffer, buffer-reader callbacks, Cloud glue, Impl, forwarders
  thirdparty/grainflow/        # vendored, de-STL'd GrainflowLib (headers only)
host/
  test_graincloud.cpp          # engine test (IEngine surface: params, record, storage, clear, MIDI)
  test_graincloud_kernel.cpp   # kernel smoke test (de-STL kernel + seam + phasor glue, finite output)
  bench_graincloud.cpp         # the #11 scattered-read feasibility benchmark
```

## The SDRAM scattered-read benchmark (the #11 gate)

`make -C host bench-graincloud`. N grains each do 96 cubic reads/block at independent random positions over a 16 MB buffer; cost scales linearly (~0.79 us/grain/block cubic on the host). At 16 grains/deck (32 total) the projected device CPU is comfortable. **Caveat:** a desktop cannot reproduce the H7 external-SDRAM random-access penalty - the host number is a CPU lower bound and a *scaling* signal only. Confirm on-device with `METER=1` before raising `kMaxGrains`.

## Resolved bugs

- **Instant full-scale noise on hardware (host was fine).** The SDRAM arena (`DSY_SDRAM_BSS`) is **not** zero-initialized by startup; the host arena is. GrainflowLib skips `sample_buffer` (so never writes `grain_output`) when the source buffer is empty/absent - true for every grain at boot - so the mixdown summed uninitialized SDRAM garbage. Fix: `memset` the `IoScratch` to 0 in `setup()` (like granular/shuttle memset their arena allocations), plus a non-finite guard on the final output (NaN/inf -> 0) so no garbage can reach the codec. A poisoned-arena regression test (`test_graincloud.cpp` "poisoned-arena ... silence") fills the arena with a finite-float byte pattern before `init()` and asserts boot silence; it fails without the memset.

## Risks / watch-items

- **Any new arena allocation must be zeroed (or fully written before read).** SDRAM is garbage at boot. This bit the io scratch once; it will bite again.

- **On-device SDRAM cost is unconfirmed.** The dominant risk for any scatter-read cloud. If a high-density `METER=1` run shows the block overrunning, lower `kMaxGrains` or `kStreams`, or switch the reader to linear interpolation (~40% cheaper).
- **`SigType=float`** (not GrainflowLib's default `double`) halves the io scratch and is ample for audio.
- **Recording is self-contained**, not the granular `Buffer`: a deliberate choice to avoid dragging `daisysp.h`/`common.h` into a clean engine and coupling two engines (matches `shuttle`/`tape`/`radio`). The trade is no overdub/cut/skip-write-head machinery - irrelevant for a read-mostly cloud source.
- **Transport sync** is advertised (`CapTransport`) but density currently free-runs from the knob; locking density to a tempo division is a TODO.
