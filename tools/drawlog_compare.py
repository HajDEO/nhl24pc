"""Compare the per-draw trace of given GPU frames against a reference frame, draw by draw.

    python tools/drawlog_compare.py <shad_log.txt> <bad_frame>[,<bad_frame>...] [--ref <frame>]

For every listed frame the draw sequence is aligned against the reference (the previous frame
with the same draw count by default) and each differing draw is printed with both versions:
kind, pipeline, targets and bound images (ids + address/format/size). Images created or freed
in the compared frame are listed first, with the request that triggered each free when known.
"""
import difflib
import re
import sys
from collections import defaultdict

DL = re.compile(r'dl f(\d+) (\w+) p=(0x[0-9a-f]+)(.*)$')
ENTRY = re.compile(r' (rt\d|db|b)=\[id (\d+) ([^\]]*)\]')
TC = re.compile(r'tc([+-]) f(\d+) id (\d+) (.*)$')


def parse(path):
    frames = defaultdict(list)
    tc = defaultdict(list)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = DL.search(line)
            if m:
                fr, kind, pipe, rest = m.groups()
                entries = ENTRY.findall(rest)
                # stable key without image ids (ids change on recreation)
                key = kind + ' ' + pipe + ' ' + ' '.join('%s:%s' % (tag, body) for tag, iid, body in entries)
                full = kind + ' ' + pipe + ' ' + ' '.join('%s:[id %s %s]' % (tag, iid, body) for tag, iid, body in entries)
                frames[int(fr)].append((key, full, [int(iid) for _, iid, _ in entries]))
                continue
            m = TC.search(line)
            if m:
                s, fr, iid, rest = m.groups()
                tc[int(fr)].append((s, int(iid), rest.strip()))
    return frames, tc


def main():
    log = sys.argv[1]
    bad = [int(x) for x in sys.argv[2].split(',')]
    ref = None
    if '--ref' in sys.argv:
        ref = int(sys.argv[sys.argv.index('--ref') + 1])
    frames, tc = parse(log)
    for b in bad:
        if b not in frames:
            print('frame %d has no draw log' % b)
            continue
        r = ref
        if r is None:
            # nearest earlier frame with no image creations and the same draw count
            for cand in range(b - 1, b - 40, -1):
                if cand in frames and not any(s == '+' for s, _, _ in tc.get(cand, [])) and abs(len(frames[cand]) - len(frames[b])) <= 2:
                    r = cand
                    break
        if r is None or r not in frames:
            print('no reference frame for %d' % b)
            continue
        print('=' * 100)
        print('frame %d (%d draws) vs reference %d (%d draws)' % (b, len(frames[b]), r, len(frames[r])))
        created = {iid: rest for s, iid, rest in tc.get(b, []) if s == '+'}
        for s, iid, rest in tc.get(b, []):
            print('  tc%s id %d %s' % (s, iid, rest[:200]))
        a = [k for k, _, _ in frames[r]]
        c = [k for k, _, _ in frames[b]]
        sm = difflib.SequenceMatcher(a=a, b=c, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal':
                continue
            print('  --- %s: ref draws %d..%d  vs  frame draws %d..%d' % (tag, i1, i2 - 1, j1, j2 - 1))
            for i in range(i1, i2):
                print('    REF  #%-3d %s' % (i, frames[r][i][1][:300]))
            for j in range(j1, j2):
                fresh = [iid for iid in frames[b][j][2] if iid in created]
                print('    BAD  #%-3d %s%s' % (j, frames[b][j][1][:300], ('   <- fresh ids %s' % fresh) if fresh else ''))
        # draws that are equal by key but use a freshly created image id
        print('  draws in frame %d that bind an image created in this frame:' % b)
        for j, (key, full, ids) in enumerate(frames[b]):
            fresh = [iid for iid in ids if iid in created]
            if fresh:
                print('    #%-3d %s  <- fresh %s' % (j, full[:220], fresh))


if __name__ == '__main__':
    main()
