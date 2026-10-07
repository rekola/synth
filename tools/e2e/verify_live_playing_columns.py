#!/usr/bin/env python3
"""Drive Live View's pattern editor through a pty with the focused track
playing: the columns to its right - stopped tracks - must show the same
rows while the highlighted row moves down with the playhead over them,
including while the playing track shows rows it doesn't have (before its
first clip), which draw blank.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

DOWN = b"\x1b[B"
CTRL_RIGHT = b"\x1b[1;5C"
CTRL_LEFT = b"\x1b[1;5D"
results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def other_window(scr):
    scr.send(vk.ctrl('x'))
    scr.pump(0.2)
    scr.send(b"o")
    scr.pump(0.5)


def main():
    pid, fd = vk.spawn(view="live")
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    # A note in T0's first clip and one in T1's, then launch T0's clip.
    other_window(scr)  # clip grid -> pattern editor, on T0
    scr.send(b"q")
    scr.pump(0.4)
    scr.send(CTRL_RIGHT)
    scr.pump(0.3)
    scr.send(DOWN)
    scr.pump(0.2)
    scr.send(b"w")
    scr.pump(0.4)
    scr.send(CTRL_LEFT)
    scr.pump(0.3)
    other_window(scr)  # -> outline panel
    other_window(scr)  # -> clip grid, on T0
    scr.send(b"\r")
    scr.wait(0.6)
    other_window(scr)  # -> pattern editor, on the playing T0

    lines = scr.dump().splitlines()
    top = next(i for i, line in enumerate(lines) if "▌◂ T0" in line) + 2
    divider = [i for i, c in enumerate(lines[top + 6]) if c == "│"][0]

    def right_of_t0():
        return ["".join(scr.screen.buffer[top + r][c].data for c in range(divider + 1, divider + 90)) for r in range(12)]

    def t0_blank_rows():
        return sum(1 for r in range(12)
                   if "".join(scr.screen.buffer[top + r][c].data for c in range(0, divider)).strip() == "")

    first = right_of_t0()
    unchanged = True
    saw_blank = False
    for _ in range(12):
        scr.wait(0.12)
        unchanged = unchanged and right_of_t0() == first
        saw_blank = saw_blank or t0_blank_rows() > 0
    check("the playing track shows the rows before its first clip blank", saw_blank, scr)
    check("the columns right of a playing track show the same rows as it plays", unchanged, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
