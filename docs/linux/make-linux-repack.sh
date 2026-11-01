#!/bin/bash
# Builds a Linux version of the NHL24PC repack archive.
#
#   ./make-linux-repack.sh WINDOWS_REPACK.zip NHL24PC_SOURCE_DIR [VERSION]
#
#   WINDOWS_REPACK.zip   the Windows repack (NHL24PC_V3_2.zip): data files are taken from it
#   NHL24PC_SOURCE_DIR   HajDEO/nhl24pc checkout with the repack patch applied and built
#                        (build/nhl24 must exist)
#   VERSION              archive version label, default 3.2-linux1
#
# The Linux scripts (nhl24-linux.py, nhl24-launcher.py, LINUX*.md, the patch) are taken from the
# folder this script is in. No game files go into the archive.
set -euo pipefail

ZIP=${1:?usage: $0 WINDOWS_REPACK.zip NHL24PC_SOURCE_DIR [VERSION]}
SRC=${2:?usage: $0 WINDOWS_REPACK.zip NHL24PC_SOURCE_DIR [VERSION]}
VER=${3:-3.2-linux1}
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
BIN="$SRC/build/nhl24"
NAME="NHL24PC-Linux"
OUT="$PWD/${NAME}_${VER}.tar.gz"

[ -x "$BIN" ] || { echo "no $BIN - build the emulator first"; exit 1; }
for f in nhl24-linux.py nhl24-launcher.py LINUX.md LINUX_RU.md nhl24pc-v3.2-repack.patch; do
    [ -f "$HERE/$f" ] || { echo "missing $HERE/$f"; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
unzip -q "$ZIP" -d "$TMP/win"
WIN=$(dirname "$(find "$TMP/win" -name setup.ps1 -path '*tools*' | head -1)")/..
[ -f "$WIN/tools/config_default.json" ] || { echo "not the NHL24PC repack: tools/config_default.json missing"; exit 1; }

D="$TMP/$NAME"
mkdir -p "$D/tools/mods" "$D/user/input_config" "$D/source"

# emulator (stripped) + Linux scripts
install -m755 "$BIN" "$D/nhl24-linux"
strip "$D/nhl24-linux" 2>/dev/null || true
install -m755 "$HERE/nhl24-linux.py" "$HERE/nhl24-launcher.py" "$D/"
install -m644 "$HERE/LINUX.md" "$HERE/LINUX_RU.md" "$D/"

# data from the Windows repack: emulator config template, national-teams mod, input bindings, guides
install -m644 "$WIN/tools/config_default.json" "$D/tools/"
cp "$WIN"/tools/mods/*.json "$D/tools/mods/"
cp "$WIN"/tools/mods/*.dat "$D/tools/mods/" 2>/dev/null || true
cp "$WIN"/user/input_config/*.ini "$D/user/input_config/"
for r in README_RUS.txt README_ENG.txt; do
    [ -f "$WIN/$r" ] && install -m644 "$WIN/$r" "$D/"
done

# sources (GPL-2.0): the emulator binary is built from HajDEO/nhl24pc + this patch
BASE=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
install -m644 "$HERE/nhl24pc-v3.2-repack.patch" "$D/source/"
[ -f "$SRC/LICENSE" ] && install -m644 "$SRC/LICENSE" "$D/source/LICENSE"
cat > "$D/source/SOURCE.txt" <<EOF
nhl24-linux is built from:
  https://github.com/HajDEO/nhl24pc  commit $BASE
  + nhl24pc-v3.2-repack.patch (emulator changes of the NHL24PC V3.2 repack)

  git clone --recursive https://github.com/HajDEO/nhl24pc.git
  cd nhl24pc && git checkout $BASE && git submodule update --init --recursive
  git apply /path/to/nhl24pc-v3.2-repack.patch
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
  cmake --build build

License: GPL-2.0-or-later (see LICENSE).
EOF

# what the binary needs on the target system
GLIBC=$(objdump -T "$D/nhl24-linux" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)
echo "Emulator needs ${GLIBC:-?} or newer and these libraries:"
ldd "$D/nhl24-linux" | awk '{print "   " $1}' | grep -v -e linux-vdso -e ld-linux

tar -C "$TMP" -czf "$OUT" "$NAME"
echo
echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
tar -tzf "$OUT" | sed 's/^/   /'
