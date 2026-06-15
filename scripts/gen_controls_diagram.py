#!/usr/bin/env python3
"""Generate an engine-specific control-surface diagram from the shared d2 template.

The hardware is the FIXED platform; every engine reuses the same physical controls.
`docs/diagrams/controls-template.d2` lays out that surface with a `[ ... ]` placeholder in
each control's value cell. This script fills those placeholders from a small JSON spec (the
per-engine control map) and writes `<engine>-controls.d2`, which `make diagrams` renders to
`docs/media/<engine>-controls.svg` (or pass `--render pdf`) for embedding in the engine doc.

The template stays the single source of truth for the layout: change the template and every
generated diagram follows. The JSON only supplies what each control DOES in a given engine.

JSON spec format (see docs/diagrams/controls/radio.json for a worked example):

    {
      "engine": "radio",
      "title":    "Radio - dual virtual RadioMusic",   # optional (header line)
      "subtitle": "free-running virtual playhead",       # optional (italic subhead)
      "knobs":  { "Pitch": "...", "Position": "...", "Size": "...", "Envelope": "...",
                  "Mix (SOS)": "...", "Cycle": "...", "Glow": "..." },
      "pads":   { "Play": "...", "Reverse": "...", "Grit": "...", "Flux": "...", "Seq": "..." },
      "ring":   "what the LED ring shows",
      "crossfade": "A/B blend",
      "switches":  { "Mode (L/C/R)": "...", "Routing (L/C/R)": "...",
                     "CV target (U/C/D)": "...", "Out trims A/B": "..." },
      "transport": { "Tap": "...", "Spot": "..." },
      "cv":     { "Size/Pos A/B": "...", "Mix A/B": "...", "V/Oct A/B": "...", "Crossfade CV": "..." },
      "gates":  { "Gate in A/B": "...", "Gate out A/B": "..." },
      "mod_midi": { "Mod CV out A/B": "..." },
      "deckB": { "knobs": {...}, "pads": {...}, "ring": "..." }   # optional asymmetric overrides
    }

Keys must match the control names in the template (a "Mix (SOS)" cell needs a "Mix (SOS)" key).
Any control left unspecified keeps the template's `[ ... ]` placeholder and is reported, so a
half-filled spec is visible; use "-" for a control the engine does not use.

Examples:
    scripts/gen_controls_diagram.py docs/diagrams/controls/radio.json
    scripts/gen_controls_diagram.py docs/diagrams/controls/radio.json --render svg
    scripts/gen_controls_diagram.py docs/diagrams/controls/edrums.json -o /tmp/x.d2 --render pdf
"""

import argparse
import json
import os
import re
import subprocess
import sys

# Template block id -> (JSON section, deck) for the value cells we fill. Deck "A"/"B" pick the
# per-deck override (deckB.<section>); None means a shared/global block.
BLOCK_SECTION = {
    "knobsA": ("knobs", "A"), "knobsB": ("knobs", "B"),
    "padsA":  ("pads", "A"),  "padsB":  ("pads", "B"),
    "ringA":  ("ring", "A"),  "ringB":  ("ring", "B"),
    "xfade":      ("crossfade", None),
    "switches":   ("switches", None),
    "transport":  ("transport", None),
    "cv":         ("cv", None),
    "gates":      ("gates", None),
    "modmidi":    ("mod_midi", None),
}
# Sections whose JSON value is a single string (applied to the block's one fillable cell), not a dict.
STRING_SECTIONS = {"crossfade", "ring"}

_BLOCK_OPEN = re.compile(r'^\s*([^\s:]+(?:\s+[^\s:]+)*)\s*:.*\{\s*$')   # `name: ... {`
_VALUE_ROW  = re.compile(r'^(?P<indent>\s*)(?P<key>"[^"]*"|[^":{}]+?)\s*:\s*"(?P<val>[^"]*)"\s*$')
_QUOTED     = re.compile(r'"[^"]*"')


def d2_escape(s):
    """Make `s` safe inside a d2 "..." literal: escape quotes/backslashes; a real newline -> \\n."""
    s = str(s).replace("\\", "\\\\").replace('"', '\\"')
    return s.replace("\n", "\\n")


def section_for(block_stack):
    """The (section, deck) the current innermost mapped block belongs to, or (None, None)."""
    for blk in reversed(block_stack):
        if blk in BLOCK_SECTION:
            return BLOCK_SECTION[blk]
    return (None, None)


def lookup(spec, section, deck, key):
    """Resolve a value for (section, deck, key); deck B falls back to the shared section. None if absent."""
    if section in STRING_SECTIONS:
        # string section: the value is the whole section entry; `key` is ignored
        if deck == "B" and isinstance(spec.get("deckB"), dict) and section in spec["deckB"]:
            return spec["deckB"][section]
        return spec.get(section)
    src = None
    if deck == "B" and isinstance(spec.get("deckB"), dict):
        src = spec["deckB"].get(section)
    if src is None:
        src = spec.get(section)
    if isinstance(src, dict):
        return src.get(key)
    return None


def generate(template_text, spec):
    """Return (output_text, warnings). Substitutes value cells from `spec` into the template."""
    out, stack, warnings, used = [], [], [], set()
    title = spec.get("title") or f"{spec.get('engine', 'engine').capitalize()} - control surface"
    subtitle = spec.get("subtitle")

    # Drop the template's leading "# ... blank template ..." comment block; our own generated header
    # replaces it (so the engine file doesn't carry the "copy this per engine" boilerplate).
    lines = template_text.splitlines()
    start = 0
    while start < len(lines) and (lines[start].lstrip().startswith("#") or not lines[start].strip()):
        start += 1

    for raw in lines[start:]:
        line = raw

        # --- title / subtitle (the |md header block) ---
        if re.match(r'^\s*#\s*Spotykach Control Surface', line):
            out.append(f"  # {title}")
            _adjust_stack(stack, raw)
            continue
        if re.match(r'^\s*_Fixed platform controls', line):
            out.append(f"  _{subtitle}_" if subtitle else "  _per-engine control functions_")
            _adjust_stack(stack, raw)
            continue

        section, deck = section_for(stack)

        # --- LED ring label: `label: "Ring A\n[ ... ]"` ---
        if section == "ring":
            m = re.match(r'^(?P<indent>\s*)label:\s*"(?P<ring>Ring [AB])\\n[^"]*"\s*$', line)
            if m:
                val = lookup(spec, "ring", deck, None)
                if val is not None:
                    used.add(("ring", deck))
                    out.append(f'{m.group("indent")}label: "{m.group("ring")}\\n{d2_escape(val)}"')
                    _adjust_stack(stack, raw)
                    continue

        # --- value cell: `Key: "value"` inside a mapped block ---
        if section is not None:
            m = _VALUE_ROW.match(line)
            if m:
                key = m.group("key").strip().strip('"')
                val = lookup(spec, section, deck, key)
                if val is not None:
                    used.add((section, deck, key) if section not in STRING_SECTIONS else (section, deck))
                    line = f'{m.group("indent")}{m.group("key")}: "{d2_escape(val)}"'

        out.append(line)
        _adjust_stack(stack, raw)

    warnings += _unfilled(out)
    warnings += _unused_spec_keys(spec, used)
    return "\n".join(out) + "\n", warnings


def _adjust_stack(stack, raw):
    """Update the block stack by the net brace balance of a line (strings stripped first)."""
    stripped = _QUOTED.sub('""', raw)
    net = stripped.count("{") - stripped.count("}")
    if net > 0:
        m = _BLOCK_OPEN.match(raw)
        name = m.group(1).strip() if m else "_anon"
        stack.append(name)
        for _ in range(net - 1):
            stack.append("_anon")
    elif net < 0:
        for _ in range(-net):
            if stack:
                stack.pop()


def _unfilled(out_lines):
    """Warn about mapped value cells still holding a `[ ... ]` placeholder."""
    warns, stack = [], []
    for raw in out_lines:
        section, _ = section_for(stack)
        if section is not None:
            m = _VALUE_ROW.match(raw)
            if m and "[" in m.group("val") and "]" in m.group("val"):
                warns.append(f"unfilled: {section} / {m.group('key').strip().strip(chr(34))}")
            if section == "ring" and re.search(r'\\n\[.*\]"', raw):
                warns.append("unfilled: ring label")
        _adjust_stack(stack, raw)
    return warns


def _unused_spec_keys(spec, used):
    """Warn about JSON keys that matched no template control (likely typos)."""
    warns = []
    for section in ("knobs", "pads", "switches", "transport", "cv", "gates", "mod_midi"):
        for deck in (None, "A", "B"):
            entry = spec.get(section) if deck != "B" else (spec.get("deckB", {}) or {}).get(section)
            if not isinstance(entry, dict):
                continue
            for key in entry:
                # consider it used if any deck consumed it
                if not any((section, d, key) in used for d in ("A", "B", None)):
                    warns.append(f"spec key never matched the template: {section}.{key}")
    return warns


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("spec", help="JSON control-spec file")
    p.add_argument("--template", default="docs/diagrams/controls-template.d2",
                   help="d2 template (default: docs/diagrams/controls-template.d2)")
    p.add_argument("-o", "--out", help="output .d2 path (default: docs/diagrams/<engine>-controls.d2)")
    p.add_argument("--render", choices=["svg", "pdf"],
                   help="also render with the d2 CLI to <media-dir>/<engine>-controls.<ext>")
    p.add_argument("--media-dir", default="docs/media", help="render output dir (default: docs/media)")
    p.add_argument("--d2", default="d2", help="d2 CLI binary (default: d2)")
    args = p.parse_args()

    try:
        with open(args.spec, encoding="utf-8") as f:
            spec = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"error reading spec {args.spec}: {e}", file=sys.stderr)
        return 1
    try:
        with open(args.template, encoding="utf-8") as f:
            template = f.read()
    except OSError as e:
        print(f"error reading template {args.template}: {e}", file=sys.stderr)
        return 1

    engine = spec.get("engine") or os.path.splitext(os.path.basename(args.spec))[0]
    out_path = args.out or os.path.join("docs", "diagrams", f"{engine}-controls.d2")

    text, warnings = generate(template, spec)
    header = (f"# GENERATED from {os.path.basename(args.template)} by "
              f"scripts/gen_controls_diagram.py.\n"
              f"# Edit the JSON spec ({os.path.basename(args.spec)}), not this file; then re-run / `make diagrams`.\n\n")
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header + text)
    print(f"wrote {out_path}")

    for w in warnings:
        print(f"  warning: {w}", file=sys.stderr)

    if args.render:
        os.makedirs(args.media_dir, exist_ok=True)
        render_path = os.path.join(args.media_dir, f"{engine}-controls.{args.render}")
        try:
            subprocess.run([args.d2, out_path, render_path], check=True)
            print(f"rendered {render_path}")
        except FileNotFoundError:
            print(f"error: '{args.d2}' not found - install d2 from https://d2lang.com", file=sys.stderr)
            return 1
        except subprocess.CalledProcessError as e:
            print(f"error: d2 render failed: {e}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
