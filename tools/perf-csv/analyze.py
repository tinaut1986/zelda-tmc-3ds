#!/usr/bin/env python3
"""Summarise a PERF RECORDER file from the console (debug/tmc-perf-NNNN.csv).

    tools/perf-csv/analyze.py FILE.csv [--worst N] [--by-room]

Prints the frame rate and its percentiles, how many frames missed a 60 Hz
vblank, what each part of the frame costs (mean, p95, max), and the worst
frames with their parts, so it is clear whether a slow frame was the engine,
the GPU wait, the command build, the software renderer or the bottom screen.
Columns: docs/debug-tools.md, "Perf recorder". Standard library only.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict

PARTS = ['work_ms', 'vblank_ms', 'gpu_begin_ms', 'gpu_build_ms', 'sw_render_ms', 'top_ms', 'bottom_ms']
FRAME_MS = 1000.0 / 59.8261  # the 3DS LCD


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[k]


def load(path):
    header = None
    rows = []
    with open(path, newline='') as f:
        for line in f:
            if line.startswith('#'):
                header = line[1:].strip()
                continue
            break
        f.seek(0)
        reader = csv.DictReader(line for line in f if not line.startswith('#'))
        for r in reader:
            for k, v in r.items():
                if k in ('area', 'room'):
                    continue
                r[k] = float(v)
            rows.append(r)
    return header, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv')
    ap.add_argument('--worst', type=int, default=10, help='how many of the slowest frames to list')
    ap.add_argument('--by-room', action='store_true', help='frame rate per area/room')
    a = ap.parse_args()
    header, rows = load(a.csv)
    if len(rows) < 2:
        sys.exit('not enough rows')
    if header:
        print(header)
    frames = rows[1:]  # the first row has no interval
    intervals = [r['interval_ms'] for r in frames]
    total_s = (rows[-1]['t_ms'] - rows[0]['t_ms']) / 1000.0
    logic = sum(r['logic_frames'] for r in frames)
    print(f'{len(rows)} frames shown in {total_s:.1f} s: {len(frames) / total_s:.2f} fps shown, '
          f'{logic / total_s:.2f} logic ticks/s')
    over1 = sum(1 for v in intervals if v > FRAME_MS * 1.5)
    over2 = sum(1 for v in intervals if v > FRAME_MS * 2.5)
    skipped = sum(1 for r in frames if r['logic_frames'] > 1)
    print(f'interval ms: mean {statistics.mean(intervals):.2f}  p50 {pct(intervals, 50):.2f}  '
          f'p95 {pct(intervals, 95):.2f}  p99 {pct(intervals, 99):.2f}  max {max(intervals):.2f}')
    print(f'missed vblanks: {over1} frames took 2+ vblanks ({100.0 * over1 / len(frames):.1f}%), '
          f'{over2} took 3+; {skipped} frames came after skipped presentations')
    gpu = sum(1 for r in rows if r['gpu'] >= 1)
    print(f'GPU renderer drew {gpu} of {len(rows)} frames ({100.0 * gpu / len(rows):.1f}%)')
    print()
    print(f'{"part":<14}{"mean":>8}{"p95":>8}{"max":>8}')
    for p in PARTS:
        vals = [r[p] for r in rows]
        nz = [v for v in vals if v > 0]
        note = ''
        if not nz:
            note = '   (never)'
        elif len(nz) < len(vals):
            note = f'   (in {len(nz)} frames: mean {statistics.mean(nz):.2f})'
        print(f'{p:<14}{statistics.mean(vals):>8.2f}{pct(vals, 95):>8.2f}{max(vals):>8.2f}' + note)
    print()
    print(f'worst {a.worst} frames by interval:')
    print(f'{"frame":>7}{"interval":>9}' + ''.join(f'{p[:-3]:>10}' for p in PARTS) + '  gpu area room')
    for r in sorted(frames, key=lambda r: -r['interval_ms'])[:a.worst]:
        print(f'{int(r["frame"]):>7}{r["interval_ms"]:>9.2f}' + ''.join(f'{r[p]:>10.2f}' for p in PARTS)
              + f'  {int(r["gpu"]):>3} {r["area"]:>4} {r["room"]:>4}')
    if a.by_room:
        print()
        by = defaultdict(list)
        for r in frames:
            by[(r['area'], r['room'])].append(r['interval_ms'])
        print(f'{"area/room":<11}{"frames":>7}{"fps":>8}{"p95 ms":>8}')
        for key, vals in sorted(by.items(), key=lambda kv: -len(kv[1])):
            print(f'{key[0]}/{key[1]:<8}{len(vals):>7}{1000.0 / statistics.mean(vals):>8.2f}{pct(vals, 95):>8.2f}')


if __name__ == '__main__':
    main()
