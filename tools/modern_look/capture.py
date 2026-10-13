#!/usr/bin/env python3
"""Captures the terminal UI's screens as cells (character, colours, style)
through the e2e harness's pty and pyte, for the modern-look audit.

    python3 tools/modern_look/capture.py OUTDIR [song.xml]

Writes OUTDIR/<screen>.json (every cell), OUTDIR/<screen>.txt (the text) and
OUTDIR/summary.json (colours, glyphs and line characters per screen)."""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "e2e"))
import harness  # noqa: E402

BOX = set("─│┌┐└┘├┤┬┴┼═║╔╗╚╝╠╣╦╩╬╭╮╰╯┃━┏┓┗┛▏▕")
BLOCKS = set("▀▄█▌▐░▒▓▖▗▘▝▚▞▙▛▜▟")
BRAILLE = {chr(c) for c in range(0x2800, 0x2900)}


def spawn(song, view):
    """harness.spawn() with --null-audio, so no sound card is needed."""
    import pty
    pid, master_fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"; os.environ["COLORTERM"] = "truecolor"
        os.environ["SYNTH_LAUNCHPAD_NO_HARDWARE"] = "1"
        os.execvp(harness.BINARY, [harness.BINARY, "--null-audio", "--view", view, song])
        os._exit(1)
    harness.set_winsize(master_fd, harness.ROWS, harness.COLS, harness.XPIX, harness.YPIX)
    os.set_blocking(master_fd, False)
    return pid, master_fd


def cells(scr):
    rows = []
    for y in range(harness.ROWS):
        line = scr.screen.buffer[y]
        row = []
        for x in range(harness.COLS):
            c = line[x]
            row.append([c.data, c.fg, c.bg, int(c.bold), int(c.italics), int(c.underscore), int(c.reverse)])
        rows.append(row)
    return rows


def summarize(rows):
    fg, bg, pairs, glyphs = (collections.Counter() for _ in range(4))
    box = block = braille = 0
    for row in rows:
        for ch, f, b, bold, ital, und, rev in row:
            fg[f] += 1
            bg[b] += 1
            pairs[(f, b)] += 1
            if ch.strip():
                glyphs[ch] += 1
            box += ch in BOX
            block += ch in BLOCKS
            braille += ch in BRAILLE
    return {
        "fg": fg.most_common(), "bg": bg.most_common(),
        "pairs": [[list(k), v] for k, v in pairs.most_common(40)],
        "glyphs": glyphs.most_common(),
        "box_drawing_cells": box, "block_cells": block, "braille_cells": braille,
    }


def main():
    out = sys.argv[1]
    song = sys.argv[2] if len(sys.argv) > 2 else harness.SONG
    os.makedirs(out, exist_ok=True)
    summary = {}

    def grab(name, scr):
        rows = cells(scr)
        json.dump(rows, open(os.path.join(out, name + ".json"), "w"))
        open(os.path.join(out, name + ".txt"), "w").write(scr.dump() + "\n")
        summary[name] = summarize(rows)

    for view in ("live", "arrangement"):
        pid, fd = spawn(song, view)
        scr = harness.Screen(fd)
        if not harness.wait_ready(scr, 15.0):
            scr.wait(2.0)
        scr.wait(1.0)
        grab(view, scr)
        if view == "live":
            scr.send(b"\x1bx")                  # M-x minibuffer
            scr.wait(0.6)
            grab("live_minibuffer", scr)
            scr.send(b"\x07")                   # C-g
            scr.wait(0.3)
        os.kill(pid, 15)
        os.waitpid(pid, 0)
    json.dump(summary, open(os.path.join(out, "summary.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
