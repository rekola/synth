#!/usr/bin/env python3
"""Drive mouse-wheel scrolling through a pty: the wheel scrolls the widget
under the mouse without moving focus, and scrolls its view, never its
cursor (or, in the pattern editor's arrangement mode, the transport); Shift
scrolls tracks sideways; the next cursor move brings the view back.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def wheel(scr, y, x, down=True, shift=False):
    """An SGR mouse wheel event at screen (y, x), 0-based."""
    button = (65 if down else 64) + (4 if shift else 0)
    scr.send(f"\x1b[<{button};{x + 1};{y + 1}M".encode())
    scr.pump(0.3)


def pattern_rows(scr):
    return [line[1:3] for line in scr.dump().splitlines() if re.match(r"^ [0-9a-f]{2} │", line)]


def transport_row(scr):
    info = [line for line in scr.dump().splitlines() if "voices:" in line][-1]
    return info.split()[1]


def first_track_title(scr):
    line = next(line for line in scr.dump().splitlines() if "▌◂ T" in line)
    return re.search(r"▌◂ (T\d+)", line).group(1)


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    # Arrangement view, pattern editor focused.
    vk.other_window(scr)
    for _ in range(2):
        wheel(scr, 20, 50)
    check("the wheel scrolls the pattern editor's view one row per notch", pattern_rows(scr)[0] == "02", scr)
    check("but not the transport", transport_row(scr) == "1.1.1", scr)

    wheel(scr, 20, 50, shift=True)
    check("Shift+wheel scrolls tracks sideways", first_track_title(scr) != "T0", scr)

    wheel(scr, 3, 20)  # over the arrangement grid
    scr.send(b"\x1b[B")  # Down - still reaches the pattern editor
    scr.pump(0.4)
    check("wheeling over another widget leaves focus where it was", transport_row(scr) == "1.1.2", scr)
    check("a cursor move brings the view back to the cursor", "01" in pattern_rows(scr)[:4], scr)

    # Live View: the outline panel and the live-mode pattern editor.
    scr.send(b"\t")
    scr.pump(0.8)
    tree_before = scr.dump().splitlines()[3]
    for _ in range(3):
        wheel(scr, 6, 5)  # over the outline tree
    check("the wheel scrolls the outline tree", scr.dump().splitlines()[3] != tree_before, scr)

    for _ in range(4):
        wheel(scr, 30, 50)  # over the pattern editor, which still has focus
    # A note typed into the pattern editor creates a clip in its scene.
    scr.send(b"q")
    scr.pump(0.4)
    check("scrolling the Live View pattern editor doesn't change its scene",
          "Clip" in scr.dump().splitlines()[2], scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
