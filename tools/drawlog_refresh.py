"""Correlate image refresh / invalidation events with the UI pass in traced frames.

    python tools/drawlog_refresh.py <drawlog_N.txt> [bad gpu frames, comma separated]

For every frame: the draw index range of the UI pass (draws whose colour target is the 1920x1080
R8G8B8A8Unorm image), the refresh / cpu-inval / gpu-inval events with their draw index, and
whether an event touched the UI target (or any render target) inside the UI pass. Then a summary
comparing the listed bad frames with the others.
"""
import re
import sys
from collections import defaultdict, Counter

DL = re.compile(r'^dl f(\d+) (\w+) p=(0x[0-9a-f]+) clr=(0x[0-9a-f]+)(.*)$')
ENTRY = re.compile(r' (rt\d|db|b)=\[id (\d+) (0x[0-9a-f]+) ([^\]]*)\]')
EV = re.compile(r'^(refresh|cpu-inval|gpu-inval) f(\d+) d(\d+) (.*)$')


def main():
    path = sys.argv[1]
    bad = {int(x) for x in sys.argv[2].split(',')} if len(sys.argv) > 2 else set()
    draws = defaultdict(list)
    events = defaultdict(list)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = DL.match(line)
            if m:
                fr, kind, pipe, clr, rest = m.groups()
                ents = ENTRY.findall(rest)
                ui = any('R8G8B8A8Unorm 1920x1080' in body for t, i, a, body in ents if t != 'b')
                draws[int(fr)].append((kind, int(clr, 16), ui))
                continue
            m = EV.match(line)
            if m:
                events[int(m.group(2))].append((m.group(1), int(m.group(3)), m.group(4)))
    frames = sorted(draws)
    # the global draw index is cumulative; map to a per-frame index using the first draw's index
    # (events carry the cumulative index; draws are logged after the event of the same draw)
    cum = 0
    base = {}
    for fr in frames:
        base[fr] = cum
        cum += len(draws[fr])
    summary = Counter()
    for fr in frames:
        ds = draws[fr]
        ui_idx = [i for i, d in enumerate(ds) if d[2]]
        ui_start, ui_end = (ui_idx[0], ui_idx[-1]) if ui_idx else (None, None)
        evs = events.get(fr, [])
        in_ui = []
        for kind, d, rest in evs:
            local = d - base[fr]
            if ui_start is not None and ui_start < local <= ui_end + 1:
                in_ui.append((kind, local, rest))
        tag = 'BAD ' if fr in bad else 'good'
        key = (tag, 'refresh-in-ui' if any(k == 'refresh' and ('1920x1080' in r) for k, l, r in in_ui) else ('inval-in-ui' if in_ui else 'none'))
        summary[key] += 1
        if fr in bad or (in_ui and any('1920x1080' in r for k, l, r in in_ui)):
            print('=== %s frame %d: %d draws, UI %s..%s, events %d (in UI pass: %d) ===' % (tag, fr, len(ds), ui_start, ui_end, len(evs), len(in_ui)))
            for kind, d, rest in evs[:40]:
                local = d - base[fr]
                mark = '  <-- in UI pass' if (ui_start is not None and ui_start < local <= ui_end + 1) else ''
                print('   d%-4d %-9s %s%s' % (local, kind, rest[:170], mark))
    print()
    print('summary (tag, event class):', sorted(summary.items()))


if __name__ == '__main__':
    main()
