"""Find the passes around the black band in an in-game draw log: the UI composite dispatch, the
CMASK mask dispatch, the draws into the UI target (viewport / scissor envelope) and every image
of a given size that is bound in the frame.

  python tools/band_passes.py <drawlog> [gpu frame] [--size 2048x1024] [--ui-hash 0x2c569595]
"""
import re, sys
from collections import defaultdict

argv = sys.argv[1:]
path = argv[0]
want = int(argv[1]) if len(argv) > 1 and argv[1].isdigit() else None
size_filter = '2048x1024'
ui_hash = '0x2c569595'
mask_hash = '0x1e09202e'
for i, a in enumerate(argv):
    if a == '--size':
        size_filter = argv[i + 1]
    if a == '--ui-hash':
        ui_hash = argv[i + 1]

frames = defaultdict(list)
order = []
with open(path, encoding='utf-8', errors='replace') as f:
    for line in f:
        m = re.match(r'dl f(\d+) (\w+) ', line)
        if not m:
            continue
        fr = int(m.group(1))
        if fr not in frames:
            order.append(fr)
        frames[fr].append(line.rstrip('\n'))
if want is None:
    want = order[-2] if len(order) > 1 else order[-1]
lines = frames[want]
print('frame', want, 'entries', len(lines), '(frames in log: %s)' % ', '.join(str(o) for o in order))

rt_re = re.compile(r' rt(\d)=\[id (\d+) (0x[0-9a-f]+) (\w+) (\d+)x(\d+) m(\d+) l(\d+) cm=(0x[0-9a-f]+):(\w)\]')
db_re = re.compile(r' db=\[id (\d+) (0x[0-9a-f]+) (\w+) (\d+)x(\d+)\]')
b_re = re.compile(r' b=\[id (\d+) (0x[0-9a-f]+) (\w+) (\d+)x(\d+) m(\d+) l(\d+) d(\d)\]')
t_re = re.compile(r' t=\[(0x[0-9a-f]+) (\d+)x(\d+) p(\d+) f(\d+)( w)?\]')
vp_re = re.compile(r' vp=\[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\] sc=\[(\d+) (\d+) (\d+) (\d+)\]')
vsc_re = re.compile(r' vsc=\[(\d+) (\d+) (\d+) (\d+)\] gsc=\[(\d+) (\d+) (\d+) (\d+)\] wsc=\[(\d+) (\d+) (\d+) (\d+)\]')

# 1. images of the filtered size seen in the frame (as targets or bindings)
seen = {}
for idx, l in enumerate(lines):
    for m in rt_re.finditer(l):
        key = (m.group(3), m.group(4), m.group(5) + 'x' + m.group(6))
        seen.setdefault(key, []).append(('rt', idx))
    for m in db_re.finditer(l):
        key = (m.group(2), m.group(3), m.group(4) + 'x' + m.group(5))
        seen.setdefault(key, []).append(('db', idx))
    for m in b_re.finditer(l):
        key = (m.group(2), m.group(3), m.group(4) + 'x' + m.group(5))
        seen.setdefault(key, []).append(('b', idx))
print('\n== images of size', size_filter)
for key, uses in sorted(seen.items()):
    if key[2] == size_filter:
        kinds = defaultdict(int)
        for k, _ in uses:
            kinds[k] += 1
        first = min(i for _, i in uses)
        last = max(i for _, i in uses)
        print('  %s %s %s uses %s entries %d..%d' % (key[0], key[1], key[2], dict(kinds), first, last))

# 2. all distinct render target sizes with counts
sizes = defaultdict(int)
for l in lines:
    for m in rt_re.finditer(l):
        sizes[(m.group(4), m.group(5) + 'x' + m.group(6))] += 1
print('\n== render target formats/sizes (draw+dispatch entries)')
for k, n in sorted(sizes.items(), key=lambda kv: -kv[1])[:30]:
    print('  %-28s %-10s %d' % (k[0], k[1], n))

# 3. composite / mask dispatches
print('\n== dispatches with hash', ui_hash, 'or', mask_hash)
for idx, l in enumerate(lines):
    if ' disp ' not in l:
        continue
    if ('hash=' + ui_hash) in l or ('hash=' + mask_hash) in l:
        head = l.split(' cb=')[0].split(' rt0=')[0]
        print('  [%d] %s' % (idx, head[:200]))
        for m in t_re.finditer(l):
            print('      t %s %sx%s pitch %s fmt %s%s' % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(6) or ''))
        for m in b_re.finditer(l):
            print('      b id %s %s %s %sx%s m%s l%s d%s' % m.groups())
        for m in re.finditer(r' cb=\[(0x[0-9a-f]+) (\d+) ([rw])(?::([0-9a-f ]+))?\]', l):
            print('      cb %s size %s %s %s' % (m.group(1), m.group(2), m.group(3), (m.group(4) or '')[:120]))

# 4. draws into R8G8B8A8Unorm 1920x1080 targets: viewport / scissor envelope per target address
print('\n== draws per colour target (viewport/scissor envelope)')
env = {}
for idx, l in enumerate(lines):
    if not re.match(r'dl f\d+ draw', l):
        continue
    m = rt_re.search(l)
    if not m:
        continue
    key = (m.group(3), m.group(4), m.group(5) + 'x' + m.group(6))
    v = vp_re.search(l)
    s = vsc_re.search(l)
    e = env.setdefault(key, {'n': 0, 'first': idx, 'last': idx, 'vp': set(), 'sc': set(), 'vsc': set(), 'wsc': set()})
    e['n'] += 1
    e['last'] = idx
    if v:
        e['vp'].add(tuple(v.groups()[:4]))
        e['sc'].add(tuple(v.groups()[4:]))
    if s:
        e['vsc'].add(tuple(s.groups()[:4]))
        e['wsc'].add(tuple(s.groups()[8:]))
for key, e in sorted(env.items(), key=lambda kv: kv[1]['first']):
    print('  %s %s %s: %d draws, entries %d..%d' % (key[0], key[1], key[2], e['n'], e['first'], e['last']))
    for name in ('vp', 'sc', 'vsc', 'wsc'):
        vals = sorted(e[name])
        print('      %s: %s%s' % (name, ' '.join('[' + ' '.join(v) + ']' for v in vals[:8]), ' ...(%d)' % len(vals) if len(vals) > 8 else ''))
