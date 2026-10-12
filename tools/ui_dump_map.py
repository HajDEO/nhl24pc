"""ASCII map of a raw RGBA8 image dump (captures/img_<addr>_<w>x<h>_<fmt>_f<n>.raw): one character
per block, '#' = block is >= 90 % opaque black (a >= 250, rgb < 10), 'o' = >= 50 % opaque other
content, '+' = >= 50 % translucent content (0 < a < 250), '.' = transparent. Also prints the widest
full-height run of opaque-black columns (the "black stripe" test) and alpha statistics.

  python -I tools/ui_dump_map.py <raw> [--cols 96]
"""
import re, sys


def main():
    argv = sys.argv[1:]
    args = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or argv[i - 1] != "--cols")]
    cols = 96
    if '--cols' in sys.argv:
        cols = int(sys.argv[sys.argv.index('--cols') + 1])
    for path in args:
        m = re.search(r'_(\d+)x(\d+)_', path)
        w, h = int(m.group(1)), int(m.group(2))
        data = open(path, 'rb').read()
        if len(data) < w * h * 4:
            print(path, 'not RGBA8 (size %d, expected %d)' % (len(data), w * h * 4))
            continue
        bw = max(1, w // cols)
        bh = bw
        rows = (h + bh - 1) // bh
        cols_n = (w + bw - 1) // bw
        black = [[0] * cols_n for _ in range(rows)]
        opaque = [[0] * cols_n for _ in range(rows)]
        trans = [[0] * cols_n for _ in range(rows)]
        total = [[0] * cols_n for _ in range(rows)]
        a0 = a255 = amid = 0
        col_black = [0] * w
        step = 2
        for y in range(0, h, step):
            row = data[y * w * 4:(y + 1) * w * 4]
            by = y // bh
            for x in range(w):
                r, g, b, a = row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]
                bx = x // bw
                total[by][bx] += 1
                if a == 0:
                    a0 += 1
                    continue
                if a >= 250:
                    a255 += 1
                    opaque[by][bx] += 1
                    if r < 10 and g < 10 and b < 10:
                        black[by][bx] += 1
                        col_black[x] += 1
                else:
                    amid += 1
                    trans[by][bx] += 1
        n = a0 + a255 + amid
        print('%s: %dx%d  alpha: 0 = %.1f %%, 255 = %.1f %%, other = %.1f %%' % (
            path, w, h, 100.0 * a0 / n, 100.0 * a255 / n, 100.0 * amid / n))
        rows_sampled = len(range(0, h, step))
        best = (0, -1)
        cur = -1
        for x in range(w + 1):
            isb = x < w and col_black[x] >= 0.85 * rows_sampled
            if isb and cur < 0:
                cur = x
            elif not isb and cur >= 0:
                if x - cur > best[0]:
                    best = (x - cur, cur)
                cur = -1
        if best[0]:
            print('  widest full-height opaque-black column run: x %d..%d (%d px)' % (best[1], best[1] + best[0] - 1, best[0]))
        else:
            print('  no full-height opaque-black column run')
        print('  block map (%dx%d px per char):' % (bw, bh))
        for by in range(rows):
            line = ''
            for bx in range(cols_n):
                t = total[by][bx] or 1
                if black[by][bx] >= 0.9 * t:
                    line += '#'
                elif opaque[by][bx] >= 0.5 * t:
                    line += 'o'
                elif trans[by][bx] >= 0.5 * t:
                    line += '+'
                else:
                    line += '.'
            print('  %4d %s' % (by * bh, line))


main()
