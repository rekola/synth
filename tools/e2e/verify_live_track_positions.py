#!/usr/bin/env python3
"""Drive Live View's per-track positions through a pty: each track is
in its own clip (scene row) - the clip grid's cursor moves none, moving
the cursor takes every stopped track along, and launching a clip moves
its track there - and each pattern editor column's header names its own
clip and numbers its own rows. The highlighted row moves within the
view's margins without scrolling any column. A playing track's position
follows its playhead: Up on it does nothing and says so, while a stopped
track's still moves.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

UP = b"\x1b[A"
DOWN = b"\x1b[B"
LEFT = b"\x1b[D"
RIGHT = b"\x1b[C"
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


def clip_numbers(scr):
    """The clip number leading each pattern editor column's second heading
    line, left to right."""
    lines = scr.dump().splitlines()
    heading = next((i for i, line in enumerate(lines) if "▌◂ T0" in line), -1)
    if heading < 0 or heading + 1 >= len(lines):
        return []
    return re.findall(r"(\d+) \S", lines[heading + 1])


def columns(scr):
    """T0's whole column, and T0's and T1's effect columns, found from the
    "│" dividers after each track."""
    line = scr.dump().splitlines()[first_pattern_row(scr)]
    dividers = [i for i, c in enumerate(line) if c == "│"]
    return range(1, dividers[0]), dividers[0] - 3, dividers[1] - 3


def column_backgrounds(scr, col):
    top = first_pattern_row(scr)
    return [scr.screen.buffer[top + r][col].bg for r in range(16)]


def column_text(scr, cols):
    top = first_pattern_row(scr)
    return ["".join(scr.screen.buffer[top + r][c].data for c in cols) for r in range(16)]




def first_pattern_row(scr):
    lines = scr.dump().splitlines()
    heading = next((i for i, line in enumerate(lines) if "▌◂ T0" in line), -1)
    return heading + 2


def row_number_pairs_offset_by(scr, offset):
    """Whether, on every screen row where T0 and T1 both show a row number,
    T0's is T1's plus `offset` - both are in 4-row clips."""
    top = first_pattern_row(scr)
    lines = scr.dump().splitlines()
    divider = [i for i, c in enumerate(lines[top]) if c == "│"][0]
    pairs = []
    for line in lines[top:top + 16]:
        t0, t1 = line[1:3], line[divider + 1:divider + 3]
        if re.fullmatch(r"[0-9a-f]{2}", t0) and re.fullmatch(r"[0-9a-f]{2}", t1):
            pairs.append((int(t0, 16), int(t1, 16)))
    return bool(pairs) and all(t0 == (t1 + offset) % 4 for t0, t1 in pairs)


def grid_cursor_rows(scr, track_index):
    """Screen rows where the focused clip grid's cursor is, in track
    `track_index`'s column (after the 30-column outline panel)."""
    x = 31 + track_index * 19
    return [y for y in range(2, 14) if scr.screen.buffer[y][x].bg == "bcd4e0"]


def main():
    pid, fd = vk.spawn(view="live")
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    # The clip grid starts focused, on T0's first clip slot. Its cursor is
    # its own: moving it moves no track.
    scr.send(DOWN)
    scr.pump(0.3)
    scr.send(DOWN)
    scr.pump(0.5)
    check("the clip grid's cursor moves no track", clip_numbers(scr)[:2] == ["1", "1"], scr)

    # In the pattern editor, moving T0 takes the stopped T1 along: both end
    # up in their third clip (4 rows each).
    other_window(scr)  # clip grid -> pattern editor
    for _ in range(8):
        scr.send(DOWN)
        scr.pump(0.15)
    scr.pump(0.4)
    check("stopped tracks move along with the cursor", clip_numbers(scr)[:2] == ["3", "3"], scr)

    # A step within the margin moves only the highlighted row - no column
    # scrolls.
    t0_before = column_text(scr, columns(scr)[0])
    t1_before = column_text(scr, range(columns(scr)[1] + 4, columns(scr)[2] + 3))
    scr.send(DOWN)
    scr.pump(0.5)
    check("a step within the margin scrolls no column",
          column_text(scr, columns(scr)[0]) == t0_before and
          column_text(scr, range(columns(scr)[1] + 4, columns(scr)[2] + 3)) == t1_before, scr)
    check("each column numbers its own rows", row_number_pairs_offset_by(scr, 0), scr)
    scr.send(UP)
    scr.pump(0.5)

    # A note in each track's third clip, then back up into the second clip.
    scr.send(b"q")
    scr.pump(0.5)
    scr.send(CTRL_RIGHT)
    scr.pump(0.4)
    scr.send(UP)
    scr.pump(0.3)
    scr.send(b"q")
    scr.pump(0.5)
    for _ in range(3):
        scr.send(UP)
        scr.pump(0.15)
    scr.pump(0.4)
    check("both tracks are in their second clip", clip_numbers(scr)[:2] == ["2", "2"], scr)

    # Launching T1's third clip from the clip grid (its cursor still on the
    # third row) moves T1 there; T0 stays.
    other_window(scr)  # pattern editor -> outline panel
    other_window(scr)  # -> clip grid, on T1
    scr.send(b"\r")
    scr.wait(1.0)
    check("launching started the transport", vk.is_playing(scr), scr)
    check("launching moves the track to the clip", clip_numbers(scr)[1:2] == ["3"], scr)

    # On the playing T1, the clip grid's cursor moves freely, and T1 stays
    # in the clip it plays.
    scr.send(DOWN)
    scr.pump(0.6)
    check("the clip grid's cursor leaves a playing track's clip", grid_cursor_rows(scr, 1) == [5], scr)
    check("and the playing track stays in its clip", clip_numbers(scr)[1:2] == ["3"], scr)

    # T1 plays: Up on it does nothing and says so.
    other_window(scr)  # clip grid -> pattern editor
    scr.pump(0.4)
    scr.send(UP)
    scr.pump(0.4)
    check("Up on a playing track says it's playing", "This track is playing" in scr.dump(), scr)

    # With the playing T1 focused, the stopped T0 doesn't move as T1 plays.
    before = column_text(scr, columns(scr)[0])
    frames = []
    for _ in range(6):
        scr.wait(0.25)
        frames.append(column_text(scr, columns(scr)[0]))
    check("a stopped track's column stays still under a playing cursor track", all(f == before for f in frames), scr)
    check("it shows its note", any("D♭4" in line for line in before), scr)

    # T0 is stopped: Up moves it.
    scr.send(CTRL_LEFT)
    scr.pump(0.4)
    scr.send(DOWN)
    scr.pump(0.4)
    scr.send(DOWN)
    scr.pump(0.4)
    scr.send(UP)
    scr.pump(0.3)
    scr.send(UP)
    scr.pump(0.3)
    before = clip_numbers(scr)[:1]
    for _ in range(4):
        scr.send(UP)
        scr.pump(0.3)
    check("Up moves a stopped track", clip_numbers(scr)[:1] != before, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
