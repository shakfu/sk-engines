#!/usr/bin/env python3
"""Batch-convert audio files to the sk-engines streaming/load format (tape, shuttle).

The `tape` and `shuttle` engines stream (tape) or load (shuttle) **MONO 32-bit IEEE
float WAV at 48 kHz** (`AudioFormat 3`). The firmware does NO conversion on the audio
path - it reads file body bytes straight into float frames - so any other encoding is
reinterpreted as garbage. As of the format-validation fix the firmware *rejects* a
non-conforming file instead (the deck's amber error LED strobes), but it still won't
play it. This script decodes arbitrary inputs to raw float with ffmpeg (or sox) and then
writes the WAV container itself, optionally naming the outputs as deck slot files.

Why write the header ourselves instead of letting ffmpeg write the `.wav`: ffmpeg's float
encoder emits a `fact` chunk, and a source exported from a DAW (Logic, etc.) carries a
`LIST`/INFO metadata block too - both land BETWEEN `fmt ` and `data`, pushing the audio
body well past the canonical offset 44 (Logic output lands `data` at byte 128). A firmware
WAV reader that assumes a fixed body offset then plays that metadata as audio = NOISE; an
older reader with a small header window rejects the file outright = silence. We instead
emit a byte-identical copy of the 44-byte header the firmware's OWN recorder writes (`fmt `
size 16, no `fact`, no `LIST`, `data` at offset 44), so the output plays on every firmware
version regardless of how its header parser works.

Supported/target format (what the firmware accepts):
    container : WAV (RIFF)
    encoding  : 32-bit IEEE float  (ffmpeg pcm_f32le / sox -e floating-point -b 32)
    channels  : 1 (mono; stereo is down-mixed)
    rate      : 48000 Hz (no resampling on-device, so 44.1k etc. must be converted)

Slot layout on the card (8 slots per deck, selected with Alt+PITCH):
    tape    -> /tapes/tape_<a|b>_<1..8>.wav
    shuttle -> /shuttle/tape_<a|b>_<1..8>.wav   (RAM-capped: ~30 s per track)

Examples:
    # Convert a few files, keeping their names, into ./out
    scripts/convert_tape_audio.py -o out kick.wav loop.aiff vocal.mp3

    # Fill deck A's slots 1.. for the tape engine (-> out/tapes/tape_a_1.wav ...)
    scripts/convert_tape_audio.py --engine tape --deck a -o out beat1.wav beat2.wav

    # Shuttle deck B, starting at slot 3, warn on anything over 30 s
    scripts/convert_tape_audio.py --engine shuttle --deck b --start-slot 3 -o card take*.wav

    # Use sox instead of ffmpeg
    scripts/convert_tape_audio.py --tool sox in.wav
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

TARGET_RATE = 48000
TARGET_BITS = 32
TARGET_FMT = 3            # WAVE_FORMAT_IEEE_FLOAT
SLOTS_PER_DECK = 8
SHUTTLE_MAX_SECONDS = 30  # per-track RAM cap (kBufSeconds in shuttle_engine.h)
ENGINE_DIR = {"tape": "tapes", "shuttle": "shuttle"}


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def ffmpeg_cmd(src):
    # Decode to headerless raw mono 48k float32 on stdout; we add the WAV header ourselves.
    return ["ffmpeg", "-nostdin", "-loglevel", "error",
            "-i", src, "-ac", "1", "-ar", str(TARGET_RATE), "-f", "f32le", "-"]


def sox_cmd(src):
    # `-t f32` = raw 32-bit float; output rate/channels make sox resample + down-mix. Raw on stdout.
    return ["sox", src, "-t", "f32", "-r", str(TARGET_RATE), "-c", "1", "-"]


def canonical_header(data_bytes):
    """The exact 44-byte header the firmware's recorder writes (see src/memory/wav.h WavHeader):
    `fmt ` size 16, float/mono/48k, then `data` - no `fact`/`LIST`, so the body sits at offset 44."""
    byte_rate  = TARGET_RATE * (TARGET_BITS // 8)        # mono: blockalign == bytes/sample
    block_align = TARGET_BITS // 8
    return (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, TARGET_FMT, 1, TARGET_RATE,
                                    byte_rate, block_align, TARGET_BITS)
            + b"data" + struct.pack("<I", data_bytes))


def convert(tool_cmd, src, dst):
    """Decode `src` to raw float via the tool, then write a canonical-header WAV to `dst`.
    Returns the audio duration in seconds. Raises subprocess.CalledProcessError on a decode failure."""
    raw = subprocess.run(tool_cmd(src), check=True, stdout=subprocess.PIPE).stdout
    raw = raw[:len(raw) - (len(raw) % 4)]               # whole float frames only (defensive)
    with open(dst, "wb") as f:
        f.write(canonical_header(len(raw)))
        f.write(raw)
    return len(raw) / (TARGET_RATE * (TARGET_BITS // 8))


def verify(path):
    """Parse the WAV header; return (ok, detail). Mirrors the firmware's WavStreamReader guard."""
    with open(path, "rb") as f:
        b = f.read(512)
    if b[0:4] != b"RIFF" or b[8:12] != b"WAVE":
        return False, "not a RIFF/WAVE file"
    cur, fmt, data_off, data_sz = 12, None, None, None
    while cur + 8 <= len(b):
        cid = b[cur:cur + 4]
        sz = struct.unpack("<I", b[cur + 4:cur + 8])[0]
        body = cur + 8
        if cid == b"fmt ":
            af, ch = struct.unpack("<HH", b[body:body + 4])
            sr = struct.unpack("<I", b[body + 4:body + 8])[0]
            bits = struct.unpack("<H", b[body + 14:body + 16])[0]
            fmt = (af, ch, sr, bits)
        elif cid == b"data":
            data_off, data_sz = body, sz
            break
        cur = body + sz + (sz & 1)
    if fmt is None or data_off is None:
        return False, "missing fmt/data chunk"
    af, ch, sr, bits = fmt
    if (af, ch, sr, bits) != (TARGET_FMT, 1, TARGET_RATE, TARGET_BITS):
        return False, f"format {fmt} != (3,1,48000,32) float/mono/48k"
    if data_off > 256:                       # firmware reads a 256-byte header window
        return False, f"data chunk at byte {data_off} is past the 256-byte header window"
    seconds = data_sz / (TARGET_RATE * (TARGET_BITS // 8))
    return True, f"mono float32 48k, {seconds:.1f}s, data@{data_off}"


def main():
    ap = argparse.ArgumentParser(description="Batch-convert audio to the sk-engines tape/shuttle format.")
    ap.add_argument("inputs", nargs="+", help="input audio files (any format ffmpeg/sox can read)")
    ap.add_argument("-o", "--out", default=".", help="output directory (default: current dir)")
    ap.add_argument("--engine", choices=ENGINE_DIR, help="place outputs in the engine's card subdir (tapes/ or shuttle/)")
    ap.add_argument("--deck", choices=["a", "b"], help="name outputs as deck slot files tape_<deck>_<n>.wav")
    ap.add_argument("--start-slot", type=int, default=1, help="first slot number when --deck is given (1..8)")
    ap.add_argument("--tool", choices=["ffmpeg", "sox"], default="ffmpeg", help="converter to use (default: ffmpeg)")
    ap.add_argument("--no-verify", action="store_true", help="skip the output header check")
    args = ap.parse_args()

    if shutil.which(args.tool) is None:
        die(f"{args.tool} not found on PATH")
    if args.deck and not (1 <= args.start_slot <= SLOTS_PER_DECK):
        die(f"--start-slot must be 1..{SLOTS_PER_DECK}")

    out_dir = args.out
    if args.engine:
        out_dir = os.path.join(out_dir, ENGINE_DIR[args.engine])
    os.makedirs(out_dir, exist_ok=True)

    build = ffmpeg_cmd if args.tool == "ffmpeg" else sox_cmd
    max_seconds = SHUTTLE_MAX_SECONDS if args.engine == "shuttle" else None
    slot = args.start_slot
    rc = 0

    for src in args.inputs:
        if not os.path.isfile(src):
            print(f"skip (not a file): {src}", file=sys.stderr); rc = 1; continue
        if args.deck:
            if slot > SLOTS_PER_DECK:
                print(f"skip (only {SLOTS_PER_DECK} slots/deck): {src}", file=sys.stderr); rc = 1; continue
            name = f"tape_{args.deck}_{slot}.wav"
            slot += 1
        else:
            name = os.path.splitext(os.path.basename(src))[0] + ".wav"
        dst = os.path.join(out_dir, name)

        try:
            seconds = convert(build, src, dst)
        except subprocess.CalledProcessError:
            print(f"FAIL  {src}: {args.tool} could not decode it", file=sys.stderr); rc = 1; continue

        detail = ""
        if not args.no_verify:
            ok, detail = verify(dst)
            if not ok:
                print(f"FAIL  {src} -> {dst}: {detail}", file=sys.stderr); rc = 1; continue
        if max_seconds and seconds > max_seconds:
            print(f"WARN  {dst}: {seconds:.1f}s exceeds the shuttle ~{max_seconds}s RAM cap "
                  f"(it will be truncated on load)", file=sys.stderr)
        print(f"ok    {src} -> {dst}" + (f"   [{detail}]" if detail else ""))

    sys.exit(rc)


if __name__ == "__main__":
    main()
