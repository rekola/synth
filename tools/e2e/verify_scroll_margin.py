#!/usr/bin/env python3
"""Drive Arrangement view's pattern editor through a pty: moving the cursor
moves the highlighted row down the screen without scrolling, and the view
only starts to scroll once the cursor comes within the scroll margin (3
rows) of the bottom edge - it then stays 3 rows above the bottom.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

DOWN = b"\x1b[B"
MARGIN = 3
results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def pattern_rows(scr):
    """The pattern editor's rows: (row number, is the first on screen)."""
    lines = scr.dump().splitlines()
    heading = next((i for i, line in enumerate(lines) if "▌◂ T0" in line), -1)
    numbers = []
    for line in lines[heading + 2:]:
        m = re.match(r" ([0-9a-f]{2}) │", line)
        if not m:
            break
        numbers.append(int(m.group(1), 16))
    return numbers


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)
    vk.other_window(scr)  # the pattern editor
    scr.pump(0.4)

    visible = len(pattern_rows(scr))
    check("the pattern editor shows rows from 00", visible > 2 * MARGIN and pattern_rows(scr)[0] == 0, scr)

    # Down until the view first scrolls: the cursor (row `steps`) is then
    # MARGIN rows above the bottom.
    steps = 0
    while steps < 64 and pattern_rows(scr)[0] == 0:
        scr.send(DOWN)
        scr.pump(0.15)
        steps += 1
    check("the view doesn't scroll until the cursor nears the bottom", steps == visible - MARGIN, scr)
    cursor_line = steps - pattern_rows(scr)[0]
    check("once scrolling, the cursor stays the margin above the bottom", cursor_line == visible - 1 - MARGIN, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
