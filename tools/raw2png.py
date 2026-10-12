"""Convert raw image dumps written by the emulator (captures/img_<addr>_<w>x<h>_<fmt>_f<n>.raw,
tightly packed mip 0) to PNG. Formats: A2B10G10R10UnormPack32, R8G8B8A8Unorm/Srgb, B8G8R8A8*,
R8Unorm, R16Sfloat (as grey), B10G11R11UfloatPack32 (tone-mapped roughly).

  python tools/raw2png.py <raw> [...]      -> <raw>.png next to it (RGBA kept for RGBA8 formats)
"""
import os, re, struct, sys
from PIL import Image


def half_to_float(h):
    s = (h >> 15) & 1; e = (h >> 10) & 0x1f; m = h & 0x3ff
    if e == 0:
        v = (m / 1024.0) * 2.0 ** -14
    elif e == 31:
        v = float('inf')
    else:
        v = (1 + m / 1024.0) * 2.0 ** (e - 15)
    return -v if s else v


def ufloat(bits, mbits, ebits):
    e = (bits >> mbits) & ((1 << ebits) - 1); m = bits & ((1 << mbits) - 1)
    if e == 0:
        return (m / float(1 << mbits)) * 2.0 ** -14
    return (1 + m / float(1 << mbits)) * 2.0 ** (e - 15)


AS = None
args = []
for i, a in enumerate(sys.argv[1:]):
    if a == '--as':
        continue
    if i > 0 and sys.argv[i] == '--as':
        AS = a; continue
    args.append(a)
for path in args:
    m = re.search(r'_(\d+)x(\d+)_([A-Za-z0-9]+)_f\d+\.raw$', os.path.basename(path))
    if not m:
        print('cannot parse size/format from', path); continue
    w, h, fmt = int(m.group(1)), int(m.group(2)), (AS or m.group(3))
    data = open(path, 'rb').read()
    out = path + '.png'
    if fmt.startswith('A2B10G10R10'):
        n = w * h
        px = struct.unpack('<%dI' % n, data[:n * 4])
        img = Image.new('RGBA', (w, h))
        img.putdata([((v & 1023) >> 2, ((v >> 10) & 1023) >> 2, ((v >> 20) & 1023) >> 2, (v >> 30) * 85) for v in px])
    elif fmt.startswith('R8G8B8A8'):
        img = Image.frombytes('RGBA', (w, h), data[:w * h * 4])
    elif fmt.startswith('B8G8R8A8'):
        img = Image.frombytes('RGBA', (w, h), data[:w * h * 4], 'raw', 'BGRA')
    elif fmt.startswith('R8Unorm') or fmt.startswith('R8Srgb'):
        img = Image.frombytes('L', (w, h), data[:w * h])
    elif fmt.startswith('R16Sfloat'):
        n = w * h
        px = struct.unpack('<%dH' % n, data[:n * 2])
        img = Image.new('L', (w, h)); img.putdata([max(0, min(255, int(half_to_float(v) * 255))) for v in px])
    elif fmt.startswith('Depth32'):
        n = w * h
        px = struct.unpack('<%df' % n, data[:n * 4])
        lo = min(px); hi = max(px)
        print('   depth range %g .. %g' % (lo, hi))
        span = (hi - lo) or 1.0
        img = Image.new('L', (w, h)); img.putdata([int((v - lo) / span * 255) for v in px])
    elif fmt.startswith('B10G11R11'):
        n = w * h
        px = struct.unpack('<%dI' % n, data[:n * 4])
        def tm(x):
            return max(0, min(255, int((x / (1 + x)) ** (1 / 2.2) * 255)))
        img = Image.new('RGB', (w, h))
        img.putdata([(tm(ufloat(v & 0x7ff, 6, 5)), tm(ufloat((v >> 11) & 0x7ff, 6, 5)), tm(ufloat(v >> 22, 5, 5))) for v in px])
    else:
        print('unsupported format', fmt, 'for', path); continue
    img.save(out)
    print('wrote', out, img.size, img.mode)
