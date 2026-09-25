#!/usr/bin/env python3
"""Drive PatternEditor's session mode (Session view) through a pty:
annotations exist only in Arrangement view, typing a note into an empty
slot creates a clip there (and not in the arrangement), launching that
clip from the clip grid starts the transport and moves a playhead in its
own track's column only, and Space is the transport in Session view too:
stopping it stops the launched clip, which stays stopped when the
transport starts again.
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


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)

    # An annotation in Arrangement view.
    vk.other_window(scr)
    for _ in range(12):
        scr.send(CTRL_RIGHT)
        scr.pump(0.1)
    scr.send(b"\r")
    scr.pump(0.4)
    scr.send(b"hello note\r")
    scr.pump(0.5)
    check("annotation shows in Arrangement view", "hello note" in scr.dump(), scr)

    # Session view: clip grid on top, pattern editor below, no annotations.
    scr.send(b"\t")
    scr.pump(0.8)
    check("Session view hides annotations", "hello note" not in scr.dump(), scr)

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
    top = first_pattern_row(scr)
    # playhead_tint_color over a plain row and over a bar row.
    PLAYHEAD_BG = {"245361", "3e6d7b"}
    playing_t0 = playing_t1 = tinted_divider = False
    for _ in range(12):
        scr.pump(0.2)
        for r in range(4):
            t0 = scr.screen.buffer[top + r][6].bg
            t1 = scr.screen.buffer[top + r][22].bg
            divider = scr.screen.buffer[top + r][20].bg  # the "│" between T0 and T1
            playing_t0 |= t0 in PLAYHEAD_BG
            playing_t1 |= t1 in PLAYHEAD_BG
            tinted_divider |= t0 in PLAYHEAD_BG and divider in PLAYHEAD_BG
    check("the launched clip's playhead shows in its own track's column", playing_t0, scr)
    check("no playhead in a track with nothing launched", not playing_t1, scr)
    check("the playhead stops short of the divider between tracks", playing_t0 and not tinted_divider, scr)

    # Launching the playing clip again relaunches it at the next bar - a
    # launch never toggles - so past that bar its playhead still moves.
    scr.send(b"\r")
    scr.wait(3.0)  # past the next bar
    still_playing = False
    for _ in range(8):
        scr.pump(0.2)
        still_playing |= any(scr.screen.buffer[top + r][6].bg in PLAYHEAD_BG for r in range(4))
    check("launching the playing clip again restarts it rather than stopping it", still_playing, scr)

    # Space is the transport in Session view too: the launch started it,
    # and stopping it stops the launched clip.
    other_window(scr)  # clip grid -> pattern editor
    check("launching started the transport", vk.is_playing(scr), scr)
    check("the clip grid marks the launched track as taken over", "◆" in scr.dump(), scr)
    scr.send(b" ")
    scr.pump(0.6)
    check("Space in Session view stops the transport", not vk.is_playing(scr), scr)
    scr.send(b" ")
    scr.pump(0.6)
    check("and starts it again", vk.is_playing(scr), scr)
    moving = False
    for _ in range(8):
        scr.pump(0.2)
        moving |= any(scr.screen.buffer[top + r][6].bg in PLAYHEAD_BG for r in range(4))
    check("the stopped clip stays stopped", not moving, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
