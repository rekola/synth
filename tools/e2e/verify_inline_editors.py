#!/usr/bin/env python3
"""Drive every widget's inline text editor (InlineEditor) through a pty:
ArrangementGrid's section rename, PatternEditor's track-name and
locator editors, and ClipGrid's clip and track renames. Checks that
Enter commits, Ctrl-g cancels, M-x cancels an open editor, and that a
typed space never reaches the global toggle-playing binding.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

F2 = b"\x1bOQ"
UP, DOWN, CTRL_RIGHT = b"\x1b[A", b"\x1b[B", b"\x1b[1;5C"
SESSION_SONG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "launchpad_session_test.xml")

results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def type_text(scr, text):
    scr.send(text.encode())
    scr.pump(0.4)


def start(song=vk.SONG):
    pid, fd = vk.spawn(song)
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        print(scr.dump())
        os.kill(pid, 9)
        sys.exit(2)
    return pid, scr


def arrangement_and_pattern_editor():
    pid, scr = start()

    # Initial focus is the ArrangementGrid, its cursor on a section title row.
    scr.send(b"\r")
    scr.pump(0.4)
    type_text(scr, "Intro X")
    scr.send(b"\r")
    scr.pump(0.5)
    check("section rename commits", "Intro X" in scr.dump(), scr)
    check("space in section name does not start playback", not vk.is_playing(scr), scr)

    vk.other_window(scr)
    scr.send(F2)
    scr.pump(0.4)
    type_text(scr, "Zed Q")
    scr.send(b"\r")
    scr.pump(0.5)
    check("track rename commits", "T0 Zed Q" in scr.dump(), scr)
    check("space in track name does not start playback", not vk.is_playing(scr), scr)

    scr.send(F2)
    scr.pump(0.4)
    type_text(scr, "NOPE")
    scr.send(vk.ctrl('g'))
    scr.pump(0.5)
    d = scr.dump()
    check("Ctrl-g cancels track rename", "NOPE" not in d and "T0 Zed Q" in d, scr)

    scr.send(F2)
    scr.pump(0.4)
    type_text(scr, "ALSO")
    scr.send(b"\x1bx")
    scr.pump(0.5)
    check("M-x cancels an open track rename", "ALSO" not in scr.dump() and "M-x" in scr.dump(), scr)
    scr.send(vk.ctrl('g'))
    scr.pump(0.5)

    for _ in range(10):
        scr.send(CTRL_RIGHT)
        scr.pump(0.15)
    scr.send(b"\r")
    scr.pump(0.4)
    type_text(scr, "hello note")
    scr.send(b"\r")
    scr.pump(0.5)
    check("locator edit commits", "hello note" in scr.dump(), scr)

    os.kill(pid, 9)


def clip_grid():
    pid, scr = start(SESSION_SONG)
    scr.send(b"\x1bx")
    scr.pump(0.4)
    scr.send(b"session-view\r")
    scr.pump(0.8)

    # The fixture's only clip ("target") sits on the last of the 8 clip rows.
    for _ in range(7):
        scr.send(DOWN)
        scr.pump(0.15)
    scr.send(F2)
    scr.pump(0.4)
    type_text(scr, " B")
    scr.send(b"\r")
    scr.pump(0.5)
    check("clip rename commits", "target B" in scr.dump(), scr)
    check("space in clip name does not start playback", not vk.is_playing(scr), scr)

    for _ in range(8):
        scr.send(UP)
        scr.pump(0.15)
    scr.send(F2)
    scr.pump(0.4)
    type_text(scr, "Bass Y")
    scr.send(b"\r")
    scr.pump(0.5)
    check("session track rename commits", "T0 Bass Y" in scr.dump(), scr)

    os.kill(pid, 9)


def main():
    arrangement_and_pattern_editor()
    clip_grid()
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
