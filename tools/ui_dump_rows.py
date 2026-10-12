"""Row / column statistics of a raw RGBA8 image dump (captures/dumpimage output): per row the number
of pixels with alpha 0 / 255 / other and the number of non-black pixels, the most common pixel
values of the top region vs. the bottom rows, and the most common values of the right-edge columns.

  python tools/ui_dump_rows.py <img_..._1920x1080_R8G8B8A8Unorm_f123.raw> [--band 1026] [--rows y,y,...]
"""
import collections, re, sys

path = sys.argv[1]
m = re.search(r'_(\d+)x(\d+)_', path)
w, h = (int(m.group(1)), int(m.group(2))) if m else (1920, 1080)
band = 1026
rows = None
for i, a in enumerate(sys.argv):
    if a == '--band':
        band = int(sys.argv[i + 1])
    if a == '--rows':
        rows = [int(v) for v in sys.argv[i + 1].split(',')]
d = open(path, 'rb').read()
print('%s: %dx%d, %d bytes (expected %d)' % (path, w, h, len(d), w * h * 4))
if rows is None:
    rows = sorted(set(list(range(0, h, h // 9)) + [band - 30, band - 10, band - 2, band - 1, band, band + 1, band + 4, band + 24, h - 6, h - 5, h - 1]))


def row(y):
    o = y * w * 4
    return d[o:o + w * 4]


for y in rows:
    if not 0 <= y < h:
        continue
    r = row(y)
    a = r[3::4]
    c = collections.Counter(a)
    nonblack = sum(1 for i in range(0, len(r), 4) if max(r[i], r[i + 1], r[i + 2]) >= 8)
    print('y=%4d a0=%4d a255=%4d aother=%4d nonblack=%4d px[960]=%s px[1912]=%s' % (
        y, c.get(0, 0), c.get(255, 0), w - c.get(0, 0) - c.get(255, 0), nonblack, r[960 * 4:960 * 4 + 4].hex(), r[1912 * 4:1912 * 4 + 4].hex()))
top = collections.Counter()
bottom = collections.Counter()
for y in range(0, h, 9):
    r = row(y)
    for x in range(0, w * 4, 4 * 7):
        (top if y < band else bottom)[r[x:x + 4]] += 1
print('rows < %d most common: %s' % (band, top.most_common(6)))
print('rows >= %d most common: %s' % (band, bottom.most_common(4)))
for x in (w - 32, w - 16, w - 15, w - 8, w - 1):
    col = [d[(y * w + x) * 4:(y * w + x) * 4 + 4] for y in range(0, h, 5)]
    c = collections.Counter(col)
    print('x=%4d most common: %s' % (x, c.most_common(3)))
# first / last row with any pixel that is not the bottom-region fill value
fill = bottom.most_common(1)[0][0] if bottom else b'\x00\x00\x00\xff'
last = None
for y in range(h - 1, -1, -1):
    r = row(y)
    if any(r[i:i + 4] != fill for i in range(0, len(r), 4)):
        last = y
        break
print('fill value %s; last row that differs from it: %s' % (fill.hex(), last))
