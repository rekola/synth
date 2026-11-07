#!/usr/bin/env python3
"""After the transport pauses and the last voice has died, the terminal UI must stop drawing.

    verify_idle_frames.py [SYNTH]

Plays idle_frames_test.xml (notes of a bell with a 1.17 s decay and a 0.231 s release) on a pty, pauses
after 5 s, and counts the frames the UI asks the terminal for each second (one per fence reply).
Scopes run while a voice decays; once it is gone (its decay, release and the analysers' settling, at most
SETTLE seconds after the pause) the frame rate must be at or under IDLE_FPS. Needs cellui's pacing
harness (lib/cellui/tools/pacing/ptylink.py).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "lib", "cellui", "tools", "pacing"))
from ptylink import Link  # noqa: E402

PAUSE_AT = 5.0
# The longest decay plus release is 1.4 s; the visualization thread keeps analysing for the 100 ms window,
# DirAC's smoothing and the meter's fall (kSettleSeconds), after which it sends nothing.
SETTLE = 10.0
DURATION = 25
IDLE_FPS = 2  # the idle gap the UI keeps for its clock and link text


def main():
    synth = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "synth")
    song = os.path.join(HERE, "idle_frames_test.xml")
    res = Link().run([synth, "--null-audio", "--ui", "terminal", song],
                     keys=[(2.0, b" "), (PAUSE_AT, b" ")], duration=DURATION)
    per_second = {}
    for t in res.fences:
        k = int(t - res.start)
        per_second[k] = per_second.get(k, 0) + 1
    fps = {k: v // 2 for k, v in per_second.items()}  # a fence leads and ends each frame
    print("frames/s:", " ".join("%d:%d" % (k, fps.get(k, 0)) for k in range(DURATION)))
    first_idle = next((k for k in range(int(PAUSE_AT), DURATION) if all(fps.get(j, 0) <= IDLE_FPS for j in range(k, DURATION))), None)
    if first_idle is None:
        print("FAIL: still drawing at the end (%d frames in the last second)" % fps.get(DURATION - 1, 0))
        return 1
    since = first_idle - PAUSE_AT
    print("idle from %.0f s after the pause" % since)
    if since > SETTLE:
        print("FAIL: idle only %.0f s after the pause (allowed %.0f s)" % (since, SETTLE))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
