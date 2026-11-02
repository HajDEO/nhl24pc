#!/bin/bash
# NHL 24 on Linux: repack launcher under Wine for settings, native build for the game.
# Put next to game/, user/, tools/ and nhl24-linux (= build/nhl24 from HajDEO/nhl24pc).
cd "$(dirname "$(readlink -f "$0")")"
[ -x ./nhl24-linux ] || { echo "nhl24-linux not found - build it and copy here"; exit 1; }
[ -d "$HOME/.wine-nhl24" ] && export WINEPREFIX="$HOME/.wine-nhl24"
export WINEDEBUG=-all
rm -f linux_play.flag linux_env.sh

# launcher: settings, Russian text, eboot patches (needs patch_launcher.sh applied once)
wine "C:\\Program Files\\PowerShell\\7\\pwsh.exe" -NoProfile -ExecutionPolicy Bypass \
    -File "$(winepath -w "$PWD/tools/launcher.ps1")"

# window closed without PLAY
[ -f linux_play.flag ] || exit 0

[ -f linux_env.sh ] && . ./linux_env.sh
# PS4 Pro (Neo) mode crashes the native build (texture_cache.cpp:585) - force it off
sed -i 's/"neo_mode": true/"neo_mode": false/' user/config.json

mkdir -p user/log
exec ./nhl24-linux > user/log/linux_run.log 2>&1
