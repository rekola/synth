#!/usr/bin/env python3
"""Drive PatternEditor's session mode (Session view) through a pty:
locators exist only in Arrangement view, typing a note into an empty
slot creates a clip there (and not in the arrangement), launching that
clip from the clip grid starts the transport and moves a playhead in its
own track's column only, and Space is the transport in Session view too:
it only pauses, and playing again resumes the launched clip.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

CTRL_RIGHT = b"\x1b[1;5C"
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


def first_pattern_row(scr):
    """Screen row index of the pattern editor's first "00 │" row - the first
    one below its own heading (the "▌◂ T0" track title row)."""
    lines = scr.dump().splitlines()
    heading = next((i for i, line in enumerate(lines) if "▌◂ T0" in line), -1)
    for i, line in enumerate(lines):
        if i > heading and line.startswith(" 00 │"):
            return i
    return -1


def row_tint(base):
    """StyleProvider::cursorRowTint() over `base` (hex)."""
    tint, alpha = (0x90, 0xb8, 0xcc), 0.35
    rgb = [int(base[i:i + 2], 16) for i in (0, 2, 4)]
    return "".join("%02x" % int(c * (1 - alpha) + t * alpha) for c, t in zip(rgb, tint))


# The marked row's tint over a plain, a beat and a bar row.
TINTED = {row_tint(b) for b in ("151515", "292929", "3d3d3d")}


def tinted_lines(scr, top, col):
    return tuple(r for r in range(16) if scr.screen.buffer[top + r][col].bg in TINTED)


def watch(scr, top, col, samples=8):
    """The distinct sets of tinted lines `col` shows over `samples` frames -
    more than one means its marked row moves: it's playing."""
    seen = set()
    for _ in range(samples):
        scr.pump(0.2)
        seen.add(tinted_lines(scr, top, col))
    return seen


def session_top(scr):
    """Screen row index of Session view's first pattern row - two below the
    "▌◂ T0" track title row (each column shows its own row numbers, so
    there's no shared "00 │" gutter to find)."""
    lines = scr.dump().splitlines()
    return next((i for i, line in enumerate(lines) if "▌◂ T0" in line), -1) + 2


def session_columns(scr, top):
    """Screen columns in Session view's pattern editor: T0's effect column
    (a cell of its own that isn't the cursor's note cell), T1's note
    column, and the "│" divider between them."""
    line = scr.dump().splitlines()[top]
    dividers = [i for i, c in enumerate(line) if c == "│"]
    return {"t0": dividers[0] - 3, "t1": dividers[0] + 5, "divider": dividers[0]}


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    # A locator in Arrangement view.
    vk.other_window(scr)
    for _ in range(12):
        scr.send(CTRL_RIGHT)
        scr.pump(0.1)
    scr.send(b"\r")
    scr.pump(0.4)
    scr.send(b"hello note\r")
    scr.pump(0.5)
    check("locator shows in Arrangement view", "hello note" in scr.dump(), scr)

    # Session view: clip grid on top, pattern editor below, no locators.
    scr.send(b"\t")
    scr.pump(0.8)
    check("Session view hides locators", "hello note" not in scr.dump(), scr)

    # The pattern editor keeps focus across the view switch.
    scr.send(vk.ctrl('a'))  # back to the first track
    scr.pump(0.3)
    scr.send(b"q")
    scr.pump(0.5)
    check("typing into an empty slot creates Clip 1", "Clip 1" in scr.dump(), scr)

    # The note went into the clip, not the arrangement.
    scr.send(b"\t")
    scr.pump(0.8)
    row = first_pattern_row(scr)
    line = scr.dump().splitlines()[row] if row >= 0 else ""
    check("the arrangement's own row 00 stays empty", vk.note_columns(line)[:1] == ["···"], scr)

    # Launch the clip from the clip grid; its playhead moves through T0's
    # column only.
    scr.send(b"\t")
    scr.pump(0.8)
    other_window(scr)  # pattern editor -> outline panel
    other_window(scr)  # -> clip grid
    scr.send(b"\r")
    scr.pump(0.2)
    top = session_top(scr)
    columns = session_columns(scr, top)
    # A cell of T0's own, its effect column - not its note cell, which is
    # the cursor's (on T0's playhead while it plays) and shows the cursor.
    T0_COL = columns["t0"]
    t0_seen, t1_seen = set(), set()
    tinted_divider = False
    for _ in range(12):
        scr.pump(0.2)
        t0_seen.add(tinted_lines(scr, top, T0_COL))
        t1_seen.add(tinted_lines(scr, top, columns["t1"]))
        tinted_divider |= bool(tinted_lines(scr, top, columns["divider"]))
    check("the launched clip's playhead moves through its own track's column", len(t0_seen) > 1, scr)
    check("a track with nothing launched keeps its position still", len(t1_seen) == 1, scr)
    check("the playhead stops short of the divider between tracks", not tinted_divider, scr)

    # Launching the playing clip again relaunches it at the next bar - a
    # launch never toggles - so past that bar its playhead still moves.
    scr.send(b"\r")
    scr.wait(3.0)  # past the next bar
    check("launching the playing clip again restarts it rather than stopping it", len(watch(scr, top, T0_COL)) > 1, scr)

    # Space is the transport in Session view too: the launch started it,
    # and it only pauses - playing again resumes the launched clip.
    other_window(scr)  # clip grid -> pattern editor
    check("launching started the transport", vk.is_playing(scr), scr)
    check("the clip grid marks the launched track as taken over", "◆" in scr.dump(), scr)
    scr.send(b" ")
    scr.pump(0.6)
    check("Space in Session view stops the transport", not vk.is_playing(scr), scr)
    scr.send(b" ")
    scr.pump(0.6)
    check("and starts it again", vk.is_playing(scr), scr)
    check("the launched clip resumes", len(watch(scr, top, T0_COL)) > 1, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
