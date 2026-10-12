"""Decode the final composite dispatch of a frame from a build-40 draw log: dispatch dims,
thread-group size, user data and every constant buffer dword as hex / int / float.

  python tools/composite_consts.py <drawlog> [gpu frame]        (default: last complete frame)
  python tools/composite_consts.py <drawlog> <gpu frame> all    (every dispatch of the frame)
"""
import re, struct, sys

path = sys.argv[1]
want = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else None
show_all = len(sys.argv) > 3 and sys.argv[3] == 'all'
DISPLAY = ('0x10cf400000', '0x10d4000000', '0x10d4800000')

frames = {}
order = []
with open(path, encoding='utf-8', errors='replace') as f:
    for line in f:
        m = re.match(r'dl f(\d+) (\w+) ', line)
        if not m:
            continue
        fr = int(m.group(1))
        if fr not in frames:
            frames[fr] = []
            order.append(fr)
        frames[fr].append(line.rstrip('\n'))
if want is None:
    want = order[-2] if len(order) > 1 else order[-1]
lines = frames[want]
print('frame', want, 'entries', len(lines), '(frames in log: %d..%d)' % (order[0], order[-1]))


def decode(dwords):
    out = []
    for i, h in enumerate(dwords):
        v = int(h, 16)
        fl = struct.unpack('<f', struct.pack('<I', v))[0]
        fs = '%.4g' % fl if 1e-6 < abs(fl) < 1e6 or fl == 0 else '-'
        out.append('%2d:%08x(%d|%s)' % (i, v, v if v < 1 << 31 else v - (1 << 32), fs))
    return out


def show(line):
    head = line.split(' rt0=')[0].split(' cb=')[0]
    print(head[:260])
    for m in re.finditer(r' cb=\[(0x[0-9a-f]+) (\d+) ([rw])(?::([0-9a-f ]+))?\]', line):
        addr, size, rw, dws = m.groups()
        print('   cb %s size %s %s' % (addr, size, rw))
        if dws:
            dec = decode(dws.split())
            for j in range(0, len(dec), 4):
                print('      ' + '  '.join(dec[j:j + 4]))
    binds = re.findall(r' b=\[id (\d+) (0x[0-9a-f]+) (\w+) (\d+x\d+) m(\d+) l(\d+) d(\d)\]', line)
    for b in binds:
        print('   img id %s %s %s %s m%s l%s%s' % (b[0], b[1], b[2], b[3], b[4], b[5],
              '  <- DISPLAY BUFFER' if b[1] in DISPLAY else ''))


disps = [l for l in lines if ' disp ' in l]
if show_all:
    for l in disps:
        show(l)
        print()
else:
    comp = [l for l in disps if any(a in l for a in DISPLAY)]
    print('dispatches', len(disps), 'binding a display buffer:', len(comp))
    for l in (comp or disps[-2:]):
        show(l)
        print()
