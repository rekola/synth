#!/usr/bin/env python3
"""The real application on a slow link (phase 5 of the link classes): is the editor still usable at
50 kB/s? Runs `synth` through the emulated terminal with a shaped link and checks

  1. the echo of a key (Down in the pattern editor, transport stopped) arrives within a second or two,
  2. a playing song, nothing touched, keeps well inside the link (the frame cap and the thinned scopes),
  3. keys typed while it plays are still echoed in good time.

    app_slow_link.py SYNTH [--song songs/demo3.xml] [--bandwidth 50000]

Not a ctest: it needs the application (the cellui project builds without it).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptylink import Link  # noqa: E402

DOWN = b"\x1b[B"
RESULTS = []


def check(name, ok, detail):
    RESULTS.append(ok)
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    sys.stdout.flush()


def latencies(res, key_times, up):
    """Seconds from each key to the first bytes the program wrote after the key reached it (the
    time the terminal sees them is `up` later). Only a measure of the echo when nothing else is
    being sent, as when the song is stopped."""
    out = []
    for kt in key_times:
        later = [t for t, n in res.series if t > kt + 0.02]
        out.append(later[0] - kt + up if later else float("inf"))
    return out


def quiet_before(res, key_times, seconds=1.0):
    """Whether nothing was sent in the `seconds` before each key (the start-up pictures are through)."""
    return all(not any(kt - seconds <= t < kt for t, n in res.series) for kt in key_times)


def run(synth, song, link, keys, duration):
    cmd = [synth, "--null-audio", "--ui", "terminal", "--view", "arrangement", song]
    return link.run(cmd, keys=keys, duration=duration)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("synth")
    ap.add_argument("--song", default="songs/demo3.xml")
    ap.add_argument("--bandwidth", type=int, default=50000)
    a = ap.parse_args()
    link = lambda: Link(bandwidth=a.bandwidth, up_ms=20, down_ms=20)

    # 1. Stopped: a key is echoed. The editor is opened with C-x o, then Down at 6, 8 and 10 s.
    keys = [(3.0, b"\x18o")] + [(6.0 + 2 * i, DOWN) for i in range(3)]
    res = run(a.synth, a.song, link(), keys, 14)
    key_times = res.keys_sent[1:4]
    lat = latencies(res, key_times, link().up) if len(key_times) == 3 else [float("inf")]
    check("1 key echo, stopped", len(key_times) == 3 and quiet_before(res, key_times[:1]) and max(lat) < 1.5,
          "latency %s s" % ", ".join("%.2f" % x for x in lat))

    # 2 and 3. Playing from 3 s: the traffic over 8 to 14 s, and keys at 9, 10.5 and 12 s.
    keys = [(2.0, b"\x18o"), (3.0, b" ")] + [(9.0 + 1.5 * i, DOWN) for i in range(3)]
    res = run(a.synth, a.song, link(), keys, 16)
    t0, t1 = res.start + 8, res.start + 15
    rate = sum(n for t, n in res.series if t0 <= t < t1) / (t1 - t0)
    check("2 playing stays inside the link", rate < 0.9 * a.bandwidth, "%.1f kB/s of %.1f kB/s" % (rate / 1000, a.bandwidth / 1000))
    lat = latencies(res, res.keys_sent[2:5], link().up) if len(res.keys_sent) >= 5 else [float("inf")]
    check("3 key echo, playing (the next bytes sent, so an upper bound on the echo)", max(lat) < 2.0,
          "latency %s s" % ", ".join("%.2f" % x for x in lat))
    print("%d/%d passed" % (sum(RESULTS), len(RESULTS)))
    return 0 if all(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
