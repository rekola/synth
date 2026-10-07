#!/usr/bin/env python3
"""Drive the clip grid's stop buttons through a pty: kill-region (Del) on
an empty slot removes its stop button - the slot's ⏹ goes - and a second
press has nothing left to delete; toggle-stop-button brings it back."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

DEL = b"\x1b[3~"
SONG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "launchpad_live_test.xml")
results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def first_slot(scr):
    """The clip grid's first slot of the first track (the cursor's), after
    the 30-column outline panel."""
    return scr.dump().splitlines()[2][30:48]


def main():
    pid, fd = vk.spawn(SONG, view="live")
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)
    scr.pump(0.5)
    check("an empty slot shows its stop button", "⏹" in first_slot(scr), scr)

    scr.send(DEL)
    scr.pump(0.5)
    check("kill-region on an empty slot removes its stop button", "⏹" not in first_slot(scr), scr)
    scr.send(DEL)
    scr.pump(0.5)
    check("a second press has nothing left to delete", "⏹" not in first_slot(scr), scr)

    scr.send(b"\x1bx")
    scr.pump(0.4)
    scr.send(b"toggle-stop-button\r")
    scr.pump(0.6)
    check("toggle-stop-button brings it back", "⏹" in first_slot(scr), scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
