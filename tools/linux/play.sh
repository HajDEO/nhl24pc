#!/bin/bash
# Starts NHL 24. The game is looked up in game/eboot.bin next to this script
# (or the path written in user/game_path.txt).
cd "$(dirname "$(readlink -f "$0")")" || exit 1
exec ./nhl24.AppImage "$@"
