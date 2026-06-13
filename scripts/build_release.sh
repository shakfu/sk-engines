#!/usr/bin/env bash
#
# Build a set of engine firmware binaries for distribution.
#
# Each engine is a separate firmware variant of the same device (selected at build time via
# ENGINE=, flashed over DFU). This script does a clean build of each engine in RELEASE_ENGINES,
# stamps the chosen version into the binary (so the artifact name and the banner inside it agree -
# verify with `arm-none-eabi-strings <file>.bin | grep '^spotykach '`), and collects the results
# under dist/<version>/ with SHA-256 checksums, the matching bootloader, and flashing notes.
#
# Usage:
#   scripts/build_release.sh [VERSION] [ENGINE ...]
#
#   VERSION   Release version baked into every binary and used in artifact names and the output
#             directory. Defaults to `git describe --tags --always`. On a clean tagged checkout
#             that is the bare tag (e.g. 0.3.0); pass it explicitly to override.
#   ENGINE..  Engines to build. Defaults to $RELEASE_ENGINES, else the curated list below. These
#             are the engines considered mature enough to publish - edit the default to taste.
#
# Examples:
#   scripts/build_release.sh                 # describe-derived version, default engine set
#   scripts/build_release.sh 0.3.0           # explicit version, default engine set
#   scripts/build_release.sh 0.3.0 reverb delay   # explicit version, just two engines
set -euo pipefail

cd "$(dirname "$0")/.."

# Curated "mature" engine set: the engines stable enough to publish prebuilt. Others stay
# buildable but unlisted. Override per-invocation with args or the RELEASE_ENGINES env var.
DEFAULT_ENGINES="reverb delay"

VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
[ $# -gt 0 ] && shift || true
if [ $# -gt 0 ]; then
  ENGINES="$*"
else
  ENGINES="${RELEASE_ENGINES:-$DEFAULT_ENGINES}"
fi

GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_DIRTY=""
git diff --quiet 2>/dev/null || GIT_DIRTY=" (dirty tree - not a clean release build)"
BOOT_BIN="bootloader-spotykach-v2.bin"

OUT="dist/$VERSION"
JOBS="${JOBS:-8}"

# Prefer the repo's sha256 tool; fall back across platforms.
sha256() {
  if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$@";
  elif command -v sha256sum >/dev/null 2>&1; then sha256sum "$@";
  else echo "no sha256 tool found" >&2; return 1; fi
}

echo "Release version : $VERSION$GIT_DIRTY"
echo "Git commit      : $GIT_SHA"
echo "Engines         : $ENGINES"
echo "Output          : $OUT/"
echo

rm -rf "$OUT"
mkdir -p "$OUT"

MANIFEST="$OUT/MANIFEST.txt"
{
  echo "spotykach firmware release"
  echo "version:    $VERSION$GIT_DIRTY"
  echo "git commit: $GIT_SHA"
  echo "bootloader: $BOOT_BIN (must be flashed to internal flash first; see FLASHING.md)"
  echo
  printf "%-14s %12s  %s\n" "engine" "bytes" "binary"
} > "$MANIFEST"

for ENGINE in $ENGINES; do
  echo "==> building $ENGINE"
  make clean >/dev/null
  # SPK_VERSION makes the in-binary banner match the artifact name exactly.
  make -j"$JOBS" ENGINE="$ENGINE" SPK_VERSION="$VERSION" >/dev/null

  base="spotykach-$ENGINE-$VERSION"
  cp build/spotykach.bin "$OUT/$base.bin"
  cp build/spotykach.hex "$OUT/$base.hex"

  # Sanity-check the baked-in banner before we trust the artifact.
  if ! arm-none-eabi-strings "$OUT/$base.bin" | grep -q "^spotykach $VERSION engine=$ENGINE$"; then
    echo "ERROR: $base.bin is missing the expected version banner" >&2
    exit 1
  fi

  bytes=$(wc -c < "$OUT/$base.bin" | tr -d ' ')
  printf "%-14s %12s  %s\n" "$ENGINE" "$bytes" "$base.bin" >> "$MANIFEST"
done

# Ship the bootloader alongside the apps and a single checksum file over everything.
cp "$BOOT_BIN" "$OUT/"

( cd "$OUT" && sha256 ./*.bin ./*.hex > SHA256SUMS )

cat > "$OUT/FLASHING.md" <<EOF
# Flashing a spotykach engine ($VERSION)

Each \`.bin\` here is a complete firmware for one engine. Flash exactly one at a time.

## One-time: bootloader

These app binaries run under the spotykach bootloader (\`$BOOT_BIN\`). If your device does
not already have it, flash it to internal flash once:

    dfu-util -a 0 -s 0x08000000:leave -D $BOOT_BIN -d ,0483:df11

## Flash an engine

1. Put the device in DFU mode (hold Reset ~3s until the bottom pad LEDs breathe white).
2. Flash the engine you want (QSPI app address):

       dfu-util -a 0 -s 0x90040000:leave -D spotykach-<engine>-$VERSION.bin -d ,0483:df11

## Verify

- Confirm the download is intact:  \`shasum -a 256 -c SHA256SUMS\`
- Confirm what a binary is:         \`arm-none-eabi-strings <file>.bin | grep '^spotykach '\`
  (prints e.g. \`spotykach $VERSION engine=reverb\`)

Settings note: a release may change the persistent-settings layout. If the device behaves oddly
after an upgrade, reset stored settings.
EOF

echo
echo "Done. Artifacts in $OUT/"
ls -1 "$OUT"
