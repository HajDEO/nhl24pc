#!/usr/bin/env bash
# Compatibility wrapper for the native Linux launcher. Wine is not required.
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
exec python3 "$HERE/nhl24-launcher.py" "$@"
