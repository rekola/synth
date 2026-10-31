#!/usr/bin/env python3
"""What the real application sends over a terminal link, by scenario (phase 0 of the link
classes). Runs `synth` through the emulated terminal on an unshaped link, so the figures are
what each scenario *asks* of a link, not what one gives.

    measure_link.py SYNTH [--song songs/demo3.xml]

Bytes are as the terminal receives them (picture payloads are base64). Pictures are listed by
size; the application's own per-second log (SYNTH_LINK_STATS) gives the same on a real link.
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptylink import Link, COLS, ROWS  # noqa: E402

DOWN = b"\x1b[B"


def mouse_path(start, seconds, rate=30):
    out = []
    for i in range(int(seconds * rate)):
        x = 1 + (i * 3) % (COLS - 1)
        y = 1 + (i * 2) % (ROWS - 1)
        out.append((start + i / rate, b"\x1b[<35;%d;%dM" % (x, y)))
    return out


SCENARIOS = [
    ("live view, idle", [], []),
    ("live view, playing", [(3.0, b" ")], []),
    ("arrangement view, playing", [(3.0, b" ")], ["--view", "arrangement"]),
    ("arrangement, pattern editor, Down 30/s", [(2.0, b"\x18o")] + [(3.0 + i / 30.0, DOWN) for i in range(300)], ["--view", "arrangement"]),
    ("live view, mouse moving 30/s (stopped)", mouse_path(3.0, 9), []),
    ("live view, playing + mouse moving", [(3.0, b" ")] + mouse_path(4.0, 9), []),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("synth")
    ap.add_argument("--song", default="songs/demo3.xml")
    a = ap.parse_args()
    print("%-42s %9s %9s %9s %9s %8s %9s" % ("scenario (kitty, 120x40, unshaped)", "kB/s", "pictures", "placement", "text etc", "frames/s", "uploads/s"))
    for name, keys, extra in SCENARIOS:
        cmd = [a.synth, "--null-audio", "--ui", "terminal"] + extra + [a.song]
        res = Link().run(cmd, env={"CELLUI_PIXELS": "kitty"}, keys=keys, duration=15)
        t0, t1 = res.start + 6, res.start + 14
        total = sum(n for t, n in res.series if t0 <= t < t1)
        up = sum(n for t, act, n in res.kitty if act in ("t", "chunk") and t0 <= t < t1)
        pl = sum(n for t, act, n in res.kitty if act in ("p", "d") and t0 <= t < t1)
        frames = sum(1 for t in res.fences if t0 <= t < t1)
        sizes = collections.Counter((w, h) for t, w, h in res.dims if t0 <= t < t1)
        uploads = sum(sizes.values())
        dt = t1 - t0
        print("%-42s %9.1f %9.1f %9.2f %9.1f %8.0f %9.0f" % (name, total / dt / 1000, up / dt / 1000, pl / dt / 1000, (total - up - pl) / dt / 1000, frames / dt, uploads / dt))
        for (w, h), n in sizes.most_common(4):
            print("      %4dx%-4d px   %6.1f uploads/s   %7.0f kB/s as sent" % (w, h, n / dt, n / dt * w * h * 4 / 1000))


if __name__ == "__main__":
    main()
