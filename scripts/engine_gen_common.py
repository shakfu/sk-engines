"""Shared helpers for the sk-engines code generators (gen~ and Faust).

The build-wiring mechanism: each generator owns marker-delimited blocks in the Makefile and
engine_select.h, keyed by `<tag>:<name>` (tag = "gen" or "faust"), so a re-run replaces its block in
place and never clobbers another engine's. Lifted from scripts/gen_engine.py so both generators share one
copy. (gen_engine.py predates this module and still carries its own equivalents; it can adopt these later.)
"""

from __future__ import annotations

import re
from pathlib import Path


# Platform control name (the friendly knob labels used in manifests + the control-diagram specs) ->
# ParamId. The first six are the plain panel knobs delivered via set_param(); Cycle/MODFREQ reaches an
# engine via set_mod_speed() (the Faust runtime routes ModSpeed there).
KNOB_PARAM = {
    "pitch": "Speed", "speed": "Speed",
    "position": "Pos", "pos": "Pos",
    "size": "Size",
    "envelope": "Env", "env": "Env",
    "mix (sos)": "Mix", "mix": "Mix", "sos": "Mix",
    "cycle": "ModSpeed", "modfreq": "ModSpeed", "modspeed": "ModSpeed",
    "glow": "ModAmp", "mod_amt": "ModAmp", "modamt": "ModAmp", "modamp": "ModAmp",
}

# Every ParamId enum name (engine/engine_params.h, minus the Count sentinel). A manifest may bind a
# modifier-layer param that has no friendly knob label (e.g. Feedback on SOS+Alt, EnvSize on ENV+chord)
# by writing its ParamId name directly as the knob key.
PARAM_IDS = (
    "Pos", "FluxFb", "Env", "EnvSize", "Size", "Win", "PolySlice", "Speed",
    "FluxIntensity", "GritIntensity", "FluxMix", "GritMix", "Feedback", "Mix",
    "ModSpeed", "ModAmp", "Tempo", "ClickMix", "PanSpeed", "PanRange",
    "KeyInterval", "Crossfade", "AltPos", "Aux",
)


def knob_to_paramid(key: str) -> str:
    """Resolve a manifest knob key -> ParamId name. Accepts a friendly control label (Pitch, Position,
    Size, Envelope, Mix (SOS), Cycle, Glow) or a raw ParamId name (Feedback, EnvSize, Aux, ...)."""
    k = key.strip()
    if k.lower() in KNOB_PARAM:
        return KNOB_PARAM[k.lower()]
    for pid in PARAM_IDS:
        if pid.lower() == k.lower():
            return pid
    raise SystemExit(
        f"unknown control/param {key!r} - use a knob label (Pitch, Position, Size, Envelope, "
        f"Mix (SOS), Cycle, Glow) or a ParamId name (e.g. Feedback, EnvSize, Aux)")


def class_name(name: str) -> str:
    return "".join(p.capitalize() for p in re.split(r"[_\-]", name)) + "Engine"


def macro(name: str) -> str:
    return "SPK_ENGINE_" + re.sub(r"[^0-9a-zA-Z]", "_", name).upper()


def block_re(tag: str, name: str) -> re.Pattern:
    start = f">>> {tag}:{name} >>>"
    end = f"<<< {tag}:{name} <<<"
    return re.compile(
        r"[ \t]*(?://|#) " + re.escape(start) + r".*?(?://|#) " + re.escape(end) + r"\n",
        re.DOTALL,
    )


def upsert(text: str, tag: str, name: str, block: str, before: str) -> str:
    """Insert/replace a `<tag>:<name>` marker block immediately before the first `before` occurrence."""
    pat = block_re(tag, name)
    block = block if block.endswith("\n") else block + "\n"
    if pat.search(text):
        return pat.sub(block, text)
    idx = text.index(before)
    return text[:idx] + block + text[idx:]


def wire_engine_select(root: Path, tag: str, name: str, include: str, alias: str) -> None:
    hdr = root / "src" / "engine" / "engine_select.h"
    text = hdr.read_text()
    block = (
        f"// >>> {tag}:{name} >>>\n"
        f"#elif defined({macro(name)})\n"
        f'  #include "{include}"\n'
        f"  namespace spotykach {{ using ActiveEngine = {alias}; }}\n"
        f"// <<< {tag}:{name} <<<\n"
    )
    if "#else" not in text:
        raise SystemExit("engine_select.h: could not find the #else sentinel")
    hdr.write_text(upsert(text, tag, name, block, "#else"))


def unwire(root: Path, tag: str, name: str, files=("Makefile", "src/engine/engine_select.h",
                                                   "CMakeLists.txt")) -> None:
    for rel in files:
        p = root / rel
        if p.exists():
            p.write_text(block_re(tag, name).sub("", p.read_text()))
