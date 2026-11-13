#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BIN=${1:-"$REPO/build/nhl24"}
if [ -d "$BIN" ]; then
    BIN="$BIN/nhl24"
fi
VERSION=$(sed -n 's/^#define NHLPORT_VERSION "\([^"]*\)"/\1/p' "$REPO/src/common/port_version.h")
[ -n "$VERSION" ] || { echo "could not read NHLPORT_VERSION" >&2; exit 1; }
OUT=${2:-"$PWD/NHL24-PC-v${VERSION}-x86_64.AppImage"}
case $OUT in
    /*) ;;
    *) OUT=$PWD/$OUT ;;
esac

PYTHON=${APPIMAGE_PYTHON:-/usr/bin/python3}
[ -x "$BIN" ] || { echo "no executable at $BIN" >&2; exit 1; }
[ -x "$PYTHON" ] || { echo "no Python interpreter at $PYTHON" >&2; exit 1; }
"$PYTHON" -c 'import _tkinter, tkinter, zlib' >/dev/null 2>&1 || {
    echo "Python Tk support is required (python3-tk on Debian/Ubuntu)" >&2
    exit 1
}

PYTHON=$(readlink -f "$PYTHON")
PY_STDLIB=$("$PYTHON" -c 'import sysconfig; print(sysconfig.get_path("stdlib"))')
TKINTER_PACKAGE=$("$PYTHON" -c 'import os, tkinter; print(os.path.dirname(tkinter.__file__))')
TKINTER_EXTENSION=$("$PYTHON" -c 'import _tkinter; print(_tkinter.__file__)')
TCL_DIR=${APPIMAGE_TCL_DIR:-$("$PYTHON" -c 'import tkinter; print(tkinter.Tcl().eval("info library"))')}
TK_DIR=${APPIMAGE_TK_DIR:-$(find "$(dirname "$TCL_DIR")" -mindepth 1 -maxdepth 1 -type d -name 'tk[0-9]*' -print -quit)}
[ -d "$PY_STDLIB" ] || { echo "Python standard library not found" >&2; exit 1; }
[ -d "$TKINTER_PACKAGE" ] || { echo "Python Tk package not found" >&2; exit 1; }
[ -f "$TKINTER_EXTENSION" ] || { echo "Python _tkinter extension not found" >&2; exit 1; }
[ -d "$TCL_DIR" ] || { echo "Tcl library not found" >&2; exit 1; }
[ -n "$TK_DIR" ] && [ -d "$TK_DIR" ] || { echo "Tk library not found" >&2; exit 1; }

LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
APPDIR=$TMP/NHL24-PC.AppDir
TOOLS=$TMP/tools
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/nhl24pc" "$APPDIR/usr/share/nhl24pc/input_config" \
    "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
    "$APPDIR/usr/share/metainfo" "$APPDIR/usr/share/doc/nhl24pc" "$TOOLS"

install -m755 "$BIN" "$APPDIR/usr/bin/nhl24"
install -m755 "$HERE/nhl24-linux.py" "$HERE/nhl24-launcher.py" "$APPDIR/usr/lib/nhl24pc/"
install -m755 "$HERE/appimage/AppRun" "$APPDIR/AppRun"
install -m644 "$REPO/release/user/config.json" "$APPDIR/usr/share/nhl24pc/config_default.json"
install -m644 "$REPO/release/user/input_config/default.ini" \
    "$REPO/release/user/input_config/global.ini" "$APPDIR/usr/share/nhl24pc/input_config/"
install -m644 "$REPO/game_patches/nhl24.xml" "$APPDIR/usr/share/nhl24pc/nhl24.xml"
install -m644 "$REPO/src/resources/nhl24.png" "$APPDIR/usr/share/nhl24pc/nhl24-pc.png"
install -m644 "$HERE/appimage/nhl24-pc.desktop" "$APPDIR/nhl24-pc.desktop"
install -m644 "$HERE/appimage/nhl24-pc.desktop" "$APPDIR/usr/share/applications/nhl24-pc.desktop"
install -m644 "$REPO/src/resources/nhl24.png" "$APPDIR/nhl24-pc.png"
install -m644 "$REPO/src/resources/nhl24.png" \
    "$APPDIR/usr/share/icons/hicolor/256x256/apps/nhl24-pc.png"
install -m644 "$HERE/appimage/nhl24-pc.appdata.xml" \
    "$APPDIR/usr/share/metainfo/nhl24-pc.appdata.xml"
install -m644 "$HERE/LINUX.md" "$HERE/LINUX_RU.md" "$REPO/LICENSE" \
    "$APPDIR/usr/share/doc/nhl24pc/"
install -m644 "$HERE/LINUX.md" "$HERE/LINUX_RU.md" "$APPDIR/usr/share/nhl24pc/"
printf '%s\n' "$VERSION" > "$APPDIR/usr/lib/nhl24pc/VERSION"
COMMIT=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)
SOURCE_REPOSITORY=${NHL24_SOURCE_URL:-}
if [ -z "$SOURCE_REPOSITORY" ]; then
    SOURCE_REPOSITORY=$(git -C "$REPO" remote get-url origin 2>/dev/null || true)
fi
SOURCE_REPOSITORY=${SOURCE_REPOSITORY:-https://github.com/HajDEO/nhl24pc}
GLIBC=unknown
GLIBCXX=unknown
if command -v objdump >/dev/null 2>&1; then
    GLIBC=$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sort -Vu | tail -1 || true)
    GLIBCXX=$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBCXX_[0-9][0-9.]*' | sort -Vu | tail -1 || true)
    GLIBC=${GLIBC:-unknown}
    GLIBCXX=${GLIBCXX:-unknown}
fi
{
    echo "NHL24 PC AppImage v$VERSION"
    echo "Source: $SOURCE_REPOSITORY"
    echo "Commit: $COMMIT"
    echo "Required glibc symbol version: $GLIBC"
    echo "Required libstdc++ symbol version: $GLIBCXX"
} > "$APPDIR/usr/share/nhl24pc/BUILD-INFO.txt"
install -m644 "$APPDIR/usr/share/nhl24pc/BUILD-INFO.txt" "$APPDIR/usr/share/doc/nhl24pc/"
ln -s nhl24-pc.png "$APPDIR/.DirIcon"
ln -s ../../AppRun "$APPDIR/usr/bin/nhl24-pc"

install -m755 "$PYTHON" "$APPDIR/usr/bin/python3"
cp -a "$PY_STDLIB" "$APPDIR/usr/lib/"
PY_BASENAME=$(basename "$PY_STDLIB")
rm -rf "$APPDIR/usr/lib/$PY_BASENAME/tkinter"
cp -a "$TKINTER_PACKAGE" "$APPDIR/usr/lib/$PY_BASENAME/tkinter"
mkdir -p "$APPDIR/usr/lib/$PY_BASENAME/lib-dynload"
install -m644 "$TKINTER_EXTENSION" "$APPDIR/usr/lib/$PY_BASENAME/lib-dynload/"
find "$APPDIR/usr/lib/$PY_BASENAME" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$APPDIR/usr/lib/$PY_BASENAME" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
find "$APPDIR/usr/lib/$PY_BASENAME" -maxdepth 1 -type d \
    \( -name 'config-*' -o -name test -o -name tests -o -name idlelib -o -name ensurepip \
       -o -name venv -o -name lib2to3 \) -exec rm -rf {} +
mkdir -p "$APPDIR/usr/share/tcltk"
cp -a "$TCL_DIR" "$TK_DIR" "$APPDIR/usr/share/tcltk/"
ln -s "$(basename "$TCL_DIR")" "$APPDIR/usr/share/tcltk/tcl"
ln -s "$(basename "$TK_DIR")" "$APPDIR/usr/share/tcltk/tk"

for copyright_file in /usr/share/doc/python3*/copyright /usr/share/doc/tcl*/copyright \
                      /usr/share/doc/tk*/copyright; do
    if [ -f "$copyright_file" ]; then
        package_name=$(basename "$(dirname "$copyright_file")")
        install -m644 "$copyright_file" "$APPDIR/usr/share/doc/nhl24pc/${package_name}-copyright"
    fi
done

download_tool() {
    url=$1
    expected=$2
    destination=$3
    curl -fL --retry 3 --retry-delay 2 "$url" -o "$destination"
    printf '%s  %s\n' "$expected" "$destination" | sha256sum -c -
    chmod +x "$destination"
}

if [ -n "${LINUXDEPLOY:-}" ]; then
    LINUXDEPLOY_BIN=$LINUXDEPLOY
else
    LINUXDEPLOY_BIN=$TOOLS/linuxdeploy-x86_64.AppImage
    download_tool "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256" "$LINUXDEPLOY_BIN"
fi
if [ -n "${APPIMAGETOOL:-}" ]; then
    APPIMAGETOOL_BIN=$APPIMAGETOOL
else
    APPIMAGETOOL_BIN=$TOOLS/appimagetool-x86_64.AppImage
    download_tool "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$APPIMAGETOOL_BIN"
fi
if [ -n "${APPIMAGE_RUNTIME:-}" ]; then
    RUNTIME_BIN=$APPIMAGE_RUNTIME
else
    RUNTIME_BIN=$TOOLS/runtime-x86_64
    download_tool "$RUNTIME_URL" "$RUNTIME_SHA256" "$RUNTIME_BIN"
fi

deploy_args=(--appdir "$APPDIR" --executable "$APPDIR/usr/bin/nhl24"
             --executable "$APPDIR/usr/bin/python3"
             --desktop-file "$APPDIR/usr/share/applications/nhl24-pc.desktop"
             --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/nhl24-pc.png")

APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY_BIN" "${deploy_args[@]}"
mkdir -p "$(dirname "$OUT")"
ARCH=x86_64 VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGETOOL_BIN" --runtime-file "$RUNTIME_BIN" "$APPDIR" "$OUT"
chmod +x "$OUT"

echo
echo "Done: $OUT ($(du -h --apparent-size "$OUT" | cut -f1))"
