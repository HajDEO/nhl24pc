"""Analyse nhlport "frame trace" log lines (one per presented frame, see videoout/driver.cpp).

    python tools/frametrace_analyze.py build/user/log/shad_log.txt [logs/burstN]

Prints per-counter statistics over the traced frames, lists the frames whose counters deviate
from the typical frame, and - when a burst directory with index.csv (from tools/burst_shots.ps1)
is given - matches each screenshot to the trace frame presented just before it and scores the
screenshot's left-half brightness band (the team-select flicker shows a bright horizontal band).
"""
import os
import re
import sys
from collections import Counter

LINE = re.compile(r'frame trace #(\d+) t=(\d+) dt=(-?[\d.]+) (.*)$')
KV = re.compile(r'([a-z-]+) (\d+)')


def parse(path):
    frames = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = LINE.search(line)
            if not m:
                continue
            no, t, dt, rest = m.groups()
            rest = rest.replace('-[', ' free-').replace(']', '')
            # "img +N" -> created
            rest = rest.replace('img +', 'created ')
            vals = {}
            # the free reasons share names with other counters (expand, lcopy); prefix them
            in_free = False
            for tok in re.finditer(r'(free-)?([a-z-]+) (\d+)', rest):
                pre, key, val = tok.groups()
                if pre:
                    in_free = True
                if in_free and key in ('pool', 'depth', 'expand', 'ltarget', 'lcopy', 'rchance', 'fail', 'gc', 'unmap', 'tile'):
                    vals['free.' + key] = int(val)
                    if key == 'tile':
                        in_free = False
                else:
                    vals[key] = int(val)
            vals['dt'] = float(dt)
            frames.append((int(no), int(t), vals))
    return frames


def stats(frames):
    keys = sorted({k for _, _, v in frames for k in v})
    print('%-14s %8s %8s %8s %8s  nonzero' % ('counter', 'min', 'median', 'max', 'mean'))
    for k in keys:
        xs = sorted(v.get(k, 0) for _, _, v in frames)
        nz = sum(1 for x in xs if x)
        med = xs[len(xs) // 2]
        print('%-14s %8.1f %8.1f %8.1f %8.1f  %d/%d' % (k, xs[0], med, xs[-1], sum(xs) / len(xs), nz, len(xs)))
    return keys


def outliers(frames, keys):
    # a frame is unusual when one of its counters differs from the median by more than the
    # typical spread (median absolute deviation) - printed with the deviating counters
    med = {}
    mad = {}
    for k in keys:
        xs = sorted(v.get(k, 0) for _, _, v in frames)
        med[k] = xs[len(xs) // 2]
        dev = sorted(abs(x - med[k]) for x in xs)
        mad[k] = max(dev[len(dev) // 2], 1)
    print()
    print('unusual frames (counter: value vs median):')
    rare = Counter()
    for no, t, v in frames:
        odd = []
        for k in keys:
            x = v.get(k, 0)
            if abs(x - med[k]) > 4 * mad[k] and abs(x - med[k]) >= 2:
                odd.append('%s %g (med %g)' % (k, x, med[k]))
                rare[k] += 1
        if odd:
            print('  #%d t=%d: %s' % (no, t, ', '.join(odd)))
    print()
    print('counters that deviate most often:', rare.most_common(10))


def match_burst(frames, burst_dir):
    try:
        from PIL import Image, ImageStat
    except ImportError:
        print('PIL missing, skipping screenshot scoring')
        return
    idx = os.path.join(burst_dir, 'index.csv')
    if not os.path.exists(idx):
        print('no index.csv in', burst_dir)
        return
    times = sorted(t for _, t, _ in frames)
    print()
    print('screenshots -> trace frame presented just before the capture, left-half band score:')
    for row in open(idx):
        parts = row.strip().split(',')
        if len(parts) < 3:
            continue
        i, t0, t1 = int(parts[0]), int(parts[1]), int(parts[2])
        before = [f for f in frames if f[1] <= t0]
        after = [f for f in frames if f[1] > t0]
        f = before[-1] if before else None
        shot = [p for p in os.listdir(burst_dir) if p.endswith('_%03d.png' % i)]
        score = ''
        if shot:
            im = Image.open(os.path.join(burst_dir, shot[0])).convert('L')
            w, h = im.size
            # left half of the game window (assumed to be the left part of the screen), middle bands
            bands = [ImageStat.Stat(im.crop((0, y, w // 4, y + h // 8))).mean[0] for y in range(0, h, h // 8)]
            score = 'bands ' + ' '.join('%3d' % b for b in bands)
        if f:
            lag = t0 - f[1]
            nxt = (after[0][1] - t0) if after else -1
            print('  shot %3d t=%d: frame #%d (%d ms earlier, next flip in %d ms) %s' % (i, t0, f[0], lag, nxt, score))
        else:
            print('  shot %3d t=%d: no trace frame before it %s' % (i, t0, score))


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else 'build/user/log/shad_log.txt'
    frames = parse(log)
    if not frames:
        print('no frame trace lines in', log)
        return
    print('%d traced frames, t %d .. %d' % (len(frames), frames[0][1], frames[-1][1]))
    keys = stats(frames)
    outliers(frames, keys)
    if len(sys.argv) > 2:
        match_burst(frames, sys.argv[2])


if __name__ == '__main__':
    main()
