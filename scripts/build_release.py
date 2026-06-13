#!/usr/bin/env python3
"""Build distributable engine firmware binaries for download-and-flash users.

Each "engine" is a separate firmware variant of the same device, selected at build
time via ENGINE= and flashed over DFU. This script does a clean build of each engine
in the release set, stamps the chosen version into the binary (so the artifact name
and the banner baked inside it agree), and collects the results under dist/<version>/
with SHA-256 checksums, the matching bootloader, and flashing notes. It lets users who
do not have the ARM toolchain (and the cyfaust/gen-dsp venv the Faust/gen~ engines need)
download a ready-to-flash binary instead of building one.

The in-binary banner ("spotykach <version> engine=<engine>", see src/version.cpp) is the
provenance anchor: this script asserts every artifact contains the banner matching its
filename before trusting it, and the same banner lets anyone identify a stray binary
later (`arm-none-eabi-strings file.bin | grep '^spotykach '`).

Usage:
    python scripts/build_release.py [VERSION] [ENGINE ...]

    VERSION   Version baked into every binary and used in artifact names and the output
              directory. Defaults to `git describe --tags --always` of the source tree.
              On a clean tagged checkout that is the bare tag (e.g. 0.3.0); pass it
              explicitly to override.
    ENGINE..  Engines to build. Defaults to $RELEASE_ENGINES (space-separated), else the
              curated mature set below.

Examples:
    python scripts/build_release.py                # describe-derived version, mature set
    python scripts/build_release.py 0.3.0          # explicit version, mature set
    python scripts/build_release.py 0.3.0 reverb   # explicit version, just one engine
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Curated "mature" engine set: the engines stable enough to publish prebuilt. Others stay
# buildable but unlisted. Override per-invocation with args or the RELEASE_ENGINES env var.
DEFAULT_ENGINES = ["reverb", "delay"]

REPO_ROOT = Path(__file__).resolve().parent.parent
BOOT_BIN = "bootloader-spotykach-v2.bin"

# DFU flash addresses / PID, mirrored from lib/libDaisy/core/Makefile (BOOT_SRAM app type):
# the bootloader goes to internal flash, the app to QSPI, both via the df11 DFU PID.
BOOTLOADER_ADDRESS = "0x08000000"
APP_ADDRESS = "0x90040000"
DFU_PID = "df11"


def git_output(*args: str) -> str:
    """Return stripped stdout of a git command, or '' if git/command fails."""
    try:
        out = subprocess.run(
            ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, check=True
        )
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def default_version() -> str:
    return git_output("describe", "--tags", "--always") or "dev"


def is_dirty() -> bool:
    """True if the working tree has uncommitted changes (so the build is not reproducible)."""
    return subprocess.run(["git", "diff", "--quiet"], cwd=REPO_ROOT).returncode != 0


def banner_bytes(version: str, engine: str) -> bytes:
    """The exact NUL-terminated banner literal src/version.cpp bakes into the binary."""
    return f"spotykach {version} engine={engine}".encode() + b"\x00"


def run_make(*args: str) -> None:
    """Run make in the repo root, surfacing output only on failure."""
    proc = subprocess.run(
        ["make", *args], cwd=REPO_ROOT, capture_output=True, text=True
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"make {' '.join(args)} failed (exit {proc.returncode})")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_engine(engine: str, version: str, jobs: int, out_dir: Path) -> int:
    """Clean-build one engine, copy + verify its artifacts into out_dir, return .bin size."""
    print(f"==> building {engine}")
    run_make("clean")
    # SPK_VERSION makes the in-binary banner match the artifact name exactly.
    run_make(f"-j{jobs}", f"ENGINE={engine}", f"SPK_VERSION={version}")

    base = f"spotykach-{engine}-{version}"
    bin_path = out_dir / f"{base}.bin"
    hex_path = out_dir / f"{base}.hex"
    shutil.copyfile(REPO_ROOT / "build" / "spotykach.bin", bin_path)
    shutil.copyfile(REPO_ROOT / "build" / "spotykach.hex", hex_path)

    # Sanity-check the baked-in banner before we trust the artifact.
    if banner_bytes(version, engine) not in bin_path.read_bytes():
        raise SystemExit(f"ERROR: {bin_path.name} is missing the expected version banner")

    return bin_path.stat().st_size


def write_manifest(path: Path, version: str, dirty: str, git_sha: str, sizes: dict[str, int]) -> None:
    lines = [
        "spotykach firmware release",
        f"version:    {version}{dirty}",
        f"git commit: {git_sha}",
        f"bootloader: {BOOT_BIN} (must be flashed to internal flash first; see FLASHING.md)",
        "",
        f"{'engine':<14} {'bytes':>12}  binary",
    ]
    for engine, size in sizes.items():
        lines.append(f"{engine:<14} {size:>12}  spotykach-{engine}-{version}.bin")
    path.write_text("\n".join(lines) + "\n")


def write_checksums(out_dir: Path) -> None:
    """Write SHA256SUMS over every artifact, in `shasum -a 256 -c`-compatible format."""
    files = sorted(p for p in out_dir.iterdir() if p.suffix in (".bin", ".hex"))
    lines = [f"{sha256(p)}  {p.name}" for p in files]
    (out_dir / "SHA256SUMS").write_text("\n".join(lines) + "\n")


def write_flashing(path: Path, version: str) -> None:
    path.write_text(
        f"""# Flashing a spotykach engine ({version})

Each `.bin` here is a complete firmware for one engine. Flash exactly one at a time.

## One-time: bootloader

These app binaries run under the spotykach bootloader (`{BOOT_BIN}`). If your device does
not already have it, flash it to internal flash once:

    dfu-util -a 0 -s {BOOTLOADER_ADDRESS}:leave -D {BOOT_BIN} -d ,0483:{DFU_PID}

## Flash an engine

1. Put the device in DFU mode (hold Reset ~3s until the bottom pad LEDs breathe white).
2. Flash the engine you want (QSPI app address):

       dfu-util -a 0 -s {APP_ADDRESS}:leave -D spotykach-<engine>-{version}.bin -d ,0483:{DFU_PID}

## Verify

- Confirm the download is intact:  `shasum -a 256 -c SHA256SUMS`
- Confirm what a binary is:         `arm-none-eabi-strings <file>.bin | grep '^spotykach '`
  (prints e.g. `spotykach {version} engine=reverb`)

Settings note: a release may change the persistent-settings layout. If the device behaves oddly
after an upgrade, reset stored settings.
"""
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build distributable, version-stamped engine firmware binaries.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "version", nargs="?", default=None,
        help="release version (default: git describe --tags --always)",
    )
    parser.add_argument(
        "engines", nargs="*",
        help="engines to build (default: $RELEASE_ENGINES, else the curated mature set)",
    )
    parser.add_argument(
        "--jobs", "-j", type=int, default=int(os.environ.get("JOBS", "8")),
        help="parallel make jobs (default: 8 or $JOBS)",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    version = args.version or default_version()
    if args.engines:
        engines = args.engines
    else:
        env = os.environ.get("RELEASE_ENGINES", "").split()
        engines = env or DEFAULT_ENGINES

    git_sha = git_output("rev-parse", "--short", "HEAD") or "unknown"
    dirty = " (dirty tree - not a clean release build)" if is_dirty() else ""
    out_dir = REPO_ROOT / "dist" / version

    print(f"Release version : {version}{dirty}")
    print(f"Git commit      : {git_sha}")
    print(f"Engines         : {' '.join(engines)}")
    print(f"Output          : {out_dir.relative_to(REPO_ROOT)}/\n")

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    sizes = {engine: build_engine(engine, version, args.jobs, out_dir) for engine in engines}

    # Ship the bootloader alongside the apps, then checksum everything together.
    shutil.copyfile(REPO_ROOT / BOOT_BIN, out_dir / BOOT_BIN)
    write_manifest(out_dir / "MANIFEST.txt", version, dirty, git_sha, sizes)
    write_flashing(out_dir / "FLASHING.md", version)
    write_checksums(out_dir)

    print(f"\nDone. Artifacts in {out_dir.relative_to(REPO_ROOT)}/")
    for name in sorted(p.name for p in out_dir.iterdir()):
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
