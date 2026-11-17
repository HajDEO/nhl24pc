#!/bin/bash
# Extracts an NHL 24 PKG (base game first, then the optional 1.60 update) into game/.
#   ./install_pkg.sh /path/to/NHL24-base.pkg
#   ./install_pkg.sh /path/to/NHL24-update-1.60.pkg
cd "$(dirname "$(readlink -f "$0")")" || exit 1
if [ -z "$1" ] || [ ! -f "$1" ]; then
    echo "usage: $0 /path/to/package.pkg"
    exit 1
fi
mkdir -p game
./nhl24.AppImage --extract-pkg "$(readlink -f "$1")" --extract-to "$PWD/game"
