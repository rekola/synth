#!/usr/bin/env python3
"""What the pictures an application uploads are made of, from a run through the emulated
terminal: how many colours each has, how well plain deflate does on it, and how much of it
changes from one upload to the next - the numbers that decide whether compression, a
palette or sending only the changed tiles pays. Usage:

    analyze_pictures.py SYNTH [--seconds N] [--view live|arrangement]

Uses zlib from Python's standard library as a stand-in for the in-tree deflate encoder.
"""
import argparse
import collections
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptylink import Link, CELL_H, CELL_W  # noqa: E402

TILES = {"cell 8x16": (CELL_W, CELL_H), "2 cells 16x16": (2 * CELL_W, CELL_H), "strip 8 x all": (CELL_W, None), "4 cells 32x16": (4 * CELL_W, CELL_H)}


def tiles(raw, w, h, tw, th):
    th = th or h
    out = {}
    for ty in range(0, h, th):
        for tx in range(0, w, tw):
            rows = [raw[((ty + y) * w + tx) * 3:((ty + y) * w + min(tx + tw, w)) * 3] for y in range(min(th, h - ty))]
            out[(tx, ty)] = b"".join(rows)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("synth")
    ap.add_argument("--seconds", type=float, default=14)
    ap.add_argument("--view", default="live")
    ap.add_argument("--song", default="songs/demo3.xml")
    a = ap.parse_args()
    cmd = [a.synth, "--null-audio", "--ui", "terminal"] + (["--view", "arrangement"] if a.view == "arrangement" else []) + [a.song]
    res = Link().run(cmd, env={"CELLUI_PIXELS": "kitty"}, keys=[(3.0, b" ")], duration=a.seconds, keep_pictures=True)
    pics = [p for p in res.pictures if p[0] - res.start > 6]
    span = max(1e-9, pics[-1][0] - pics[0][0]) if pics else 1
    groups = collections.defaultdict(list)
    for t, w, h, raw in pics:
        groups[(w, h)].append(raw)
    print("%s view, playing, %d pictures in %.1f s\n" % (a.view, len(pics), span))
    print("%-9s %7s %9s %8s %8s %7s %7s" % ("size", "per s", "raw kB/s", "colours", "deflate1", "deflt6", "pixels"))
    for (w, h), imgs in sorted(groups.items(), key=lambda kv: -len(kv[1]) * kv[0][0] * kv[0][1]):
        n = len(imgs)
        raw_b = w * h * 3
        col = [len({im[i:i + 3] for i in range(0, len(im), 3)}) for im in imgs[:40]]
        z1 = sum(len(zlib.compress(im, 1)) for im in imgs[:40]) / min(40, n)
        z6 = sum(len(zlib.compress(im, 6)) for im in imgs[:40]) / min(40, n)
        # Against the most similar of the last 8 uploads of this size (the same widget's last one).
        changed_px = []
        tile_frac = {k: [] for k in TILES}
        delta_bytes = {k: [] for k in TILES}
        for i in range(1, min(n, 120)):
            best = None
            for j in range(max(0, i - 8), i):
                d = sum(1 for k in range(0, raw_b, 3) if imgs[i][k:k + 3] != imgs[j][k:k + 3])
                if best is None or d < best[0]:
                    best = (d, j)
            changed_px.append(best[0] / (w * h))
            for name, (tw, th) in TILES.items():
                ta, tb = tiles(imgs[i], w, h, tw, th), tiles(imgs[best[1]], w, h, tw, th)
                changed = [k for k in ta if ta[k] != tb[k]]
                tile_frac[name].append(len(changed) / len(ta))
                delta_bytes[name].append(sum(len(zlib.compress(ta[k], 1)) + 60 for k in changed))
        mean = lambda xs: sum(xs) / len(xs) if xs else 0.0
        print("%4dx%-4d %7.1f %9.0f %8.0f %8.0f %7.0f %6.0f%%" % (w, h, n / span, n / span * raw_b / 1000, mean(col), z1, z6, 100 * mean(changed_px)))
        if raw_b > 2000:
            for name in TILES:
                print("            tiles %-14s %3.0f%% changed; sent as changed tiles, deflated: %6.0f B (whole picture deflated: %.0f B)" % (
                    name, 100 * mean(tile_frac[name]), mean(delta_bytes[name]), z1))


if __name__ == "__main__":
    main()
