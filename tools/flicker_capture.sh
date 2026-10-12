#!/usr/bin/env bash
# Capture a 600-frame trace (+ full draw log) and a burst of timestamped screenshots, then flag
# one-frame flickers: a screenshot that differs from both neighbours while the neighbours match.
#
#   bash tools/flicker_capture.sh <out dir name> [shots] [interval ms]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:-burst}"; SHOTS="${2:-200}"; IV="${3:-35}"; DRAW="${4:-600}"
L="$ROOT/build/user/log/shad_log.txt"
PY="$LOCALAPPDATA/Programs/Python/Python312/python.exe"
before=$(grep -c 'frame trace: logging' "$L")
echo "$DRAW" > "$ROOT/build/user/captures/frametrace"
for i in $(seq 1 30); do [ "$(grep -c 'frame trace: logging' "$L")" -gt "$before" ] && break; sleep 0.3; done
powershell -NoProfile -ExecutionPolicy Bypass -File "$ROOT/tools/burst_shots.ps1" -Count "$SHOTS" -IntervalMs "$IV" -Out "logs/$NAME" | tail -1
for i in $(seq 1 60); do [ "$(grep -c 'draw log written' "$L")" -ge "$((before + 1))" ] && break; sleep 1; done
sleep 3
"$PY" - "$L" "$ROOT/logs/$NAME" <<'EOF'
import glob, os, re, sys
from PIL import Image, ImageChops, ImageStat
L, d = sys.argv[1], sys.argv[2]
FT = re.compile(r'frame trace #(\d+) t=(\d+) dt=([\d.-]+) draws (\d+) .* img \+(\d+) .*expand (\d+) overlap .* gpuframe (\d+)')
flips = []
for line in open(L, encoding='utf-8', errors='replace'):
    m = FT.search(line)
    if m: flips.append(dict(no=int(m.group(1)), t=int(m.group(2)), dt=float(m.group(3)), draws=int(m.group(4)), created=int(m.group(5)), expand=int(m.group(6)), gf=int(m.group(7))))
flips = flips[-600:]
rows = [tuple(int(x) for x in r.strip().split(',')[:3]) for r in open(os.path.join(d, 'index.csv')) if r.strip()]
ims = []
for i, t0, t1 in rows:
    f = glob.glob(os.path.join(d, '*_%03d.png' % i))[0]
    ims.append((i, t0, Image.open(f).convert('L').crop((95, 80, 1375, 800)).resize((320, 180))))
def diff(a, b): return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]
print('trace gpu frames %d..%d, t %d..%d; shots t %d..%d' % (flips[0]['gf'], flips[-1]['gf'], flips[0]['t'], flips[-1]['t'], rows[0][1], rows[-1][1]))
cands = []
for j in range(1, len(ims) - 1):
    i, t0, im = ims[j]
    d1 = diff(im, ims[j-1][2]); d2 = diff(im, ims[j+1][2]); d3 = diff(ims[j-1][2], ims[j+1][2])
    if d1 > 5 and d2 > 5 and d3 < min(d1, d2) * 0.5:
        before = [x for x in flips if x['t'] <= t0]
        last = before[-1] if before else None
        cands.append((i, round(d1, 1), round(d2, 1), round(d3, 1), last['gf'] if last else None, last['no'] if last else None, (t0 - last['t']) if last else None))
print('flicker candidates (shot, d_prev, d_next, d_neighbours, gpuframe, trace#, ms since flip):')
for c in cands: print('  ', c)
open(os.path.join(d, 'candidates.txt'), 'w').write(' '.join(str(c[4]) for c in cands if c[4]))
# overall motion level to tell cutscene (camera moving) from static menu
ds = [diff(ims[j][2], ims[j-1][2]) for j in range(1, len(ims))]
print('mean shot-to-shot difference %.1f (max %.1f)' % (sum(ds)/len(ds), max(ds)))
EOF
