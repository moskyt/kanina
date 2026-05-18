#!/usr/bin/env bash
#
# Release a new firmware version:
#   1. Bump FIRMWARE_VERSION in config.h (if not already)
#   2. Build with arduino-cli
#   3. Commit + push the bump
#   4. Create a GitHub release with the matching tag and attach firmware.bin
#
# Usage:
#   ./release.sh v0.1.1

set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <version-tag>   (e.g. $0 v0.1.1)" >&2
  exit 2
fi

VERSION="$1"
FQBN="arduino:renesas_uno:unor4wifi"

SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH_NAME="$(basename "$SKETCH_DIR")"
BUILD_DIR="${SKETCH_DIR}/build"
BIN_PATH="${BUILD_DIR}/${SKETCH_NAME}.ino.bin"
CONFIG_H="${SKETCH_DIR}/config.h"

cd "$SKETCH_DIR"

# --- preflight ---------------------------------------------------------------

command -v arduino-cli >/dev/null || { echo "arduino-cli is not installed" >&2; exit 1; }
command -v gh          >/dev/null || { echo "gh CLI is not installed"       >&2; exit 1; }

case "$VERSION" in
  v*) ;;
  *) echo "version must start with 'v' (got '$VERSION')" >&2; exit 2 ;;
esac

if git rev-parse --verify --quiet "refs/tags/$VERSION" >/dev/null; then
  echo "tag $VERSION already exists locally" >&2; exit 1
fi
if git ls-remote --exit-code --tags origin "refs/tags/$VERSION" >/dev/null 2>&1; then
  echo "tag $VERSION already exists on origin" >&2; exit 1
fi

# Allow uncommitted changes only in config.h (which we're about to bump anyway).
DIRTY="$(git status --porcelain | grep -vE ' (firmware_v2/)?config\.h$' || true)"
if [ -n "$DIRTY" ]; then
  echo "working tree has uncommitted changes outside config.h:" >&2
  echo "$DIRTY" >&2
  exit 1
fi

# --- bump FIRMWARE_VERSION ---------------------------------------------------

CURRENT="$(grep -E 'FIRMWARE_VERSION[[:space:]]*=' "$CONFIG_H" | sed -E 's/.*"([^"]+)".*/\1/')"
echo "current FIRMWARE_VERSION: $CURRENT"

if [ "$CURRENT" != "$VERSION" ]; then
  echo "bumping config.h: $CURRENT -> $VERSION"
  sed -i.bak -E "s/(FIRMWARE_VERSION[[:space:]]*=[[:space:]]*\")[^\"]+(\")/\1${VERSION}\2/" "$CONFIG_H"
  rm -f "${CONFIG_H}.bak"
fi

# --- build -------------------------------------------------------------------

echo "building $SKETCH_NAME for $FQBN..."
rm -rf "$BUILD_DIR"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" .

if [ ! -f "$BIN_PATH" ]; then
  echo "build did not produce $BIN_PATH" >&2
  exit 1
fi
echo "built: $BIN_PATH ($(wc -c < "$BIN_PATH") bytes)"

# --- commit + tag + push -----------------------------------------------------

if ! git diff --quiet -- "$CONFIG_H"; then
  git add "$CONFIG_H"
  git commit -m "$VERSION"
fi

git tag "$VERSION"
git push
git push origin "$VERSION"

# --- release -----------------------------------------------------------------

gh release create "$VERSION" \
  "${BIN_PATH}#firmware.bin" \
  --title "$VERSION" \
  --notes "Firmware build $VERSION"

echo "done: $VERSION released"
