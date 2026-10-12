"""Analyse the nhlport per-draw trace ("dl f<frame> ..." lines, plus "tc+"/"tc-" image lifetime
lines) written while the frame trace's draw window is active.

    python tools/drawlog_analyze.py build/user/log/shad_log.txt [--frames a,b,c] [--burst dir]

Draws are grouped by GPU frame index. Each frame gets a signature made of its draw sequence
(kind, pipeline, render-target and bound-image addresses/formats/sizes); frames whose signature
differs from the most common one are printed with a diff against the typical frame. Image ids are
left out of the signature because they change whenever the cache recreates an image; a separate
report lists images that were created in a frame and sampled in the same frame (contents came from
guest memory, not from the GPU).
"""
import difflib
import os
import re
import sys
from collections import Counter, defaultdict

DL = re.compile(r'dl f(\d+) (\w+) p=(0x[0-9a-f]+)(.*)$')
ENTRY = re.compile(r' (rt\d|db|b)=\[([^\]]*)\]')
TC = re.compile(r'tc([+-]) f(\d+) id (\d+) (.*)$')
FT = re.compile(r'frame trace #(\d+) t=(\d+) dt=([\d.-]+) .* gpuframe (\d+)')


def parse(path):
    frames = defaultdict(list)   # gpu frame -> list of draw dicts
    tc = defaultdict(list)       # gpu frame -> list of (sign, id, rest)
    flips = []                   # (trace no, unix ms, gpuframe)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = DL.search(line)
            if m:
                frame, kind, pipe, rest = m.groups()
                entries = ENTRY.findall(rest)
                rts, bound = [], []
                for tag, body in entries:
                    parts = body.split()
                    # "id N addr fmt WxH ..." -> drop the id
                    if parts[0] == 'id':
                        parts = parts[2:]
                    s = tag + ':' + ' '.join(parts)
                    (rts if tag != 'b' else bound).append(s)
                ids = [int(p.split()[1]) for t, p in entries if p.startswith('id ')]
                frames[int(frame)].append({'kind': kind, 'pipe': pipe, 'rts': rts, 'bound': bound, 'ids': ids, 'raw': line.strip()})
                continue
            m = TC.search(line)
            if m:
                sign, frame, iid, rest = m.groups()
                tc[int(frame)].append((sign, int(iid), rest.strip()))
                continue
            m = FT.search(line)
            if m:
                flips.append((int(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4))))
    return frames, tc, flips


def signature(draws):
    return tuple('%s %s | %s | %s' % (d['kind'], d['pipe'], ' '.join(d['rts']), ' '.join(d['bound'])) for d in draws)


def short(entry):
    return entry if len(entry) < 170 else entry[:167] + '...'


def main():
    args = sys.argv[1:]
    log = args[0] if args else 'build/user/log/shad_log.txt'
    want = None
    burst = None
    if '--frames' in args:
        want = {int(x) for x in args[args.index('--frames') + 1].split(',')}
    if '--burst' in args:
        burst = args[args.index('--burst') + 1]
    frames, tc, flips = parse(log)
    if not frames:
        print('no dl lines in', log)
        return
    keys = sorted(frames)
    # the first and last frames of the window are partial
    full = keys[1:-1] if len(keys) > 2 else keys
    print('%d frames with draw logs (gpu frames %d..%d), %d complete' % (len(keys), keys[0], keys[-1], len(full)))
    sigs = {k: signature(frames[k]) for k in full}
    counts = Counter(sigs.values())
    print('distinct frame signatures: %d' % len(counts))
    ranked = counts.most_common()
    typical_sig = ranked[0][0]
    typical_frame = next(k for k in full if sigs[k] == typical_sig)
    print('typical signature: %d frames (e.g. gpu frame %d, %d draws/dispatches)' % (ranked[0][1], typical_frame, len(typical_sig)))
    print()
    print('per frame: draws, images created / freed in that frame, signature rank')
    rank_of = {sig: i for i, (sig, _) in enumerate(ranked)}
    for k in full:
        created = sum(1 for s, _, _ in tc.get(k, []) if s == '+')
        freed = sum(1 for s, _, _ in tc.get(k, []) if s == '-')
        print('  f%-5d %4d draws  tc +%-3d -%-3d  sig #%d' % (k, len(frames[k]), created, freed, rank_of[sigs[k]]))
    print()
    # diff every non-typical signature against the typical one
    shown = 0
    for sig, n in ranked[1:]:
        example = next(k for k in full if sigs[k] == sig)
        members = [k for k in full if sigs[k] == sig]
        if want and not (want & set(members)):
            continue
        print('=== signature #%d: %d frame(s) %s, %d draws (typical %d) ===' % (rank_of[sig], n, members[:12], len(sig), len(typical_sig)))
        for line in difflib.unified_diff([short(x) for x in typical_sig], [short(x) for x in sig], 'typical f%d' % typical_frame, 'f%d' % example, n=1, lineterm=''):
            print('   ' + line)
        shown += 1
        if shown >= 12:
            print('... more signatures not shown')
            break
    # images created then sampled within the same frame
    print()
    print('images created in a frame and bound in that same frame (fresh content from guest memory):')
    for k in full:
        created = {iid: rest for s, iid, rest in tc.get(k, []) if s == '+'}
        if not created:
            continue
        used = Counter()
        for d in frames[k]:
            for iid in d['ids']:
                if iid in created:
                    used[iid] += 1
        if used:
            print('  f%d: %s' % (k, '; '.join('id %d x%d (%s)' % (iid, n, created[iid][:90]) for iid, n in used.most_common(8))))
    # frees in the window with reasons
    print()
    reasons = Counter()
    for k in full:
        for s, iid, rest in tc.get(k, []):
            if s == '-':
                m = re.search(r'reason (\d+)', rest)
                reasons[m.group(1) if m else '?'] += 1
    print('free reasons in window (0 pool,1 depth,2 expand,3 ltarget,4 lcopy,5 rchance,6 fail,7 gc,8 unmap,9 tile):', dict(reasons))
    # screenshots -> gpu frame
    if burst and flips:
        idx = os.path.join(burst, 'index.csv')
        if os.path.exists(idx):
            print()
            print('screenshots -> last presented trace frame / gpu frame before the capture:')
            for row in open(idx):
                parts = row.strip().split(',')
                if len(parts) < 3:
                    continue
                i, t0 = int(parts[0]), int(parts[1])
                before = [f for f in flips if f[1] <= t0]
                if before:
                    no, t, dt, gf = before[-1]
                    print('  shot %3d: trace #%d gpu frame %d (%d ms before)' % (i, no, gf, t0 - t))


if __name__ == '__main__':
    main()
