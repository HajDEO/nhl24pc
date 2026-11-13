#!/usr/bin/env bash
# Build a native Linux release archive entirely from this repository.
#
#   ./docs/linux/make-linux-repack.sh [BINARY_OR_BUILD_DIR] [OUTPUT_ARCHIVE]
#
# BINARY_OR_BUILD_DIR defaults to build/nhl24. No game files are included.
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BIN=${1:-"$REPO/build/nhl24"}
if [ -d "$BIN" ]; then
    BIN="$BIN/nhl24"
fi
VERSION=$(sed -n 's/^#define NHLPORT_VERSION "\([^"]*\)"/\1/p' "$REPO/src/common/port_version.h")
[ -n "$VERSION" ] || { echo "could not read NHLPORT_VERSION"; exit 1; }
NAME="NHL24-PC-Linux-v$VERSION"
OUT=${2:-"$PWD/$NAME.tar.gz"}

[ -x "$BIN" ] || { echo "no $BIN - build the emulator first"; exit 1; }
for f in nhl24-linux.py nhl24-launcher.py LINUX.md LINUX_RU.md; do
    [ -f "$HERE/$f" ] || { echo "missing $HERE/$f"; exit 1; }
done
for f in release/user/config.json release/user/input_config/default.ini \
         release/user/input_config/global.ini game_patches/nhl24.xml LICENSE; do
    [ -f "$REPO/$f" ] || { echo "missing $REPO/$f"; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
D="$TMP/$NAME"
mkdir -p "$D/user/input_config" "$D/user/patches" "$D/source"

# Native binary, launcher and installer.
install -m755 "$BIN" "$D/nhl24"
install -m755 "$HERE/nhl24-linux.py" "$HERE/nhl24-launcher.py" "$D/"
printf '%s\n' "$VERSION" > "$D/VERSION"

# Portable user directory and the current 1.00/1.60 game patch.
install -m644 "$REPO/release/user/config.json" "$D/config_default.json"
install -m644 "$REPO/release/user/config.json" "$D/user/config.json"
install -m644 "$REPO/release/user/input_config/default.ini" \
    "$REPO/release/user/input_config/global.ini" "$D/user/input_config/"
install -m644 "$REPO/game_patches/nhl24.xml" "$D/user/patches/"

# Documentation, license and exact source revision for this binary.
install -m644 "$HERE/LINUX.md" "$D/README.md"
install -m644 "$HERE/LINUX.md" "$HERE/LINUX_RU.md" "$D/"
install -m644 "$REPO/LICENSE" "$D/LICENSE"
COMMIT=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)
SOURCE_REPOSITORY=${NHL24_SOURCE_URL:-}
if [ -z "$SOURCE_REPOSITORY" ]; then
    SOURCE_REPOSITORY=$(git -C "$REPO" remote get-url origin 2>/dev/null || true)
fi
SOURCE_REPOSITORY=${SOURCE_REPOSITORY:-https://github.com/HajDEO/nhl24pc}
{
    echo "Source: $SOURCE_REPOSITORY"
    echo "Commit: $COMMIT"
    echo "License: GPL-2.0-or-later (see LICENSE)"
} > "$D/source/SOURCE.txt"

GLIBC=unknown
GLIBCXX=unknown
if command -v objdump >/dev/null 2>&1; then
    GLIBC=$(objdump -T "$D/nhl24" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sort -Vu | tail -1 || true)
    GLIBCXX=$(objdump -T "$D/nhl24" 2>/dev/null | grep -o 'GLIBCXX_[0-9][0-9.]*' | sort -Vu | tail -1 || true)
    GLIBC=${GLIBC:-unknown}
    GLIBCXX=${GLIBCXX:-unknown}
fi
{
    echo "NHL24 PC Linux v$VERSION"
    echo "Source: $SOURCE_REPOSITORY"
    echo "Commit: $COMMIT"
    echo "Required glibc symbol version: $GLIBC"
    echo "Required libstdc++ symbol version: $GLIBCXX"
} > "$D/BUILD-INFO.txt"
echo "Emulator needs $GLIBC and $GLIBCXX or newer."
if command -v ldd >/dev/null 2>&1; then
    echo "Dynamic libraries:"
    ldd "$D/nhl24" 2>&1 | sed 's/^/   /' || true
fi

tar -C "$TMP" -czf "$OUT" "$NAME"
echo
echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
