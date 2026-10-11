#!/usr/bin/env python3
"""Drive set-song-scale through a pty: M-x opens the scale list, choosing
Otonal says so on the status line, and saving writes it to the song file.
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

DOWN = b"\x1b[B"

results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def main():
    work = tempfile.mkdtemp()
    song = os.path.join(work, "scale.xml")
    shutil.copy(vk.SONG, song)
    with open(song, encoding="utf-8") as f:
        assert 'scale="' not in f.read(), "the test song must start with no scale"

    pid, fd = vk.spawn(song)
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        print(scr.dump())
        os.kill(pid, 9)
        sys.exit(2)

    scr.send(b"\x1bx")
    scr.pump(0.4)
    scr.send(b"set-song-scale\r")
    scr.pump(0.6)
    d = scr.dump()
    check("set-song-scale lists the scales", all(s in d for s in ("None (chromatic)", "Major", "Minor", "Otonal", "Utonal")), scr)

    # The list starts on the scale in use, None.
    for _ in range(3):
        scr.send(DOWN)
        scr.pump(0.2)
    scr.send(b"\r")
    scr.pump(0.6)
    check("choosing Otonal says so", "Scale Otonal" in scr.dump(), scr)

    scr.send(vk.ctrl('x'))
    scr.pump(0.3)
    scr.send(vk.ctrl('s'))
    scr.pump(1.0)
    with open(song, encoding="utf-8") as f:
        saved = f.read()
    check("the saved song has the scale", 'scale="otonal"' in saved, scr)

    os.kill(pid, 9)
    shutil.rmtree(work)
    print(f"{sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
