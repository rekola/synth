#!/usr/bin/env python3
"""Drive Arrangement view's block commands over a placed clip through a
pty (fixture: arrangement_clip_region_test.xml - background C-4 on every
row, a 4-row one-shot clip of E-4s shown on rows 4-7). A region acts on
what it shows: marked inside the clip, it stops at the clip's end and
kills the clip's notes, never the background; a yank lands in whatever the
cursor is on, and stops where that content ends.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "arrangement_clip_region_test.xml")
DOWN, UP = b"\x1b[B", b"\x1b[A"
results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def notes(scr):
    """The section's own 16 rows' first note column, in order."""
    lines = [line for line in scr.dump().splitlines() if re.match(r"^ [0-9a-f]{2} │", line)]
    return [line[5:8] for line in lines[:16]]


def move(scr, key, times):
    for _ in range(times):
        scr.send(key)
        scr.pump(0.1)
    scr.pump(0.3)


def main():
    pid, fd = vk.spawn(FIXTURE)
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)
    vk.other_window(scr)  # the pattern editor

    # Mark on row 5 (inside the clip), point on row 11 (background), kill.
    move(scr, DOWN, 5)
    scr.send(vk.ctrl('b'))
    scr.pump(0.3)
    move(scr, DOWN, 6)
    scr.send(vk.ctrl('w'))
    scr.pump(0.5)
    shown = notes(scr)
    check("the kill clears the clip's rows from the mark to the clip's end", shown[5:8] == ["···"] * 3, scr)
    check("but not the clip's row before the mark", shown[4] == "E-4", scr)
    check("and never the background past the clip", shown[8:12] == ["C-4"] * 4, scr)

    # The cursor is back on row 5; yank onto the background at row 9.
    move(scr, DOWN, 4)
    scr.send(vk.ctrl('y'))
    scr.pump(0.5)
    shown = notes(scr)
    check("a yank onto the background lands there", shown[9:12] == ["E-4"] * 3 and shown[12] == "C-4", scr)

    # Yank at row 6, inside the clip: rows 6 and 7 are the clip's, row 8
    # isn't - the paste stops at the clip's end.
    move(scr, UP, 3)
    scr.send(vk.ctrl('y'))
    scr.pump(0.5)
    shown = notes(scr)
    check("a yank into the clip fills it", shown[6:8] == ["E-4"] * 2, scr)
    check("and stops at the clip's end, not spilling into the background", shown[8] == "C-4", scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
