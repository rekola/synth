#!/usr/bin/env python3
"""Drive set-song-tuning through a pty: M-x opens the tuning list, choosing
53-EDO converts the 31-EDO test song and says so, and saving writes the new
tuning with every note on the step nearest its old pitch: here, the same names.
"""
import os
import re
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


def notes(xml):
    return re.findall(r'<note [^>]*>([^<]*)</note>', xml)


def save(scr, song):
    scr.send(vk.ctrl('x'))
    scr.pump(0.3)
    scr.send(vk.ctrl('s'))
    scr.pump(1.0)
    with open(song, encoding="utf-8") as f:
        return f.read()


def main():
    work = tempfile.mkdtemp()
    song = os.path.join(work, "tuning.xml")
    shutil.copy(vk.SONG, song)
    with open(song, encoding="utf-8") as f:
        assert 'temperament="31edo"' in f.read(), "the test song must be in 31-EDO"

    pid, fd = vk.spawn(song)
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        print(scr.dump())
        os.kill(pid, 9)
        sys.exit(2)

    original = save(scr, song) # in the format this build writes
    scr.send(b"\x1bx")
    scr.pump(0.4)
    scr.send(b"set-song-tuning\r")
    scr.pump(0.6)
    d = scr.dump()
    check("set-song-tuning lists the tunings", all(t in d for t in ("12-EDO", "19-EDO", "31-EDO", "53-EDO")), scr)

    # The list starts on the tuning in use, 31-EDO.
    scr.send(DOWN)
    scr.pump(0.2)
    scr.send(b"\r")
    scr.pump(0.6)
    check("choosing 53-EDO says so", "Tuning 53-EDO" in scr.dump(), scr)

    saved = save(scr, song)
    check("the saved song is in 53-EDO", 'temperament="53edo"' in saved, scr)
    changed = [(a, b) for a, b in zip(notes(original), notes(saved)) if a != b]
    check("its notes keep their names", notes(saved) and len(notes(saved)) == len(notes(original)) and not changed, scr)
    if changed:
        print("changed:", sorted(set(changed)))

    os.kill(pid, 9)
    shutil.rmtree(work)
    print(f"{sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
