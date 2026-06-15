#!/usr/bin/env python3
"""Generate an sk-engines IEngine from a Faust .dsp + a JSON manifest.

The Faust analogue of scripts/gen_engine.py (gen~). For `src/engine/<name>/<name>.dsp` +
`<name>.json` it:

  1. builds the cyfaust kernel (`make faust-gen` -> `faust_kernel_<name>.h`, namespace
     `spotykach::fx_<name>`),
  2. parses the kernel's sliders (label + enclosing box) to validate the manifest's bindings,
  3. emits `<name>_engine.h`: a Traits struct (the ParamId -> slider Bind table + feature flags) bound to
     the shared `FaustEngine<Traits>` template in src/engine/faust/faust_fx.h,
  4. idempotently wires the Makefile ENGINE switch, engine_select.h, and CMakeLists.txt (marker-delimited
     `>>> faust:<name> >>>` blocks) and registers the kernel in the Makefile's FAUST_KERNELS list.

The DSP stays in the .dsp; the manifest only says which platform knob drives which Faust slider, plus a
few feature flags. The generated `<name>_engine.h` is preserved on re-run unless --force-glue.

See docs/dev/engine-gen.md for the design and docs/engines/README.md for the control-name vocabulary.

Manifest (docs/diagrams/controls/-style knob keys):
    {
      "engine": "chorus", "backend": "faust",
      "title": "...", "subtitle": "...",
      "knobs": { "Cycle": "rate", "Glow": "depth", "Size": "delay", "Mix (SOS)": "mix" },
      "capabilities": ["CapOwnDisplay"],
      "features": { "meter": true, "color": "0x33ccff", "soft_limit": false, "wet_dry": null }
    }
A knob value may also be an object {"label","box","slot","invert"} or a list of them (repeated labels
across boxes / one knob driving two sliders); "-" / null = unmapped.

Usage:
    scripts/gen_faust_engine.py <manifest.json> [--force-glue] [--remove] [--no-kernel]
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import engine_gen_common as common

# Platform control name (the control-diagram vocabulary + hardware aliases) -> ParamId. The first 6 reach
# the engine via set_param(); Cycle/MODFREQ reaches it via set_mod_speed() (FaustEngine routes ModSpeed).
_KNOB_PARAM = {
    "pitch": "Speed", "speed": "Speed",
    "position": "Pos", "pos": "Pos",
    "size": "Size",
    "envelope": "Env", "env": "Env",
    "mix (sos)": "Mix", "mix": "Mix", "sos": "Mix",
    "cycle": "ModSpeed", "modfreq": "ModSpeed", "modspeed": "ModSpeed",
    "glow": "ModAmp", "mod_amt": "ModAmp", "modamt": "ModAmp", "modamp": "ModAmp",
}


def _param_for(knob: str) -> str:
    pid = _KNOB_PARAM.get(knob.strip().lower())
    if not pid:
        raise SystemExit(f"unknown control name {knob!r} - use one of: "
                         "Pitch, Position, Size, Envelope, Mix (SOS), Cycle, Glow")
    return pid


def _bindings(knobs: dict):
    """Flatten the manifest 'knobs' into [(label, box|None, ParamId, slot, invert)] entries."""
    out = []
    for knob, val in knobs.items():
        if val in (None, "-", ""):
            continue
        pid = _param_for(knob)
        specs = val if isinstance(val, list) else [val]
        for s in specs:
            if isinstance(s, str):
                out.append((s, None, pid, 0, False))
            elif isinstance(s, dict):
                out.append((s["label"], s.get("box"), pid, int(s.get("slot", 0)), bool(s.get("invert", False))))
            else:
                raise SystemExit(f"bad binding for {knob!r}: {s!r}")
    return out


def _parse_kernel(header: Path):
    """Return (namespace, [(slider_label, enclosing_box|None)]) parsed from the generated kernel header."""
    text = header.read_text()
    m = re.search(r"namespace spotykach \{ namespace (\w+) \{", text)
    ns = m.group(1) if m else None
    sliders, box = [], None
    for line in text.splitlines():
        mb = re.search(r'open(?:Vertical|Horizontal|Tab)Box\("([^"]*)"\)', line)
        if mb:
            box = mb.group(1) or None
            continue
        ms = (re.search(r'add(?:Horizontal|Vertical)Slider\("([^"]+)"', line)
              or re.search(r'addNumEntry\("([^"]+)"', line))
        if ms:
            sliders.append((ms.group(1), box))
    return ns, sliders


# A kernel spec is "<dir>:<namespace-prefix>:<kernel-name>". Single/parallel engines use prefix "fx_" and
# the engine name as the kernel (namespace fx_<name>); a series engine's stages use an engine-scoped
# prefix "fx_<engine>_" so stage names (e.g. "filter") can't collide across engines.
def _kernel_spec(name: str, prefix: str, kname: str) -> str:
    return f"src/engine/{name}:{prefix}:{kname}"


def _build_kernel(root: Path, name: str, prefix: str, kname: str):
    spec = _kernel_spec(name, prefix, kname)
    print(f"  $ make faust-gen FAUST_KERNELS={spec}")
    subprocess.run(["make", "faust-gen", f"FAUST_KERNELS={spec}"], cwd=root, check=True)


def _ensure_faust_kernels(root: Path, name: str, prefix: str, kname: str):
    mk = root / "Makefile"
    text = mk.read_text()
    spec = _kernel_spec(name, prefix, kname)
    m = re.search(r"^FAUST_KERNELS \?= (.*)$", text, re.M)
    if not m:
        raise SystemExit("Makefile: no 'FAUST_KERNELS ?=' line to register the kernel")
    if spec in m.group(1):
        return
    mk.write_text(text[:m.start()] + f"FAUST_KERNELS ?= {m.group(1).rstrip()} {spec}" + text[m.end():])


def _caps_expr(manifest: dict, meter: bool, extra: list) -> str:
    caps = list(manifest.get("capabilities", []))
    for c in (["CapOwnDisplay"] if meter else []) + extra:
        if c not in caps:
            caps.append(c)
    return " | ".join(caps) if caps else "0"


def _bind_rows(binds) -> str:
    rows = []
    for (label, box, pid, slot, inv) in binds:
        boxc = f'"{box}"' if box else "nullptr"
        rows.append(f'            {{ {boxc}, "{label}", static_cast<int>(ParamId::{pid}), '
                    f'{slot}, {"true" if inv else "false"} }},  // -> {pid}')
    return "\n".join(rows) if rows else "            // (no bindings)"


def _glue_single(name: str, ns: str, binds, manifest: dict, decks: int) -> str:
    """Single control set (decks=1) or parallel DoubleMono (decks=2) on FaustEngine<Traits>."""
    cls = common.class_name(name)
    feats = manifest.get("features", {}) or {}
    meter = bool(feats.get("meter", False))
    soft_limit = bool(feats.get("soft_limit", False))
    color = feats.get("color", "0x33ccff")
    wet = feats.get("wet_dry")
    wet_role = f"static_cast<int>(ParamId::{_param_for(wet)})" if wet else "-1"
    caps_expr = _caps_expr(manifest, meter, ["CapDualDeck"] if decks == 2 else [])
    title = manifest.get("title", name)
    deck_note = ("    // decks=2: parallel DoubleMono - two instances of this mono kernel, deck A=left, "
                 "deck B=right.\n") if decks == 2 else ""
    return f"""// GENERATED from {name}.dsp + {name}.json by scripts/gen_faust_engine.py.
// Edit the manifest ({name}.json), not this file; then re-run the generator / `make engine-gen`.
// {title}
#pragma once

#include "engine/faust/faust_fx.h"
#include "engine/{name}/faust_kernel_{name}.h"

namespace spotykach {{

struct {cls}Traits {{
    using Kernel = {ns}::mydsp;

    // Platform knob -> Faust slider (keyed by ParamId; the wrapper captures each slider's range from the
    // kernel and linear-maps the 0..1 knob into it).
    static const faustgen::Bind* binds() {{
        static const faustgen::Bind b[] = {{
{_bind_rows(binds)}
        }};
        return b;
    }}
    static int nbinds() {{ return {len(binds)}; }}

{deck_note}    static constexpr Capabilities caps         = {caps_expr};
    static constexpr int          decks        = {decks};
    static constexpr int          wet_dry_role = {wet_role};
    static constexpr bool         soft_limit   = {"true" if soft_limit else "false"};
    static constexpr bool         meter        = {"true" if meter else "false"};
    static constexpr uint32_t     color        = {color};
}};

using {cls} = FaustEngine<{cls}Traits>;

}} // namespace spotykach
"""


def _glue_chain(name: str, stages, manifest: dict) -> str:
    """Series chain (deck_mode=series) on FaustChainEngine<Traits>. stages = [(kname, ns, binds), ...]."""
    cls = common.class_name(name)
    feats = manifest.get("features", {}) or {}
    meter = bool(feats.get("meter", False))
    soft_limit = bool(feats.get("soft_limit", False))
    color = feats.get("color", "0x33ccff")
    caps_expr = _caps_expr(manifest, meter, ["CapDualDeck"])
    title = manifest.get("title", name)
    (ka, nsa, ba), (kb, nsb, bb) = stages[0], stages[1]
    return f"""// GENERATED from {name}.json (deck_mode=series) by scripts/gen_faust_engine.py.
// Edit the manifest ({name}.json), not this file; then re-run the generator / `make engine-gen`.
// {title}
#pragma once

#include "engine/faust/faust_chain.h"
#include "engine/{name}/faust_kernel_{ka}.h"
#include "engine/{name}/faust_kernel_{kb}.h"

namespace spotykach {{

struct {cls}Traits {{
    using StageA = {nsa}::mydsp;   // deck A -> stage 1 ({ka})
    using StageB = {nsb}::mydsp;   // deck B -> stage 2 ({kb})

    static const faustgen::Bind* binds_a() {{
        static const faustgen::Bind b[] = {{
{_bind_rows(ba)}
        }};
        return b;
    }}
    static int nbinds_a() {{ return {len(ba)}; }}

    static const faustgen::Bind* binds_b() {{
        static const faustgen::Bind b[] = {{
{_bind_rows(bb)}
        }};
        return b;
    }}
    static int nbinds_b() {{ return {len(bb)}; }}

    static constexpr Capabilities caps       = {caps_expr};
    static constexpr bool         soft_limit = {"true" if soft_limit else "false"};
    static constexpr bool         meter      = {"true" if meter else "false"};
    static constexpr uint32_t     color      = {color};
}};

using {cls} = FaustChainEngine<{cls}Traits>;

}} // namespace spotykach
"""


def _wire_makefile(root: Path, name: str):
    mk = root / "Makefile"
    text = mk.read_text()
    block = (
        f"# >>> faust:{name} >>> (managed by scripts/gen_faust_engine.py)\n"
        f"else ifeq ($(ENGINE), {name})\n"
        f"C_DEFS += -D{common.macro(name)}\n"
        f"# Faust engine generated from {name}.dsp + {name}.json - header-only (the cyfaust kernel + the\n"
        f"# shared FaustEngine<Traits> wrapper), so there is no engine .cpp.\n"
        f"ENGINE_SOURCES =\n"
        f"# <<< faust:{name} <<<\n"
    )
    sentinel = "else\n$(error Unknown ENGINE"
    if sentinel not in text:
        raise SystemExit("Makefile: could not find the ENGINE-switch sentinel")
    mk.write_text(common.upsert(text, "faust", name, block, sentinel))


# Reverse of _KNOB_PARAM: ParamId -> the control-diagram knob key.
_PARAM_KNOB = {
    "Speed": "Pitch", "Pos": "Position", "Size": "Size", "Env": "Envelope",
    "Mix": "Mix (SOS)", "ModSpeed": "Cycle", "ModAmp": "Glow",
}


def _knob_map(binds) -> dict:
    m = {k: "-" for k in ("Pitch", "Position", "Size", "Envelope", "Mix (SOS)", "Cycle", "Glow")}
    for (label, _box, pid, _slot, _inv) in binds:
        m[_PARAM_KNOB[pid]] = label
    return m


def _emit_control_spec(root: Path, name: str, manifest: dict, binds, binds_b=None) -> None:
    """Convergence: emit the control-diagram spec (docs/diagrams/controls/<name>.json) from the same
    manifest, so `make diagrams` renders the engine's control surface. Emit-if-absent: an author who
    hand-tunes the spec owns it (the generator never overwrites). For a series engine, binds_b is stage
    B's bindings and lands as a deckB override so the diagram shows each deck driving its own stage."""
    out = root / "docs" / "diagrams" / "controls" / f"{name}.json"
    if out.exists():
        print(f"  keep   {out.relative_to(root)} (exists)")
        return
    meter = bool((manifest.get("features", {}) or {}).get("meter", False))
    spec = {
        "engine": name,
        "title": manifest.get("title", name),
        "subtitle": manifest.get("subtitle", ""),
        "knobs": _knob_map(binds),
        "pads": {k: "-" for k in ("Play", "Reverse", "Grit", "Flux", "Seq")},
        "ring": "output level meter" if meter else "-",
        "crossfade": "-",
        "switches": {"Mode (L/C/R)": "-", "Routing (L/C/R)": "-", "CV target (U/C/D)": "-", "Out trims A/B": "output trims"},
        "transport": {"Tap": "-", "Spot": "-"},
        "cv": {"Size/Pos A/B": "-", "Mix A/B": "-", "V/Oct A/B": "-", "Crossfade CV": "-"},
        "gates": {"Gate in A/B": "-", "Gate out A/B": "-"},
        "mod_midi": {"Mod CV out A/B": "-"},
    }
    if binds_b is not None:
        spec["deckB"] = {"knobs": _knob_map(binds_b)}
    out.write_text(json.dumps(spec, indent=2) + "\n")
    print(f"  write  {out.relative_to(root)} (control diagram spec)")


def _wire_cmake(root: Path, name: str):
    cm = root / "CMakeLists.txt"
    if not cm.exists():
        return
    text = cm.read_text()
    block = (
        f"# >>> faust:{name} >>>\n"
        f"elseif(ENGINE STREQUAL {name})\n"
        f"    set(ENGINE_DEFINE {common.macro(name)})\n"
        f"    set(ENGINE_SOURCES)\n"
        f"# <<< faust:{name} <<<\n"
    )
    sentinel = 'else()\n    message(FATAL_ERROR "Unknown ENGINE'
    if sentinel not in text:
        print("  WARNING: CMakeLists.txt sentinel not found; skipping CMake wiring")
        return
    cm.write_text(common.upsert(text, "faust", name, block, sentinel))


def _process_kernel(root: Path, name: str, prefix: str, kname: str, knobs: dict, no_kernel: bool):
    """Build (unless no_kernel), register, parse, and validate-bind one kernel. Returns (namespace, binds)."""
    eng_dir = root / "src" / "engine" / name
    if not (eng_dir / f"{kname}.dsp").is_file():
        raise SystemExit(f"missing src/engine/{name}/{kname}.dsp")
    if not no_kernel:
        _build_kernel(root, name, prefix, kname)
    _ensure_faust_kernels(root, name, prefix, kname)
    header = eng_dir / f"faust_kernel_{kname}.h"
    if not header.is_file():
        raise SystemExit(f"kernel not found: {header.relative_to(root)} (run without --no-kernel)")
    ns, sliders = _parse_kernel(header)
    if ns is None:
        raise SystemExit(f"could not find the kernel namespace in {header.name}")
    binds = _bindings(knobs)
    labels = {s for s, _ in sliders}
    for (label, _box, _pid, _slot, _inv) in binds:
        if label not in labels:
            raise SystemExit(f"binding label {label!r} is not a slider in {header.name}; "
                             f"available: {sorted(labels)}")
    print(f"  kernel spotykach::{ns} ({kname}.dsp): {len(sliders)} slider(s), {len(binds)} bound")
    return ns, binds


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate an sk-engines engine from a Faust .dsp + manifest.")
    ap.add_argument("manifest", type=Path, help="JSON manifest (engine name derived from it)")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--force-glue", action="store_true",
                    help="overwrite <name>_engine.h even if it exists (drops hand edits)")
    ap.add_argument("--no-kernel", action="store_true",
                    help="skip the cyfaust kernel rebuild (use the checked-in faust_kernel_<name>.h)")
    ap.add_argument("--remove", action="store_true",
                    help="unwire the engine from Makefile/engine_select.h/CMakeLists.txt")
    args = ap.parse_args()

    root = args.repo_root.resolve()
    manifest = json.loads(args.manifest.read_text())
    name = manifest.get("engine") or args.manifest.stem
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        raise SystemExit(f"invalid engine name {name!r}: lowercase [a-z0-9_], starting with a letter")

    deck_mode = (manifest.get("deck_mode") or "single").lower()

    if args.remove:
        common.unwire(root, "faust", name)
        # also drop the FAUST_KERNELS registration(s)
        mk = root / "Makefile"; text = mk.read_text()
        if deck_mode == "series":
            for st in manifest.get("stages", []):
                text = text.replace(f" src/engine/{name}:fx_{name}_:{st['dsp']}", "")
        else:
            text = text.replace(f" src/engine/{name}:fx_:{name}", "")
        mk.write_text(text)
        print(f"gen_faust_engine: unwired {name}")
        return 0

    eng_dir = root / "src" / "engine" / name
    print(f"gen_faust_engine: {args.manifest} -> ENGINE={name} (deck_mode={deck_mode})")
    glue = eng_dir / f"{name}_engine.h"
    keep_glue = glue.exists() and not args.force_glue

    if deck_mode == "series":
        stages_in = manifest.get("stages", [])
        if len(stages_in) != 2:
            raise SystemExit("deck_mode=series requires exactly 2 stages (deck A -> stage 1, deck B -> stage 2)")
        stages = []
        for st in stages_in:
            kname = st["dsp"]
            ns, binds = _process_kernel(root, name, f"fx_{name}_", kname, st.get("knobs", {}), args.no_kernel)
            stages.append((kname, ns, binds))
        if keep_glue:
            print(f"  keep   {glue.relative_to(root)} (exists; --force-glue to regenerate)")
        else:
            glue.write_text(_glue_chain(name, stages, manifest))
            print(f"  write  {glue.relative_to(root)}")
        _emit_control_spec(root, name, manifest, stages[0][2], stages[1][2])
    else:
        decks = 2 if deck_mode in ("parallel", "doublemono") else 1
        ns, binds = _process_kernel(root, name, "fx_", name, manifest.get("knobs", {}), args.no_kernel)
        if keep_glue:
            print(f"  keep   {glue.relative_to(root)} (exists; --force-glue to regenerate)")
        else:
            glue.write_text(_glue_single(name, ns, binds, manifest, decks))
            print(f"  write  {glue.relative_to(root)}")
        _emit_control_spec(root, name, manifest, binds)

    _wire_makefile(root, name)
    common.wire_engine_select(root, "faust", name,
                              f"engine/{name}/{name}_engine.h", common.class_name(name))
    _wire_cmake(root, name)
    print(f"  wired  Makefile + engine_select.h + CMakeLists.txt")
    print(f"  build: make ENGINE={name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
