#!/usr/bin/env bash
# Replay every RenderDoc capture in %TEMP%\RenderDoc (or the directory given as $1) through
# tools/rdc_analyze.py (qrenderdoc --python), then score the presented swapchain image: the NHL 24
# team-select flicker replaces the LEFT half of the screen with a bright image, so a capture whose
# left half is much brighter than its right half is a "bad frame" candidate.
#
#   bash tools/rdc_batch_analyze.sh            # all captures in %TEMP%/RenderDoc
#   bash tools/rdc_batch_analyze.sh dir        # captures in dir
#
# Run with the game closed: replay needs the whole 8 GB of VRAM.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${1:-$TEMP/RenderDoc}"
PY="$LOCALAPPDATA/Programs/Python/Python312/python.exe"
QRD="/c/Program Files/RenderDoc/qrenderdoc.exe"
mkdir -p "$ROOT/logs/rdc_batch"
for rdc in "$DIR"/*.rdc; do
    [ -f "$rdc" ] || continue
    name="$(basename "$rdc" .rdc)"
    out="$ROOT/logs/rdc_batch/$name"
    if [ -f "$out/analyze_log.txt" ] && grep -qE 'DONE|FAILED' "$out/analyze_log.txt"; then
        echo "skip (already analysed): $name"; continue
    fi
    rm -rf "$out"; mkdir -p "$out"
    echo "replaying $name ..."
    export RDC_CAPTURE="$(cygpath -w "$rdc")" RDC_OUT="$(cygpath -w "$out")"
    ("$QRD" --python "$(cygpath -w "$ROOT/tools/rdc_analyze.py")" > "$out/qrenderdoc_stdout.txt" 2>&1 &)
    for i in $(seq 1 240); do grep -qE 'DONE|FAILED|EXIT' "$out/analyze_log.txt" 2>/dev/null && break; sleep 3; done
    sleep 3
    powershell -NoProfile -Command "Stop-Process -Name qrenderdoc -Force -ErrorAction SilentlyContinue"
    tail -2 "$out/analyze_log.txt"
done
echo "=== scores (left-half vs right-half mean brightness of the presented image) ==="
"$PY" - "$ROOT/logs/rdc_batch" <<'EOF'
import glob, os, sys
from PIL import Image, ImageStat
for d in sorted(glob.glob(os.path.join(sys.argv[1], '*'))):
    sw = sorted(glob.glob(os.path.join(d, '*Swapchain*.png')))
    if not sw:
        print('%-50s no swapchain image' % os.path.basename(d)); continue
    im = Image.open(sw[-1]).convert('L'); w, h = im.size
    l = ImageStat.Stat(im.crop((0, 0, w // 2, h))).mean[0]
    r = ImageStat.Stat(im.crop((w // 2, 0, w, h))).mean[0]
    flag = 'BAD FRAME?' if l > r * 1.5 and l > 90 else ''
    print('%-50s left %.1f right %.1f %s' % (os.path.basename(d), l, r, flag))
EOF
