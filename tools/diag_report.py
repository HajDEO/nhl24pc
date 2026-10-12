#!/usr/bin/env python3
"""Report from the nhlport diagnostic log (nhl24_frames.csv + nhl24_diag.log + nhl24_threads.csv).

    python tools/diag_report.py <dir or nhl24_frames.csv> [--top N] [--stutter MS] [--csv out.csv]

<dir> is a directory holding nhl24_frames.csv / nhl24_diag.log / nhl24_threads.csv (the game's
user/log folder or a logs/ copy made by run_nhl24.ps1, where the files carry a run stamp prefix).
The report prints: frame-time statistics, a stutter table with the per-frame causes, a GPU-bound /
CPU-bound verdict, the scene-resolution timeline, glitch indicators (dropped draws, texture cache
churn, invalidations, warnings), thread CPU usage and the user markers with the frames around them.
"""

import argparse
import csv
import glob
import os
import re
import statistics
import sys
from collections import Counter, defaultdict


def find_files(target):
    """Returns (frames_csv, diag_log, threads_csv) paths; any may be None."""
    if os.path.isfile(target):
        base_dir = os.path.dirname(target) or "."
        name = os.path.basename(target)
        prefix = name[: name.find("nhl24_")] if "nhl24_" in name else ""
    else:
        base_dir = target
        prefix = ""
        candidates = sorted(glob.glob(os.path.join(base_dir, "*nhl24_frames.csv")),
                            key=os.path.getmtime)
        if candidates:
            name = os.path.basename(candidates[-1])
            prefix = name[: name.find("nhl24_")]

    def pick(suffix):
        path = os.path.join(base_dir, prefix + suffix)
        return path if os.path.isfile(path) else None

    return pick("nhl24_frames.csv"), pick("nhl24_diag.log"), pick("nhl24_threads.csv")


def to_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def load_frames(path):
    rows = []
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            parsed = {}
            for key, value in row.items():
                if key is None:
                    continue
                parsed[key] = value if key == "flags" else to_float(value)
            if parsed.get("flags") is None:
                parsed["flags"] = ""
            rows.append(parsed)
    return rows


def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    idx = min(len(sorted_values) - 1, int(pct / 100.0 * len(sorted_values)))
    return sorted_values[idx]


def fmt_ms(value):
    return f"{value:.1f} ms"


def section(title):
    print()
    print(title)
    print("=" * len(title))


def report_frames(rows, top, stutter_ms):
    dts = sorted(r["dt_ms"] for r in rows)
    total_s = sum(dts) / 1000.0
    section("Frame times")
    print(f"frames: {len(rows)}  duration: {total_s:.1f} s  average: {len(rows) / total_s:.1f} fps "
          f"({statistics.fmean(dts):.2f} ms)")
    p50, p90, p99, p999 = (percentile(dts, p) for p in (50, 90, 99, 99.9))
    print(f"median {p50:.2f} ms  90% {p90:.2f}  99% {p99:.2f}  99.9% {p999:.2f}  max {dts[-1]:.1f} ms")
    print(f"1% low {1000.0 / max(p99, 0.001):.1f} fps  0.1% low {1000.0 / max(p999, 0.001):.1f} fps")
    slow33 = sum(1 for d in dts if d > 33.4)
    slow100 = sum(1 for d in dts if d > 100)
    print(f"frames > 33 ms: {slow33} ({100.0 * slow33 / len(dts):.1f}%)   > 100 ms: {slow100}")
    edges = [8.4, 12.6, 16.8, 20, 25, 33.4, 50, 100, 250]
    lo = 0
    parts = []
    pos = 0
    for hi in edges:
        n = 0
        while pos < len(dts) and dts[pos] <= hi:
            pos += 1
            n += 1
        parts.append(f"{lo:.0f}-{hi:.0f}: {100.0 * n / len(dts):.1f}%")
        lo = hi
    parts.append(f">250: {100.0 * (len(dts) - pos) / len(dts):.1f}%")
    print("histogram (ms): " + "  ".join(parts))

    # Bound analysis: compare GPU busy / GPU thread / jobs against the frame interval.
    def share(key):
        vals = [min(1.0, r[key] / r["dt_ms"]) for r in rows if r["dt_ms"] > 0]
        return 100.0 * statistics.fmean(vals) if vals else 0.0

    section("Where the time goes (average share of the frame interval)")
    print(f"GPU busy (approx.) {share('gpu_busy_ms'):.0f}%   GPU thread CPU {share('gpu_thr_ms'):.0f}%   "
          f"present thread {share('present_ms'):.0f}%   simulation {share('sim_ms'):.0f}%   "
          f"render loop {share('render_ms'):.0f}%   job threads (sum) {share('jobs_ms'):.0f}%")
    lat = [r["gpu_lat_ms"] for r in rows if r["gpu_lat_ms"] > 0]
    if lat:
        print(f"GPU submit latency: median {statistics.median(lat):.1f} ms, max {max(r['gpu_lat_max_ms'] for r in rows):.1f} ms")
    gpu_bound = sum(1 for r in rows if "G" in r["flags"])
    thr_bound = sum(1 for r in rows if "T" in r["flags"])
    print(f"frames flagged GPU-bound (G): {gpu_bound} ({100.0 * gpu_bound / len(rows):.1f}%)   "
          f"GPU-thread-bound (T): {thr_bound} ({100.0 * thr_bound / len(rows):.1f}%)")
    stalls = sum(r["stall_ms"] for r in rows)
    compiles = sum(r["pipe_ms"] + r["shader_ms"] for r in rows)
    rb_wait = sum(r["rb_wait_ms"] for r in rows)
    print(f"GPU-thread stalls total {stalls / 1000.0:.1f} s ({100.0 * stalls / 1000.0 / total_s:.1f}% of the session): "
          f"compiles {compiles / 1000.0:.1f} s, readback waits {rb_wait / 1000.0:.1f} s")

    section(f"Stutters (dt > {stutter_ms:.0f} ms or flagged S), {top} worst")
    stutters = [r for r in rows if r["dt_ms"] > stutter_ms or "S" in r["flags"]]
    print(f"{len(stutters)} stutter frames")
    header = (f"{'frame':>7} {'t(s)':>8} {'dt':>7} {'comp':>5} {'cms':>6} {'stall':>7} {'rb':>4} "
              f"{'rbw':>6} {'img+':>5} {'inv':>5} {'file':>7} {'gpu':>6} {'thr':>6} {'jobs':>6} "
              f"{'pf':>5} {'scene':>10} flags")
    print(header)
    for r in sorted(stutters, key=lambda x: -x["dt_ms"])[:top]:
        inval = r["cpu_inval"] + r["cp_inval"] + r["gpu_inval"]
        print(f"{int(r['frame']):>7} {r['t_s']:>8.1f} {r['dt_ms']:>7.1f} {int(r['pipes'] + r['shaders']):>5} "
              f"{r['pipe_ms'] + r['shader_ms']:>6.1f} {r['stall_ms']:>7.1f} {int(r['readbacks']):>4} "
              f"{r['rb_wait_ms']:>6.1f} {int(r['img_new']):>5} {int(inval):>5} {int(r['file_kb']):>6}K "
              f"{r['gpu_busy_ms']:>6.1f} {r['gpu_thr_ms']:>6.1f} {r['jobs_ms']:>6.1f} {int(r['page_faults']):>5} "
              f"{int(r['scene_w'])}x{int(r['scene_h']):<5} {r['flags']}")
    if stutters:
        causes = Counter()
        for r in stutters:
            if r["pipes"] + r["shaders"] > 0:
                causes["pipeline/shader compile"] += 1
            if r["stall_ms"] > 2:
                causes["gpu stall (wait/readback)"] += 1
            if r["img_new"] >= 20:
                causes["texture cache churn"] += 1
            if r["file_kb"] >= 512:
                causes["file I/O"] += 1
            if r["page_faults"] >= 200:
                causes["page faults"] += 1
            if "R" in r["flags"]:
                causes["resolution change"] += 1
            if r["gpu_busy_ms"] >= 0.9 * r["dt_ms"]:
                causes["gpu bound"] += 1
            if r["gpu_thr_ms"] >= 0.9 * r["dt_ms"]:
                causes["gpu thread bound"] += 1
            if r["jobs_ms"] >= 2.0 * r["dt_ms"]:
                causes["job threads saturated"] += 1
        print("stutter frames with a visible cause: " +
              ", ".join(f"{k} {v}" for k, v in causes.most_common()))


def report_resolution(rows):
    section("Scene resolution (largest depth target per GPU frame)")
    hist = Counter((int(r["scene_w"]), int(r["scene_h"])) for r in rows)
    total = sum(hist.values())
    print("share: " + ", ".join(f"{w}x{h} {100.0 * n / total:.1f}%" for (w, h), n in hist.most_common(10)))
    changes = []
    last = None
    for r in rows:
        key = (int(r["scene_w"]), int(r["scene_h"]))
        if key != last:
            changes.append((r["t_s"], int(r["frame"]), key))
            last = key
    print(f"resolution changes: {max(0, len(changes) - 1)}")
    for t, frame, (w, h) in changes[:40]:
        print(f"  @{t:8.1f}s frame {frame:>7}: {w}x{h}")
    if len(changes) > 40:
        print(f"  ... {len(changes) - 40} more")
    sizes = [r["rt_sizes"] for r in rows]
    passes = [r["render_passes"] for r in rows]
    if sizes:
        print(f"distinct render-target sizes per frame: median {statistics.median(sizes):.0f}, max {max(sizes):.0f};  "
              f"render passes per frame: median {statistics.median(passes):.0f}, max {max(passes):.0f}")
    vo = Counter((int(r["vo_w"]), int(r["vo_h"])) for r in rows)
    win = Counter((int(r["win_w"]), int(r["win_h"])) for r in rows)
    print("video out buffer: " + ", ".join(f"{w}x{h} ({n})" for (w, h), n in vo.most_common(4)) +
          "   window: " + ", ".join(f"{w}x{h} ({n})" for (w, h), n in win.most_common(4)))


def report_glitches(rows):
    section("Glitch indicators")
    dropped = [r for r in rows if r["skip_nopipe"] + r["skip_bind"] > 0]
    print(f"frames with dropped draws: {len(dropped)} ({100.0 * len(dropped) / len(rows):.1f}%), "
          f"no-pipeline {int(sum(r['skip_nopipe'] for r in rows))}, bind-fail {int(sum(r['skip_bind'] for r in rows))}")
    if "cs_meta_clear" in rows[0]:
        print(f"compute dispatches replaced by emulator fast paths (intentional): meta-clear "
              f"{int(sum(r['cs_meta_clear'] for r in rows))}, image-copy {int(sum(r['cs_image_copy'] for r in rows))}, "
              f"image-clear {int(sum(r['cs_image_clear'] for r in rows))}, hle {int(sum(r['cs_hle'] for r in rows))}")
    churn = [r for r in rows if r["img_new"] >= 20]
    print(f"frames with >= 20 new images: {len(churn)};  images created total {int(sum(r['img_new'] for r in rows))}, "
          f"freed {int(sum(r['img_freed'] for r in rows))}, expand {int(sum(r['tc_expand'] for r in rows))}, "
          f"depth<->colour recreate {int(sum(r['tc_depth_rec'] for r in rows))}, "
          f"incompatible views {int(sum(r['tc_incompat'] for r in rows))}")
    print(f"invalidations: cpu {int(sum(r['cpu_inval'] for r in rows))}, inline-write {int(sum(r['cp_inval'] for r in rows))}, "
          f"gpu {int(sum(r['gpu_inval'] for r in rows))};  image re-uploads {int(sum(r['refresh'] for r in rows))} "
          f"({sum(r['refresh_kb'] for r in rows) / 1024:.0f} MB);  streamed promotions {int(sum(r['streamed'] for r in rows))}")
    print(f"readbacks: {int(sum(r['readbacks'] for r in rows))} ({sum(r['rb_kb'] for r in rows) / 1024:.0f} MB), "
          f"cpu-write faults {int(sum(r['rb_cpu_w'] for r in rows))}, cpu-read faults {int(sum(r['rb_cpu_r'] for r in rows))}, "
          f"gpu waits {int(sum(r['gpu_waits'] for r in rows))}")
    print(f"flip backlog >= 3 (Q): {sum(1 for r in rows if 'Q' in r['flags'])} frames;  "
          f"audio waits > 10 ms (A): {sum(1 for r in rows if 'A' in r['flags'])} frames;  "
          f"audio outputs {int(sum(r['audio_out'] for r in rows))}")
    flag_counts = Counter()
    for r in rows:
        for c in r["flags"]:
            flag_counts[c] += 1
    print("flag totals: " + "  ".join(f"{k}:{v}" for k, v in sorted(flag_counts.items())))


def report_log(diag_log, rows, top):
    if not diag_log:
        return
    events = defaultdict(list)
    header = []
    last = None  # (category, index) of the previous event, for continuation lines
    with open(diag_log, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line.startswith("#"):
                header.append(line)
                continue
            m = re.match(r"\[\s*([0-9.]+)\] (\w+): (.*)", line)
            if m:
                events[m.group(2)].append((float(m.group(1)), m.group(3)))
                last = (m.group(2), len(events[m.group(2)]) - 1)
            elif line.startswith("    ") and last:
                # Multi-line event (worst frames, warning sites, markers): keep the whole block.
                cat, idx = last
                t, msg = events[cat][idx]
                events[cat][idx] = (t, msg + "\n" + line)
    section("Session")
    for line in header[:12]:
        print(line)
    if events.get("resolution"):
        section("Resolution events (diag log)")
        for t, msg in events["resolution"][:30]:
            print(f"  @{t:8.1f}s {msg}")
    if events.get("videoout") or events.get("window"):
        section("Video out / window events")
        for t, msg in (events.get("videoout", []) + events.get("window", []))[:30]:
            print(f"  @{t:8.1f}s {msg}")
    if events.get("glitch"):
        section("Glitch events (diag log)")
        for t, msg in events["glitch"][:20]:
            print(f"  @{t:8.1f}s {msg}")
    if events.get("compute-shortcut"):
        section("Compute dispatches replaced by emulator fast paths (first occurrence per shader)")
        for t, msg in events["compute-shortcut"][:40]:
            print(f"  @{t:8.1f}s {msg}")
    if events.get("compile"):
        section("Compiles")
        comp = events["compile"]
        per_10s = Counter(int(t // 10) * 10 for t, _ in comp)
        print(f"{len(comp)} compile events;  busiest 10 s windows: " +
              ", ".join(f"@{k}s {v}" for k, v in per_10s.most_common(6)))
        slow = sorted(((float(re.search(r"([0-9.]+) ms", m).group(1)), t, m) for t, m in comp
                       if re.search(r"([0-9.]+) ms", m)), reverse=True)[:top]
        for ms, t, msg in slow:
            print(f"  @{t:8.1f}s {msg}")
    if events.get("marker"):
        section("User markers")
        by_frame = {int(r["frame"]): r for r in rows}
        for t, msg in events["marker"]:
            print(f"  @{t:8.1f}s {msg}")
            m = re.search(r"at frame (\d+)", msg)
            if m:
                frame = int(m.group(1))
                for f in range(frame - 3, frame + 4):
                    r = by_frame.get(f)
                    if r:
                        print(f"      frame {f}: {r['dt_ms']:.1f} ms scene {int(r['scene_w'])}x{int(r['scene_h'])} "
                              f"img+{int(r['img_new'])} inval {int(r['cpu_inval'] + r['cp_inval'] + r['gpu_inval'])} "
                              f"dropped {int(r['skip_nopipe'] + r['skip_bind'])} flags {r['flags']}")
    finals = events.get("final", [])
    if finals:
        section("Final summary (from the diag log)")
        for t, msg in finals:
            print(msg)
    summaries = events.get("summary", [])
    warn = [msg for _, msg in summaries if "message sites:" in msg]
    if warn and not finals:
        section("Last warning summary")
        print(warn[-1])


def report_threads(threads_csv, top):
    if not threads_csv:
        return
    totals = defaultdict(list)
    with open(threads_csv, newline="", encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle):
            totals[row["name"]].append(to_float(row["cpu_pct"]))
    section("Thread CPU (average of the sampled seconds, 100 = one core)")
    rows = sorted(((statistics.fmean(v), max(v), name) for name, v in totals.items()), reverse=True)
    for avg, peak, name in rows[:top]:
        print(f"  {name:<36} avg {avg:6.1f}%  peak {peak:6.1f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", help="directory or nhl24_frames.csv")
    parser.add_argument("--top", type=int, default=20, help="rows in the stutter / compile tables")
    parser.add_argument("--stutter", type=float, default=50.0, help="stutter threshold in ms")
    parser.add_argument("--csv", help="write the stutter frames to this CSV")
    args = parser.parse_args()

    frames_csv, diag_log, threads_csv = find_files(args.target)
    if not frames_csv:
        print(f"no nhl24_frames.csv found at {args.target}", file=sys.stderr)
        return 2
    rows = load_frames(frames_csv)
    if not rows:
        print("no frames in the CSV", file=sys.stderr)
        return 2
    print(f"frames: {frames_csv}\ndiag:   {diag_log}\nthreads: {threads_csv}")
    report_frames(rows, args.top, args.stutter)
    report_resolution(rows)
    report_glitches(rows)
    report_threads(threads_csv, args.top)
    report_log(diag_log, rows, args.top)
    if args.csv:
        stutters = [r for r in rows if r["dt_ms"] > args.stutter or "S" in r["flags"]]
        with open(args.csv, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(stutters)
        print(f"\nstutter frames written to {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
