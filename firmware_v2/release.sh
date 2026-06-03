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

# Link-time optimization. Worth ~10% flash (~13 KB on this firmware), which
# matters because OTA is capped at half the flash (see the OTA_MAX guard below).
# -Wl,-u,cm_backtrace_fault forces the Renesas fault handler to be kept: it's
# only referenced from the core's assembly, so LTO's symbol GC drops it
# otherwise ("undefined reference to cm_backtrace_fault" at link). -ffat-lto-objects
# keeps the build robust if a precompiled .a in the mix lacks LTO bytecode.
LTO_PROPS=(
  --build-property "compiler.c.extra_flags=-flto -ffat-lto-objects"
  --build-property "compiler.cpp.extra_flags=-flto -ffat-lto-objects"
  --build-property "compiler.c.elf.extra_flags=-flto -fuse-linker-plugin -Wl,-u,cm_backtrace_fault"
)

SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH_NAME="$(basename "$SKETCH_DIR")"
BUILD_DIR="${SKETCH_DIR}/build"
BIN_PATH="${BUILD_DIR}/${SKETCH_NAME}.ino.bin"
# GitHub stores assets under their basename; the `#name` syntax to `gh release`
# is only a display label, not the stored filename. So we upload under the
# exact name the device expects (config__github_asset = "firmware.bin").
ASSET_PATH="${BUILD_DIR}/firmware.bin"
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
# Scope to this directory (we're already cd'd into it): unrelated changes
# elsewhere in the repo (docs, drawings, the cam project) shouldn't block a
# firmware release.
DIRTY="$(git status --porcelain -- . | grep -vE ' (firmware_v2/)?config\.h$' || true)"
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

echo "building $SKETCH_NAME for $FQBN (LTO)..."
rm -rf "$BUILD_DIR"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "${LTO_PROPS[@]}" .

if [ ! -f "$BIN_PATH" ]; then
  echo "build did not produce $BIN_PATH" >&2
  exit 1
fi
cp "$BIN_PATH" "$ASSET_PATH"
BIN_SIZE="$(wc -c < "$ASSET_PATH")"
echo "built: $ASSET_PATH ($BIN_SIZE bytes)"

# OTA ceiling: the R4 ArduinoOTA scheme stages the image in the upper half of
# usable flash, so the largest OTA-flashable image is (MAX_FLASH - SKETCH_START)/2
# page-aligned = (262144 - 0x4000)/2 floored to 0x800 = 122880 bytes. A bigger
# image still flashes fine over USB but fails OTA at InternalStorage.open()
# ("storage open" / "Too big for OTA" on the device). Block the release so we
# don't ship a firmware that can never self-update.
OTA_MAX=122880
if [ "$BIN_SIZE" -gt "$OTA_MAX" ]; then
  echo "ERROR: $ASSET_PATH is $BIN_SIZE bytes, over the $OTA_MAX-byte OTA limit." >&2
  echo "Devices could not self-update to this build (USB flashing would still work)." >&2
  echo "Shrink the firmware (e.g. trim features/strings) before releasing." >&2
  exit 1
fi

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
  "$ASSET_PATH" \
  --title "$VERSION" \
  --notes "Firmware build $VERSION"

echo "done: $VERSION released"
