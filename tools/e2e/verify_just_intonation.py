#!/usr/bin/env python3
"""Drive the whole-song select and the just-intonation commands through a pty
(fixture: just_intonation_test.xml - 31-EDO in C, notes C, D#, F, G on the
first four rows). Select-all marks the whole song; applying just intonation
writes each note's correction into its fx column (a note that needs none gets
+00); clearing takes them off again; transposing the whole song moves the
key with the notes, so the corrections stay as they were.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "just_intonation_test.xml")
results = []


def check(name, ok, scr):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok:
        print(scr.dump())


def fx_column(scr):
    """The local fx text of the first four rows."""
    out = []
    for line in scr.dump().splitlines():
        m = re.match(r"^ (0[0-3]) │(\S{3}) (\S\S) (\S\S) (\S{3}) ", line)
        if m:
            out.append(m.group(5))
    return out


def notes(scr):
    out = []
    for line in scr.dump().splitlines():
        m = re.match(r"^ (0[0-3]) │(\S{3}) ", line)
        if m:
            out.append(m.group(2))
    return out


def mx(scr, command):
    scr.send(b"\x1bx")
    scr.pump(0.3)
    scr.send(command.encode() + b"\r")
    scr.pump(0.8)


def main():
    pid, fd = vk.spawn(FIXTURE)
    scr = vk.Screen(fd)
    vk.wait_ready(scr)
    vk.other_window(scr)  # the pattern editor
    check("nothing is tuned to begin with", fx_column(scr) == ["···"] * 4, scr)

    scr.send(vk.ctrl('x'))
    scr.pump(0.3)
    scr.send(b"h")
    scr.pump(0.5)
    check("C-x h selects the whole arrangement", "Whole arrangement selected" in scr.dump(), scr)

    mx(scr, "apply-just-intonation-region")
    check("it tuned every note", "Tuned 4 notes" in scr.dump(), scr)
    check("the tonic needs nothing, the others their correction",
          fx_column(scr) == ["+00", "-04", "-05", "+05"], scr)

    mx(scr, "clear-tuning-correction-region")
    check("clearing takes them off", fx_column(scr) == ["···"] * 4, scr)

    mx(scr, "apply-just-intonation-region")
    before = notes(scr)
    key_line = [l for l in scr.dump().splitlines() if "31edo" in l]
    mx(scr, "transpose-region-up")
    after = notes(scr)
    check("transposing moves every note", after != before and before[0] == "C-4", scr)
    check("and the corrections stay as they were", fx_column(scr) == ["+00", "-04", "-05", "+05"], scr)
    key_line_after = [l for l in scr.dump().splitlines() if "31edo" in l]
    check("because the key moved with the notes", key_line and key_line_after and key_line != key_line_after, scr)

    scr.send(vk.ctrl('g'))
    scr.pump(0.5)
    check("C-g cancels the selection", "Selection cancelled" in scr.dump(), scr)
    mx(scr, "kill-region")
    check("with a single note selected again, kill acts on that note only", fx_column(scr).count("···") == 1, scr)

    os.kill(pid, 9)
    sys.exit(0 if all(results) else 1)


main()
