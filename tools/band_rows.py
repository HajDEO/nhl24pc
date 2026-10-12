"""Per-row statistics of images around the black band: mean R,G,B (and A when present) for a
row range, plus the first/last row that is not black. Accepts any number of PNG paths.

  python tools/band_rows.py <png> [...] [--rows 1000-1079] [--step 2]
"""
import sys
from PIL import Image

argv = sys.argv[1:]
args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] not in ('--rows', '--step'))]
rows = (1000, 1079)
step = 2
for i, a in enumerate(sys.argv):
    if a == '--rows':
        lo, hi = sys.argv[i + 1].split('-'); rows = (int(lo), int(hi))
    if a == '--step':
        step = int(sys.argv[i + 1])
for path in args:
    im = Image.open(path)
    mode = 'RGBA' if im.mode in ('RGBA', 'LA', 'P') else 'RGB'
    im = im.convert(mode); w, h = im.size; px = im.load(); nc = len(mode)
    print('%s %dx%d %s' % (path, w, h, mode))
    def row_stats(y):
        acc = [0] * nc; mx = [0] * nc
        n = 0
        for x in range(0, w, 8):
            p = px[x, y]
            for c in range(nc):
                acc[c] += p[c]; mx[c] = max(mx[c], p[c])
            n += 1
        return [a / n for a in acc], mx
    # first / last non-black row (max channel of RGB >= 8)
    first = next((y for y in range(h) if max(row_stats(y)[1][:3]) >= 8), None)
    last = next((y for y in range(h - 1, -1, -1) if max(row_stats(y)[1][:3]) >= 8), None)
    print('   non-black rows: first %s last %s' % (first, last))
    for y in range(max(0, rows[0]), min(h - 1, rows[1]) + 1, step):
        mean, mx = row_stats(y)
        print('   y=%4d mean %s max %s' % (y, ' '.join('%6.1f' % v for v in mean), ' '.join('%3d' % v for v in mx)))
