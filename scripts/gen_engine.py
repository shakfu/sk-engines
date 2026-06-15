#!/usr/bin/env python3
"""Generate an sk-engines IEngine from a Max gen~ export via gen-dsp.

This is the gen~ analogue of `make faust-kernels`. For a gen~ export it:

  1. runs gen-dsp's Daisy backend into a temp dir,
  2. syncs the genlib-isolation bridge (the wrapper_* C interface, the copied
     gen/ export) into src/engine/<name>/, dropping gen-dsp's board main
     (gen_ext_daisy.cpp) and its private allocator (genlib_daisy.*) -- the
     sk-engines platform provides those, and genlib allocation is routed into
     the EngineContext SDRAM arena by src/engine/gen/genlib_arena.cpp,
  3. emits <name>_engine.h: a traits struct forwarding to the export's
     <name>_daisy namespace plus a ParamId -> gen-parameter map, bound to the
     generic GenEngine<W> in src/engine/gen/gen_engine.h,
  4. idempotently wires the Makefile ENGINE switch and engine_select.h (in
     marker-delimited blocks, safe to re-run).

The ParamId map (which platform knob drives which gen~ param) comes from one of
two sources, unified with the Faust generator:

  * --manifest <name>.json: a hand-authored manifest with a `knobs` map
    (control name -> gen~ param NAME), resolved + VALIDATED against the export's
    auto-generated manifest.json -- the same declarative method as Faust. The
    manifest's `export` names the gen~ export dir; --no-gen skips re-running
    gen-dsp and regenerates only the glue/wiring from the synced files.

  * positional <gen_export_dir> <name>: a positional default (gen param i ->
    _PRIMARY[i]) to BOOTSTRAP a new engine, then hand-tuned. The glue is NOT
    overwritten on re-run unless --force-glue, so a hand-tuned map survives.

One-time scaffolding this assumes already exists (created with the first gen
engine, not by this script): the shared family src/engine/gen/ and `$(GEN_INC)`
appended to C_INCLUDES in the Makefile.

Usage:
    python scripts/gen_engine.py --manifest <name>.json [--no-gen] [--force-glue]
    python scripts/gen_engine.py <gen_export_dir> <name> [--board seed] [--force-glue]
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import engine_gen_common as common

# Mechanical files copied from the gen-dsp output into the engine dir. The board
# main and the private allocator are intentionally excluded.
_KEEP_FILES = [
    "_ext_daisy.cpp",
    "_ext_daisy.h",
    "gen_ext_common_daisy.h",
    "gen_buffer.h",
    "daisy_buffer.h",
    "gen_remap_inputs.h",
    "manifest.json",
]
_KEEP_DIRS = ["gen"]

# Default ParamId assignment order: gen parameter i maps to PRIMARY[i]. This is
# arbitrary-but-sensible; retune index_of() in the generated header to taste.
# Only ParamIds the platform actually delivers to a single-deck engine via
# set_param() are listed, so every default-mapped param reaches a real control:
#   - first 6 are the plain panel knobs (no modifier): SIZE/POS/PITCH/ENV/SOS/MOD_AMT
#   - the rest are modifier layers (Flux/Grit pad, Alt, ENV-chord) needing no cap.
# Deliberately omitted: ModSpeed (MODFREQ routes to set_mod_speed(), not
# set_param()), Aux/AltPos (need CapAux/CapAltPos), Win/PolySlice (granular slice
# modes), and the global transport params. See docs/engine-types/gen.md.
_PRIMARY = [
    "Size", "Pos", "Speed", "Env", "Mix", "ModAmp",
    "Feedback", "FluxFb", "FluxIntensity", "GritIntensity", "FluxMix", "GritMix",
    "EnvSize",
]


def _run_gen_dsp(export: Path, name: str, board: str, out: Path) -> None:
    cmd = [
        sys.executable, "-m", "gen_dsp", str(export),
        "-p", "daisy", "-n", name, "--board", board, "--no-build",
        "-o", str(out),
    ]
    print("  $ " + " ".join(cmd))
    subprocess.run(cmd, check=True)


def _class_name(name: str) -> str:
    return "".join(p.capitalize() for p in re.split(r"[_\-]", name)) + "Engine"


def _macro(name: str) -> str:
    return "SPK_ENGINE_" + re.sub(r"[^0-9a-zA-Z]", "_", name).upper()


def _sync_mechanical(src: Path, dst: Path) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    for f in _KEEP_FILES:
        s = src / f
        if s.is_file():
            shutil.copy2(s, dst / f)
    for d in _KEEP_DIRS:
        s = src / d
        if s.is_dir():
            t = dst / d
            if t.exists():
                shutil.rmtree(t)
            shutil.copytree(s, t)


def _cases_positional(params: list) -> tuple[str, str]:
    """The arbitrary-but-sensible default: gen param i -> _PRIMARY[i]. Used to bootstrap a new engine
    (no hand-authored manifest yet); the generated index_of() is then hand-tuned."""
    cases, map_lines = [], []
    for p in params:
        i = p["index"]
        rng = f'[{p["min"]:g}..{p["max"]:g}]'
        if i < len(_PRIMARY):
            pid = _PRIMARY[i]
            cases.append(f'            case ParamId::{pid}:'.ljust(40) + f'return {i};  // gen "{p["name"]}" {rng}')
            map_lines.append(f'//   {pid:<10} -> {i} {p["name"]} {rng}')
        else:
            map_lines.append(f'//   (unmapped)  -> {i} {p["name"]} {rng}  -- add a ParamId case below to reach it')
    return ("\n".join(cases) if cases else "            // (no parameters)",
            "\n".join(map_lines) if map_lines else "//   (no parameters)")


def _cases_from_knobs(params: list, knobs: dict) -> tuple[str, str]:
    """Build index_of() from a hand-authored manifest's `knobs` map (control name -> gen~ param NAME),
    the gen~ analogue of the Faust manifest. Validates each param name against the export's manifest."""
    by_name = {p["name"]: p for p in params}
    cases, map_lines = [], []
    for control_key, param_name in knobs.items():
        if param_name in (None, "-", ""):
            continue
        pid = common.knob_to_paramid(control_key)
        p = by_name.get(param_name)
        if p is None:
            raise SystemExit(f"manifest binds {control_key!r} -> {param_name!r}, which is not a gen~ "
                             f"parameter of this export; available: {sorted(by_name)}")
        i = p["index"]
        rng = f'[{p["min"]:g}..{p["max"]:g}]'
        cases.append(f'            case ParamId::{pid}:'.ljust(40) + f'return {i};  // gen "{param_name}" {rng}')
        map_lines.append(f'//   {pid:<10} -> {i} {param_name} {rng}')
    return ("\n".join(cases) if cases else "            // (no bindings)",
            "\n".join(map_lines) if map_lines else "//   (no bindings)")


def _glue_source(name: str, cases_block: str, map_block: str, from_manifest: bool) -> str:
    ns = f"{name}_daisy"
    cls = _class_name(name)
    if from_manifest:
        provenance = (f"// GENERATED by scripts/gen_engine.py from {name}.json. The ParamId map below comes\n"
                      f"// from the manifest's `knobs` (control name -> gen~ param); edit the manifest, not this\n"
                      f"// file, then re-run `make gen-engine MANIFEST=...`. Preserved on re-run unless --force-glue.")
    else:
        provenance = ("// GENERATED by scripts/gen_engine.py. The mechanical wrapper files in this\n"
                      "// directory are regenerated wholesale; THIS file is generated once and then\n"
                      "// hand-tuned -- re-running gen_engine.py preserves it unless --force-glue.\n"
                      f"// The ParamId map below (normalized 0..1 -> [min,max]) is a positional default;\n"
                      "// reassign cases in index_of() to taste (or supply a manifest):")

    return f"""// {name}_engine.h - gen~ "{name}" bound to the generic GenEngine.
//
{provenance}
//
// The export's wrapper_* C interface lives in namespace `{ns}` (from -DDAISY_EXT_NAME={name}):
{map_block}

#pragma once

#include "engine/iengine.h"
#include "_ext_daisy.h"            // namespace {ns} {{ wrapper_* }}; GenState == void
#include "engine/gen/gen_engine.h"

namespace spotykach {{

struct {cls}Wrap {{
    static void* create(float sr, long block) {{
        return {ns}::wrapper_create(sr, block);
    }}
    static void perform(void* st, float** in, long nin, float** out, long nout, long n) {{
        {ns}::wrapper_perform(st, in, nin, out, nout, n);
    }}
    static int num_inputs()  {{ return {ns}::wrapper_num_inputs(); }}
    static int num_outputs() {{ return {ns}::wrapper_num_outputs(); }}

    // ParamId -> gen parameter index (-1 = unmapped).
    static int index_of(ParamId id) {{
        switch (id) {{
{cases_block}
            default: return -1;
        }}
    }}

    static void set_param(void* st, ParamId id, DeckRef::Ref deck, float v01) {{
        if (deck == DeckRef::B) return;  // single stereo effect: ignore deck B
        const int i = index_of(id);
        if (i < 0) return;
        const float lo = {ns}::wrapper_param_min(st, i);
        const float hi = {ns}::wrapper_param_max(st, i);
        {ns}::wrapper_set_param(st, i, lo + v01 * (hi - lo));
    }}

    static float get_param(void* st, ParamId id, DeckRef::Ref /*deck*/) {{
        const int i = index_of(id);
        if (i < 0) return 0.f;
        const float lo  = {ns}::wrapper_param_min(st, i);
        const float hi  = {ns}::wrapper_param_max(st, i);
        const float val = {ns}::wrapper_get_param(st, i);
        return (hi > lo) ? (val - lo) / (hi - lo) : 0.f;
    }}
}};

using {cls} = GenEngine<{cls}Wrap>;

}} // namespace spotykach
"""


def _block_re(name: str) -> re.Pattern:
    start = f">>> gen:{name} >>>"
    end = f"<<< gen:{name} <<<"
    return re.compile(
        r"[ \t]*(?://|#) " + re.escape(start) + r".*?(?://|#) " + re.escape(end) + r"\n",
        re.DOTALL,
    )


def _upsert(text: str, name: str, block: str, before: str) -> str:
    """Insert/replace a marker-delimited block immediately before `before`."""
    pat = _block_re(name)
    block = block if block.endswith("\n") else block + "\n"
    if pat.search(text):
        return pat.sub(block, text)
    idx = text.index(before)
    return text[:idx] + block + text[idx:]


def _unwire(root: Path, name: str) -> None:
    for rel in ("Makefile", "src/engine/engine_select.h"):
        p = root / rel
        new = _block_re(name).sub("", p.read_text())
        p.write_text(new)
    eng = root / "src" / "engine" / name
    if eng.is_dir():
        shutil.rmtree(eng)
        print(f"  removed {eng.relative_to(root)}/ and unwired Makefile + engine_select.h")
    else:
        print(f"  unwired Makefile + engine_select.h (no {eng.relative_to(root)}/ to remove)")


def _wire_makefile(root: Path, name: str, manifest: dict) -> None:
    mk = root / "Makefile"
    text = mk.read_text()
    engine = name
    gen_name = manifest["gen_name"]
    block = (
        f"# >>> gen:{name} >>> (managed by scripts/gen_engine.py)\n"
        f"else ifeq ($(ENGINE), {engine})\n"
        f"C_DEFS += -D{_macro(name)}\n"
        f"C_DEFS += -DGENLIB_NO_JSON\n"
        f"C_DEFS += -DDAISY_EXT_NAME={name}\n"
        f"C_DEFS += -DGEN_EXPORTED_NAME={gen_name}\n"
        f'C_DEFS += -DGEN_EXPORTED_HEADER=\\"{gen_name}.h\\"\n'
        f'C_DEFS += -DGEN_EXPORTED_CPP=\\"{gen_name}.cpp\\"\n'
        f"C_DEFS += -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter\n"
        f"GEN_DIR = src/engine/{engine}\n"
        f"GEN_INC = -I$(GEN_DIR) -I$(GEN_DIR)/gen -I$(GEN_DIR)/gen/gen_dsp\n"
        f"ENGINE_SOURCES = $(GEN_DIR)/_ext_daisy.cpp src/engine/gen/genlib_arena.cpp\n"
        f"# <<< gen:{name} <<<\n"
    )
    sentinel = "else\n$(error Unknown ENGINE"
    if sentinel not in text:
        raise SystemExit("Makefile: could not find the ENGINE-switch sentinel to anchor insertion")
    if "$(GEN_INC)" not in text:
        print("  WARNING: Makefile C_INCLUDES is missing $(GEN_INC); add it once: "
              "C_INCLUDES = -Isrc/ -Ilib/ $(RESO_INC) $(GEN_INC)")
    mk.write_text(_upsert(text, name, block, sentinel))


def _wire_engine_select(root: Path, name: str) -> None:
    hdr = root / "src" / "engine" / "engine_select.h"
    text = hdr.read_text()
    cls = _class_name(name)
    block = (
        f"// >>> gen:{name} >>>\n"
        f"#elif defined({_macro(name)})\n"
        f'  #include "engine/{name}/{name}_engine.h"\n'
        f"  namespace spotykach {{ using ActiveEngine = {cls}; }}\n"
        f"// <<< gen:{name} <<<\n"
    )
    sentinel = "#else"
    if sentinel not in text:
        raise SystemExit("engine_select.h: could not find #else sentinel")
    hdr.write_text(_upsert(text, name, block, sentinel))


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate an sk-engines engine from a gen~ export.")
    ap.add_argument("export", type=Path, nargs="?",
                    help="gen~ export directory (positional/bootstrap mode); with --manifest it is taken from the manifest")
    ap.add_argument("name", nargs="?", help="engine name (positional/bootstrap mode); with --manifest it comes from the manifest")
    ap.add_argument("--manifest", type=Path,
                    help="hand-authored <name>.json (backend:gen) - the unified method: emits index_of() from its `knobs` map")
    ap.add_argument("--no-gen", action="store_true",
                    help="skip running gen-dsp; reuse the already-synced gen/ + manifest.json (regenerate glue/wiring only)")
    ap.add_argument("--board", default="seed", help="gen-dsp Daisy board variant (default: seed)")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent,
                    help="sk-engines repo root (default: parent of scripts/)")
    ap.add_argument("--force-glue", action="store_true",
                    help="overwrite <name>_engine.h even if it exists (drops a hand-tuned ParamId map)")
    ap.add_argument("--remove", action="store_true",
                    help="delete src/engine/<name>/ and unwire it from the Makefile + engine_select.h")
    args = ap.parse_args()
    root = args.repo_root.resolve()

    # The hand-authored manifest (unified method) supplies the engine name, the export dir, and the knob map.
    man = json.loads(args.manifest.read_text()) if args.manifest else None
    name = (man.get("engine") if man else None) or args.name or (args.manifest.stem if args.manifest else None)
    if not name:
        raise SystemExit("need an engine name: a positional <name>, or --manifest <name>.json")
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        raise SystemExit(f"invalid name {name!r}: use lowercase [a-z0-9_], starting with a letter")

    if args.remove:
        print(f"gen_engine: removing {name}")
        _unwire(root, name)
        return 0

    engine_dir = root / "src" / "engine" / name

    # Resolve the gen~ export dir: the manifest's `export`, else the positional argument.
    export = None
    if man and man.get("export"):
        e = Path(man["export"])
        export = e if e.is_absolute() else (root / e)
    elif args.export:
        export = args.export.resolve()

    # Get the gen-dsp manifest (auto-generated param descriptions). Either re-run gen-dsp + sync, or
    # (with --no-gen) reuse the copy already synced into the engine dir.
    if args.no_gen:
        mf = engine_dir / "manifest.json"
        if not mf.is_file():
            raise SystemExit(f"--no-gen needs an already-synced {mf.relative_to(root)} (run once without --no-gen first)")
        auto = json.loads(mf.read_text())
        print(f"gen_engine: {name} (--no-gen; reusing {mf.relative_to(root)})")
    else:
        if export is None:
            raise SystemExit("need a gen~ export: a positional <export> dir, or `export` in the manifest")
        if not export.is_dir():
            raise SystemExit(f"export not found: {export}")
        print(f"gen_engine: {export} -> {engine_dir} (ENGINE={name})")
        with tempfile.TemporaryDirectory() as tmp:
            tmp_out = Path(tmp) / name
            _run_gen_dsp(export, name, (man or {}).get("board", args.board), tmp_out)
            auto = json.loads((tmp_out / "manifest.json").read_text())
            _sync_mechanical(tmp_out, engine_dir)

    params = auto.get("params", [])
    if man:
        cases, map_block = _cases_from_knobs(params, man.get("knobs", {}))
    else:
        cases, map_block = _cases_positional(params)

    glue = engine_dir / f"{name}_engine.h"
    if glue.exists() and not args.force_glue:
        print(f"  keep   {glue.relative_to(root)} (exists; --force-glue to regenerate)")
    else:
        glue.write_text(_glue_source(name, cases, map_block, from_manifest=bool(man)))
        print(f"  write  {glue.relative_to(root)}")

    _wire_makefile(root, name, auto)
    _wire_engine_select(root, name)

    print(f"  wired  Makefile + engine_select.h  ({auto['num_inputs']}in/"
          f"{auto['num_outputs']}out, {len(params)} params)")
    print(f"  build: make ENGINE={name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
