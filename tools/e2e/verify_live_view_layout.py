#!/usr/bin/env python3
"""Drive Live View's layout through a pty: no scope row, the clip
grid's cursor its own - never moving the pattern editor's track, nor
following it - and never on its header row, each track's column marking the
clip that track is at, and the outline panel's button bar
and details popup (Escape closes it at once, yet still starts an Alt
chord).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

UP, DOWN, PGDN = b"\x1b[A", b"\x1b[B", b"\x1b[6~"
OUTLINE_COLS = 30  # the outline panel, including its divider column
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
    scr.pump(0.4)


def status(scr):
    return scr.dump().splitlines()[-1].strip()


# StyleProvider's highlight_bg_color / highlight_unfocused_bg_color.
CURSOR_BGS = ("bcd4e0", "3a4a54")


def grid_cursor_rows(scr):
    """Screen rows whose first clip-grid cell shows the clip grid's cursor
    on an empty slot (bright while focused, faint otherwise)."""
    return [y for y in range(2, 14) if scr.screen.buffer[y][OUTLINE_COLS + 1].bg in CURSOR_BGS]


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    scr.send(b"\t")  # Live View; the clip grid has focus
    scr.pump(0.8)
    lines = scr.dump().splitlines()
    check("no scope row: the outline and clip grid start right under the menu",
          lines[1].startswith(" Outline") and "T0" in lines[1], scr)

    for _ in range(5):
        scr.send(UP)
        scr.pump(0.1)
    check("the clip grid's cursor stops on the first clip row, not the header", grid_cursor_rows(scr) == [2], scr)

    for _ in range(3):
        scr.send(DOWN)
        scr.pump(0.1)
    other_window(scr)  # -> pattern editor
    # A note typed into the pattern editor creates a clip where the track
    # is - its first clip - not at the clip grid's cursor (row 4).
    scr.send(b"q")
    scr.pump(0.4)
    lines = scr.dump().splitlines()
    check("the clip grid's cursor doesn't move the pattern editor's track",
          "Clip" in lines[2] and "Clip" not in lines[5], scr)

    other_track_x = OUTLINE_COLS + 1 + 19  # the next track's column
    check("another track's column marks its own clip, not the cursor's row",
          [y for y in range(2, 14) if scr.screen.buffer[y][other_track_x].bg not in ("151515", "default")] == [2], scr)

    scr.send(PGDN)
    scr.pump(0.3)
    scr.send(PGDN)
    scr.pump(0.5)
    check("the unfocused clip grid's cursor keeps its own row", grid_cursor_rows(scr) == [5], scr)
    other_window(scr)  # -> outline panel
    scr.send(DOWN)
    scr.pump(0.1)
    scr.send(DOWN)  # a Song > Instruments row
    scr.pump(0.4)
    dump = scr.dump()
    check("the outline's button bar shows the row's actions", all(b in dump for b in ("[Del] Delete", "[a] Stop", "[?] Info")), scr)
    scr.send(b"?")
    scr.pump(0.5)
    check("? opens the details popup", "Details" in scr.dump() and "Play note keys to preview" in scr.dump(), scr)
    scr.send(vk.ctrl('g'))
    scr.pump(0.5)
    check("Ctrl-g closes it", "Play note keys to preview" not in scr.dump(), scr)

    # Escape closes it right away, yet still works as the Alt prefix: the
    # next key makes it M-x.
    scr.send(b"?")
    scr.pump(0.5)
    scr.send(b"\x1b")
    scr.pump(0.5)
    check("Escape closes the popup without waiting for another key", "Play note keys to preview" not in scr.dump(), scr)
    scr.send(b"x")
    scr.pump(0.5)
    check("that Escape still starts an Alt chord (ESC x is M-x)", "M-x" in scr.dump().splitlines()[-1], scr)
    scr.send(vk.ctrl('g'))
    scr.pump(0.4)

    scr.send(b"\t")
    scr.pump(0.8)
    check("Arrangement view brings the scope row back", "Outline" not in scr.dump().splitlines()[1], scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
