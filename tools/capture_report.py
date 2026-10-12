"""Report of an F1 / captures/capture record: marker lines of the diag log, the frames of the newest
draw log with UI-target draws, CMASK clears and the composite dispatch, the dumped images (converted
to PNG with raw2png.py) and an ASCII map of the UI layer dump.

  python -I tools/capture_report.py [--captures <dir>] [--diag <nhl24_diag.log>] [--drawlog <file>] [--all-draws]
"""
import glob, os, re, subprocess, sys

COMPOSITES = ('0x2c569595', '0x5bb1bf02')  # UI composite: plain, and scaled (dynamic-resolution scene)
MASK = '0x1e09202e'


def arg(name, default):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
captures = arg('--captures', os.path.join(base, 'build', 'user', 'captures'))
diag = arg('--diag', os.path.join(base, 'build', 'user', 'log', 'nhl24_diag.log'))
all_draws = '--all-draws' in sys.argv

print('== diag log markers / captures')
if os.path.exists(diag):
    for line in open(diag, encoding='utf-8', errors='replace'):
        if 'marker' in line or 'capture' in line or 'frame trace' in line:
            print('  ' + line.rstrip()[:160])
else:
    print('  (no diag log)')

logs = sorted(glob.glob(os.path.join(captures, 'drawlog_*.txt')), key=os.path.getmtime)
drawlog = arg('--drawlog', logs[-1] if logs else '')
print('== draw logs:', ', '.join(os.path.basename(l) for l in logs) or '(none)')
if not drawlog or not os.path.exists(drawlog):
    print('  no draw log yet')
    sys.exit(0)
print('== analysing', os.path.basename(drawlog))
lines = open(drawlog, encoding='utf-8', errors='replace').read().split('\n')

# UI target = the R8G8B8A8 image sampled by the composite
ui_addr = None
ui_cmask = None
composite_frames = {}
for l in lines:
    if l.startswith('dl ') and ' disp ' in l and any('hash=' + c in l for c in COMPOSITES):
        f = int(l.split()[1][1:])
        composite_frames.setdefault(f, 0)
        composite_frames[f] += 1
        if ui_addr is None:
            for m in re.finditer(r'b=\[id \d+ (0x[0-9a-f]+) (R8G8B8A8\w+) (\d+)x(\d+)', l):
                ui_addr = m.group(1)
                break
if ui_addr:
    for l in lines:
        if l.startswith('dl ') and (' draw ' in l or ' drawi ' in l) and 'rt0=[id' in l and ui_addr in l:
            m = re.search(r'rt0=\[id \d+ ' + ui_addr + r'[^\]]*cm=(0x[0-9a-f]+)', l)
            if m:
                ui_cmask = m.group(1)
                break
print('  UI target', ui_addr, 'CMASK', ui_cmask)

frames = {}
def fr(f):
    return frames.setdefault(f, {'draws': 0, 'ui_draws': [], 'cs_clear': 0, 'alpha0_clear': 0, 'composite': 0, 'mask': 0, 'dump': 0, 'disp': 0})

for l in lines:
    parts = l.split()
    if len(parts) < 2 or not parts[1].startswith('f') or not parts[1][1:].isdigit():
        continue
    f = int(parts[1][1:])
    d = fr(f)
    if parts[0] == 'dl':
        if parts[2] == 'draw' or parts[2] == 'drawi':
            d['draws'] += 1
            if ui_addr and ('rt0=[id' in l) and (' ' + ui_addr + ' ' in l):
                d['ui_draws'].append(l)
        elif parts[2] == 'disp':
            d['disp'] += 1
            if any('hash=' + c in l for c in COMPOSITES):
                d['composite'] += 1
            if 'hash=' + MASK in l:
                d['mask'] += 1
    elif parts[0] == 'meta-cs-clear' and ui_cmask and parts[3] == ui_cmask:
        d['cs_clear'] += 1
    elif parts[0] == 'meta-clear-shader-read' and ui_cmask and parts[3] == ui_cmask:
        d['alpha0_clear'] += 1
    elif parts[0] == 'dump-cs':
        d['dump'] += 1

print('  frame  draws  ui-draws  cmask-cs-clear  alpha0-clear  mask  composite  dump')
for f in sorted(frames):
    d = frames[f]
    print('  %5d  %5d  %8d  %14d  %12d  %4d  %9d  %4d' % (f, d['draws'], len(d['ui_draws']), d['cs_clear'], d['alpha0_clear'], d['mask'], d['composite'], d['dump']))

dump_frames = [f for f in frames if frames[f]['dump']]
show = dump_frames or ([max(frames, key=lambda f: frames[f]['draws'])] if frames else [])
for f in show:
    d = frames[f]
    print('== UI draws in frame %d (%d): viewport, scissor, ps hash, blend, textures' % (f, len(d['ui_draws'])))
    seen = {}
    for l in d['ui_draws']:
        vp = re.search(r'vp=\[([^\]]*)\]', l)
        sc = re.search(r'sc=\[([^\]]*)\]', l)
        ps = re.search(r'ps=(0x[0-9a-f]+)', l)
        bl = re.search(r'bl=\[([^\]]*)\]', l)
        n = re.search(r' n=(\d+)', l)
        texs = re.findall(r't=\[(0x[0-9a-f]+) (\d+x\d+)', l)
        key = (vp.group(1) if vp else '', sc.group(1) if sc else '', ps.group(1) if ps else '', bl.group(1) if bl else '', tuple(texs))
        if all_draws:
            print('   vp=[%s] sc=[%s] n=%s ps=%s bl=[%s] tex=%s' % (key[0], key[1], n.group(1) if n else '?', key[2], key[3], ' '.join('%s@%s' % (t[1], t[0]) for t in texs)))
        else:
            seen[key] = seen.get(key, 0) + 1
    if not all_draws:
        for key, cnt in seen.items():
            print('   x%-3d vp=[%s] sc=[%s] ps=%s bl=[%s] tex=%s' % (cnt, key[0], key[1], key[2], key[3], ' '.join('%s@%s' % (t[1], t[0]) for t in key[4])))

# nhlport trace lines added for the tile-loss investigation
for f in show:
    print('== trace events of frame %d (clobber / buf-inval / readback / inline / dispi-args):' % f)
    n = 0
    for l in lines:
        parts = l.split()
        if len(parts) > 1 and parts[0] in ('clobber', 'readback', 'inline', 'dispi-args', 'gds') and parts[1] == 'f%d' % f:
            print('   ' + l[:220])
            n += 1
            if n >= 80:
                print('   ...')
                break

raws = sorted(glob.glob(os.path.join(captures, 'img_*.raw')), key=os.path.getmtime)
print('== image dumps:', len(raws))
for r in raws[-12:]:
    print('  ', os.path.basename(r), os.path.getsize(r))
    png = r + '.png'
    if not os.path.exists(png):
        subprocess.run([sys.executable, '-I', os.path.join(base, 'tools', 'raw2png.py'), r], check=False)
for r in raws[-12:]:
    if 'R8G8B8A8' in r:
        subprocess.run([sys.executable, '-I', os.path.join(base, 'tools', 'ui_dump_map.py'), r, '--cols', '96'], check=False)
