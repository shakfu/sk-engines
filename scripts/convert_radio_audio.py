#!/usr/bin/env python3
"""Batch-convert audio files to the `radio` engine's station format.

The `radio` engine (dual virtual RadioMusic) streams **headerless RAW signed 16-bit
mono PCM, little-endian, at 48 kHz** (`.raw`) - the same family of files the original
Music Thing Modular RadioMusic uses, fixed here to the device's 48 kHz so no on-device
resampling is needed. The firmware does NO conversion on the audio path: it reads the
file body straight into int16 frames and a station's length is simply `filesize / 2`.

Card layout (a shared library both decks browse):
    /radio/<bank>/<name>.raw     bank = 0..15 (folders), up to 48 stations per folder

Names must be short (8.3, <=12 chars) so the firmware's directory scan can re-open them;
this script emits zero-padded numeric names (01.raw, 02.raw, ...) by default.

The original RadioMusic library is itself HEADERLESS 16-bit-mono .raw at 44.1 kHz, so a
decoder cannot autodetect its format - this script states it (--in-rate, default 44100)
and resamples to the device's 48 kHz. Self-describing inputs (.wav/.mp3/...) read normally.

Examples:
    # Mirror a whole RadioMusic card (its 0..15 folders) into out/radio/0..15, keeping names
    scripts/convert_radio_audio.py mirror --keep-names -o /Volumes/SD ~/Downloads/16gb-Disk

    # Convert a few files into bank 0 (-> out/radio/0/01.raw, 02.raw, ...)
    scripts/convert_radio_audio.py convert --bank 0 -o out a.wav b.mp3 c.aiff

    # Convert every audio file in a directory (sorted) into bank 3
    scripts/convert_radio_audio.py from-dir --bank 3 -o card ./samples

    # Keep original (8.3-safe) basenames instead of renumbering
    scripts/convert_radio_audio.py convert --bank 1 --keep-names -o out 01.wav 02.wav

Requires ffmpeg (default) or sox on PATH.
"""

import argparse
import os
import shutil
import subprocess
import sys

TARGET_RATE = 48000
MAX_BANKS = 16
MAX_STATIONS = 48           # mirrors kMaxStations in radio_engine.h
AUDIO_EXTS = (".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".ogg", ".aac", ".raw")


def have(tool):
    return shutil.which(tool) is not None


def convert_one(src, dst, tool, in_rate, fmt):
    """Decode `src` to a 16-bit mono 48 kHz station at `dst`. Returns True on success.

    `fmt` is "raw" (headerless s16le, the original RadioMusic format) or "wav" (a 16-bit-mono PCM WAV,
    which carries its own sample rate so it needs no rate.txt on the card). A `.raw` INPUT is headerless,
    so the decoder cannot autodetect its layout - we state it: signed 16-bit mono at `in_rate` (the
    original cards are 44.1 kHz). Container inputs (.wav/.mp3/...) are self-describing and read normally.
    """
    is_raw_in = src.lower().endswith(".raw")
    if tool == "ffmpeg":
        in_flags = (["-f", "s16le", "-ar", str(in_rate), "-ac", "1"] if is_raw_in else [])
        out_flags = (["-f", "s16le"] if fmt == "raw" else ["-f", "wav"])
        cmd = (["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + in_flags +
               ["-i", src, "-ac", "1", "-ar", str(TARGET_RATE), "-acodec", "pcm_s16le"] + out_flags + [dst])
    elif tool == "sox":
        in_flags = (["-t", "raw", "-r", str(in_rate), "-e", "signed-integer", "-b", "16", "-c", "1", "-L"]
                    if is_raw_in else [])
        out_flags = (["-t", "raw"] if fmt == "raw" else ["-t", "wav"])
        cmd = ["sox"] + in_flags + [src] + out_flags + ["-r", str(TARGET_RATE), "-c", "1",
                                                        "-e", "signed-integer", "-b", "16", "-L", dst]
    else:
        raise ValueError(f"unknown tool {tool}")
    try:
        subprocess.run(cmd, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ERROR converting {src}: {e}", file=sys.stderr)
        return False


def out_name(index, src, keep_names, fmt):
    ext = "." + fmt    # ".raw" or ".wav"
    if keep_names:
        base = os.path.splitext(os.path.basename(src))[0]
        base = base[:8]                      # keep the 8.3 stem short / re-openable
        return f"{base}{ext}"
    return f"{index:02d}{ext}"


def list_audio(directory, recursive=False):
    srcs = []
    walker = os.walk(directory) if recursive else [(directory, [], sorted(os.listdir(directory)))]
    for root, _dirs, files in walker:
        for f in sorted(files):
            # Skip macOS metadata: .DS_Store and the AppleDouble companions ._NAME.raw (which would
            # otherwise match the .raw extension and convert to a tiny garbage "station").
            if f.startswith("."):
                continue
            if f.lower().endswith(AUDIO_EXTS):
                srcs.append(os.path.join(root, f))
    return srcs


def convert_into_bank(srcs, bank, args, tool):
    """Convert `srcs` into out/radio/<bank>/. Returns (ok, total)."""
    if len(srcs) > MAX_STATIONS:
        print(f"  note: {len(srcs)} files > {MAX_STATIONS} stations/bank cap; extras beyond "
              f"the first {MAX_STATIONS} will not be reachable.", file=sys.stderr)
        srcs = srcs[:MAX_STATIONS]
    bank_dir = os.path.join(args.outdir, "radio", str(bank))
    os.makedirs(bank_dir, exist_ok=True)
    ok = 0
    for i, src in enumerate(srcs, start=1):
        dst = os.path.join(bank_dir, out_name(i, src, args.keep_names, args.format))
        print(f"  [{bank}] {src} -> {dst}")
        if convert_one(src, dst, tool, args.in_rate, args.format):
            ok += 1
    return ok, len(srcs)


def run(args):
    tool = args.tool
    if not have(tool):
        print(f"required tool '{tool}' not found on PATH", file=sys.stderr)
        return 1

    # mirror: convert a whole RadioMusic card (numbered bank folders 0..15) into out/radio/0..15.
    if args.command == "mirror":
        ok = total = 0
        found = False
        for b in range(MAX_BANKS):
            srcdir = os.path.join(args.input, str(b))
            if not os.path.isdir(srcdir):
                continue
            found = True
            o, t = convert_into_bank(list_audio(srcdir), b, args, tool)
            ok += o; total += t
        if not found:
            print(f"no numbered bank folders (0..{MAX_BANKS - 1}) under {args.input}", file=sys.stderr)
            return 1
        print(f"mirrored {ok}/{total} file(s) into {os.path.join(args.outdir, 'radio')}")
        return 0 if ok == total else 1

    # single-bank commands (convert / from-dir)
    if args.bank < 0 or args.bank >= MAX_BANKS:
        print(f"bank must be 0..{MAX_BANKS - 1}", file=sys.stderr)
        return 1
    srcs = (list_audio(args.input, args.recursive) if args.command == "from-dir" else args.inputs)
    if not srcs:
        print("no input files", file=sys.stderr)
        return 1
    ok, total = convert_into_bank(srcs, args.bank, args, tool)
    print(f"converted {ok}/{total} file(s) into {os.path.join(args.outdir, 'radio', str(args.bank))}")
    return 0 if ok == total else 1


def main():
    # Options shared by every subcommand (a parent parser, so they may appear after the subcommand too).
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--tool", choices=["ffmpeg", "sox"], default="ffmpeg",
                        help="decoder/encoder to use (default: ffmpeg)")
    common.add_argument("-o", "--outdir", default=".",
                        help="output root (the /radio tree is created under it)")
    common.add_argument("--format", choices=["raw", "wav"], default="raw",
                        help="output format: 'raw' (headerless s16le, needs rate.txt for non-48k) or "
                             "'wav' (16-bit PCM WAV, self-describing rate, no rate.txt). Default: raw")
    common.add_argument("--keep-names", action="store_true",
                        help="keep original (truncated 8.3) basenames instead of 01.raw, 02.raw ...")
    common.add_argument("--in-rate", type=int, default=44100,
                        help="sample rate to assume for HEADERLESS .raw inputs (default 44100, the "
                             "original RadioMusic rate). Ignored for self-describing inputs (.wav/...).")

    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    c = sub.add_parser("convert", parents=[common], help="convert the listed files into a bank")
    c.add_argument("--bank", type=int, default=0, help="destination bank folder 0..15")
    c.add_argument("inputs", nargs="+")

    d = sub.add_parser("from-dir", parents=[common], help="convert every audio file in a dir into a bank")
    d.add_argument("--bank", type=int, default=0, help="destination bank folder 0..15")
    d.add_argument("--recursive", action="store_true", help="recurse into subdirectories")
    d.add_argument("input")

    m = sub.add_parser("mirror", parents=[common],
                       help="convert a whole RadioMusic card (numbered bank folders 0..15) into out/radio")
    m.add_argument("input", help="the source card root (containing folders 0, 1, ... 15)")

    args = p.parse_args()
    # `mirror` derives the bank from each folder name; `convert`/`from-dir` carry --bank. Give run() a
    # uniform args object.
    if not hasattr(args, "recursive"):
        args.recursive = False
    if not hasattr(args, "bank"):
        args.bank = 0
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
