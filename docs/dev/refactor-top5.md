# Top 5 code-reducing refactoring opportunities

Ranked by realistic line reduction. Estimates are calibrated against direct reads of the actual code (the UI pot-queue switch, the per-vox loops, and the hardware init block), which corrected two over-stated proposals from the initial survey.

## 1. Collapse the per-deck A/B duplication across the entire UI layer

**Files:** `src/ui/core.ui.cpp`, `src/ui/core.ui.pads.cpp`, `src/ui/core.ui.leds.cpp` **Realistic reduction: ~80-120 lines** (largest single cluster, needs care)

The UI layer is built almost entirely as mirrored A/B blocks plus scattered `ref == Deck::A ? X_A : X_B` ternaries:

- CV processing (`core.ui.cpp:271-297`) and gate in/out (`300-341`) - near-identical A/B pairs.

- Pad FX touch/release (`core.ui.pads.cpp` ~20-60 and ~123-145) - Flux/Grit x A/B copy-paste.

- Mode + Mod-type switch decoding (`core.ui.cpp:546-598`) - two ~9-line bitset blocks each.

- LED draw helpers (`core.ui.leds.cpp`) - repeated `A?_A:_B` LED-id selection.

Consolidation: a `Hardware::LedId led_for(Deck::Ref, LedType)` lookup plus small `for (auto ref : {Deck::A, Deck::B})` helpers for the CV/gate/pad/switch paths.

**Caveat:** the *pot-queue switch* (`362-522`) is **not** cleanly symmetric - deck A carries tempo/click/key-interval, deck B carries pan; `CTRL_SOS_A` has `_key_interval`/`tap_hold` logic that B lacks. So a "merge the whole switch, save 80 lines" approach is overstated. Only the genuinely parallel cases (SIZE, POS, ENV, PITCH-intensity, MOD) can be table-driven; the asymmetric ones stay. The bigger, safer wins are in pads/leds/CV/gate, not the pot switch.

## 2. Data-drive the hardware peripheral init

**File:** `src/hw/hardware.cpp:129-176` **Reduction: ~24 lines** - SAFE, high confidence, self-flagged

The 15 analog-control `.Init(seed.adc.GetMuxPtr(m,c), kProcessRate, false, false, kPotSmoothTime)` calls and 7 `InitBipolarCv(seed.adc.GetPtr(i), ...)` calls differ only by their mux/ADC indices. The code already admits it: *"this is verbose and clumsy - would normally do a loopable configuration mapping."* Replace with a `{id, mux, chan}` / `{id, adc}` table and a loop. Guard with `static_assert(table_size == CTRL_LAST)` so an omitted control cannot silently skip init.

**Validated:** the init-table is the only real win here. **Drop the gate-read sub-item** (`263-274`): the three methods are each `return !x.Read();` but are public and called by name, so a `GetInvertedGpioState` helper still needs three wrapper bodies - net ~0 lines. The earlier "~35 lines" overcounted by folding it in.

## 3. Replace the WS2812 nested switches with 2D lookup tables

**File:** `src/hw/ws2812.cpp:44-79` (DMA request) and `161-184` (timer/channel config) **Reduction: ~20 lines** - SAFE WITH CARE; downgraded from "cleanest"

Two nested `switch (timer) { switch (channel) }` ladders map (periph, channel) -> DMA request / TIM register.

**Validated - three cautions that lower this from the original ~35-40 line, low-risk claim:**

- `get_dma_req` (`44-79`) tables cleanly into `[2][4]` **only if it preserves two sentinels**: the `// Invalid` TIM4_CH4 -> `0` case (line 72) and `default: break -> 0` for unknown peripherals. The caller bails on `dmareq == 0` (line 187). The `default` arm implies `TimerPeripheral` has values beyond TIM_3/TIM_4, so raw-enum indexing would be out of bounds - the table needs bounds guards reproducing the `-> 0` path.

- The channel-config switch (`161-184`) is **only partly tableable**: `timccr_ = &htim->Instance->CCR1..4` is a pointer to a different live struct member per channel, not a static constant - `timch_`/`dma_id` table fine, but `timccr_` needs an offset or a small residual switch.

- This file is the patched `bleeptools` ws2812 driver the build depends on. Any change diverges from the fork patch and **mandates a hardware flash + visual LED retest**, so it is not "low risk."

## 4. Consolidate Fx mode-dispatch and Track parameter setters

**Files:** `src/core/fx.cpp` (~5 parallel Drive/Reduce switches) + `src/core/track.cpp:124-150` **Reduction: ~12 lines** - WEAKEST; Track half withdrawn after validation

Original idea: extract a `_grit_access(mode, op)` helper for the Fx switches and fold `track.cpp`'s `add_p1..p4` into `add_param(uint8_t idx, float v)`.

**Validated - both halves are weaker than they looked:**

- **Track `add_p1..p4` - withdraw.** `Event` (`event.h`) uses **named scalar fields** (`p1..p4`, `p1_on..p4_on`), and consumers read them by name (`generator.cpp:248-258`, `granular_engine.cpp:79,174`). `add_param(idx, v)` either needs an internal switch (saves ~0 lines) or requires converting `Event` to `p[4]`/`p_on[4]` arrays - which touches `event.h` + `generator.cpp` + `granular_engine.cpp`, a far wider blast radius than a leaf edit. Not worth it unless array-izing `Event` is a separate goal.

- **Fx grit dispatch - control-rate only.** `Drive` and `Reduce` are **distinct, non-polymorphic types** (no shared base), so the helper must be a template-visitor or macro, not a base-pointer call. It can fold the four control-rate accessors (`set_grit_intensity`/`grit_intensity`/`set_grit_mix`/`grit_mix`) for ~12 lines, **but leave the `process()` switch (`137-140`) alone** - it is on the `-O2` audio path and the direct branch inlines cleanly. The flux side is already loop-based (no win).

## 5. De-duplicate SD path building and AudioData init

**Files:** `src/memory/storage.cpp:102-153`, `src/hw/card.cpp:22-34, 110-130` **Reduction: ~10-12 lines** - SAFE, narrower than quoted

**Validated:** `storage.cpp` **already has** `audio_file_name()` and `audio_file_path()` helpers (lines 25-41), so the layer is less duplicated than the survey implied. The clean, safe win is the **duplicated `AudioData` field init in `save()` vs `load()`** (lines 116-122 vs 143-149, identical `root_dir`/`deck_dir`/`tape_dir` + `audio_file_name` block): extract `fill_audio_data(ad, name)` - ~8 lines.

`card.cpp`'s two inline path builds (`28-34` read, `117-130` write) overlap **only partially** - the write path builds `tape_dir_path` as a separate step because it needs that intermediate for `f_mkdir`, so a shared join helps the read path only. Keep the hand-sized buffers (`[12]`, `[11]`, `[5]`) exact.

---

## Rejected as false positives

A `_for_each_vox` / `_for_each_window` helper for `generator.cpp` and `vox.cpp` was proposed but rejected. Those ~15 loops are already one-liners (`for (auto& v: _voxs) v.set_size(size);`). Wrapping them in a `std::function`-taking helper would *add* lines (and add an indirect call on the audio path), not remove them. Net negative - skip it.

## Summary

**Total realistic reduction across the 5: ~215-275 lines** (pre-validation survey figure).

**Post-validation (#2-#5 re-checked against the actual code):** the safe, mechanical total is smaller - roughly **~45-55 lines** across #2/#3/#5 plus the #4 Fx half, with #1 (~80-120 lines) deferred to the architecture work and the #4 Track half withdrawn. The survey's per-item estimates were optimistic; see each section's **Validated** note.

### Safety ranking (#2-#5, safest first)

1. **#2 init table (~24 lines)** - safest, real, SRAM-positive, no seam crossing. Best first move. (Gate-read sub-item dropped.)

2. **#5 AudioData-init dedup (~10-12 lines)** - safe, small, host-tested layer.

3. **#3 `get_dma_req` table (~20 lines)** - safe *with care*: preserve invalid/default sentinels + enum bounds; fork code, mandatory hardware retest.

4. **#4 (~12 lines)** - Track half withdrawn (needs an `Event` struct change); Fx half is a control-rate-only cleanup that must not touch the audio-path `process()` switch.

Only **#2 and #5** are unconditionally safe mechanical wins.

---

## Interference with the platform/engine architecture plan

Assessed against `docs/dev/refactor-status.md` and the `platform-engine-refactor` memory. That plan's success metric is **coupling reduction, not LOC**, under a hard **<1KB SRAM_EXEC ceiling**, with every round **behavior-preserving and hardware-only-verified**. This proposal optimizes for LOC reduction, so the two goals mostly coexist but pull apart on one item.

| Proposal | Interferes? | When |
|---|---|---|
| #1 UI A/B consolidation | **Yes - strongly** | Defer; fold into roadmap item 3 |
| #2 HW init tables | No | Safe now |
| #3 WS2812 tables | No (fork-patch caution only) | Safe now |
| #4 Fx/Track | No (mildly aligned w/ Phase 5) | Safe now; watch audio-path inlining |
| #5 Storage paths | No | Safe now |

### #1 UI A/B consolidation - HIGH interference, defer

Collides head-on with the roadmap:

- **It is the work the plan explicitly reserves for later.** refactor-status.md's "Why the refactor has not reduced code" section names exactly this: *"collapse the apply-pass switch, generalize the overlays into a table ... a blind rewrite of the most intricate, hardware-only-verified code."* Roadmap item 3 (2nd engine) owns it via the **`MValue` -> engine-agnostic toolkit keyed by `ParamId`** generalization, which rebuilds the apply-pass and per-indicator LED interpretation as engine-declared bindings. A hand-rolled A/B table now becomes a throwaway intermediate.

- **Already considered and rejected once.** Phase 3b left the apply-pass as faithful mechanical substitution because *"predicates intertwine mode/tap/is_empty/storage; table would be a riskier blind rewrite."*

- **Churns the files the NEXT round edits.** Roadmap item 1 (transport ownership, marked NEXT) edits `core.ui.cpp`/`pads`/`midi` to cut the `_core.driver()` hatch; a large reshuffle now creates rebase friction and lands in the most SRAM-sensitive TUs (all three already `-Os`-maxed).

Nuance: the `A?_A:_B` LED-id helper and the pads consolidation are mechanically less entangled than the pot apply-pass switch, but still sit in files slated for the DisplayModel/toolkit rewrite. The pot apply-pass should not be touched outside item 3.

### #2 Hardware init tables - no interference

`src/hw` is the stable HAL, below the platform/engine seam. No roadmap item rewrites this code (Phase 5 only changes include roots). SRAM-neutral-to-positive, which helps the standing SRAM watch-item.

### #3 WS2812 lookup tables - no architectural interference

`ws2812.cpp` is HAL/driver, untouched by every roadmap item. Only caveat is the existing one: it is the patched **bleeptools fork** driver, so preserve patch semantics and re-test on hardware.

### #4 Fx/Track - compatible, mildly aligned

`src/core` is **engine** code, behind the `IEngine` seam. Phase 5 splits the engine into `libfx`/`libseq`; tightening `fx.cpp`/`track.cpp` internals before extraction is consistent with that. Cautions: `fx.cpp` is on the `-O2` audio path - keep `process()` inlined and add no per-sample indirection (target the control-rate setters, not the sample loop); public signatures (`set_fx`, etc.) must not change. Bonus: host-testable (`make -C host test`), unlike the UI proposals.

### #5 Storage paths - no interference

`src/memory` + `card.cpp` is platform storage, decoupled in Phase 3c-vii to hold `GranularEngine*`. The path/AudioData dedup is internal to the storage/card layer, does not cross the seam, and Phase 5 leaves it platform. Host has a storage-port smoke test.

### Bottom line

4 of 5 are independent of the architecture work and even nudge the SRAM budget the right way. Only the highest-LOC item (#1) conflicts - precisely because it is the same intricate, hardware-only-verified UI logic the platform/engine plan has deliberately reserved for the 2nd-engine round.
