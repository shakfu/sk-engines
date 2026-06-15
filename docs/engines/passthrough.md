# Passthrough — minimal reference engine

`ENGINE=passthrough` · `src/engine/passthrough/passthrough_engine.h` (header-only) · class `PassthroughEngine`

The smallest possible engine: stereo in → stereo out, tracking the last-block peak to drive a level meter on the rings. Its purpose is documentation, not music — it shows how little a non-granular engine needs, and is the template to copy when starting a new engine. It is the minimal `IEngine` reference (a code example), so it is developer-facing by nature and is not split into a separate impl file.

## What it does

- `process(in, out, size)` — copies input to output, updating a decayed peak per channel.

- `capabilities()` = `CapOwnDisplay` — it fills its own `DisplayModel` (`render()` draws a level meter on both rings and lights the play indicators); silence collapses the meter.

- Everything else stays at the `IEngine` no-op defaults: no params, no MIDI, no pads, no CV, no storage, no transport use.

## Why it exists

- It proves the `IEngine` contract holds for a non-looper with zero granular concepts.

- It is header-only and depends only on the contract + `nocopy.h` + `<cmath>` — the floor for engine size and coupling.

- Copy it (into `src/engine/<name>/`) to start a new engine, then override the methods you need and add an `engine_select.h` + `Makefile` branch (see [README](README.md#building-and-flashing-a-variant) and `docs/engine-layout.md`).

## Files

`src/engine/passthrough/passthrough_engine.h`; build via `engine_select.h` (`SPK_ENGINE_PASSTHROUGH`)

- `Makefile` (`ENGINE=passthrough`, `make engine-passthrough`). No `.cpp`.
