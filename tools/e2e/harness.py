"""Shared driver for the Launchpad (and general keybinding) end-to-end
scripts in this directory: forks the compiled `synth` binary under a
pty and feeds its output through `pyte` so a script can screen-scrape the
pattern editor exactly as a person would see it, without a real terminal.

The only genuinely fiddly part is `Screen._respond`: notcurses probes an
interactive terminal for its capabilities on startup (cursor position,
pixel geometry, Kitty keyboard protocol support, etc.) and will hang
waiting for a reply that a plain pty never sends on its own - `_respond`
answers each of those queries with a plausible canned reply as soon as it
sees one go out, exactly as a real terminal would, so `synth` starts
up normally instead of stalling.

Not part of the CMake/ctest build on purpose: these scripts drive real
ALSA I/O (via the fake_launchpad_*.c simulators, see README.md), which
`ctest`'s pure-function unit tests deliberately don't touch.
"""
import fcntl
import os
import pty
import re
import select
import struct
import sys
import termios
import time

import pyte

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO_ROOT, "build", "synth")
SONG = os.path.join(REPO_ROOT, "songs", "demo3.xml")

ROWS, COLS = 40, 120
XPIX, YPIX = 960, 1200


def set_winsize(fd, rows, cols, xpix, ypix):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, xpix, ypix))


def spawn(song=SONG):
    pid, master_fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execvp(BINARY, [BINARY, song])
        os._exit(1)
    set_winsize(master_fd, ROWS, COLS, XPIX, YPIX)
    os.set_blocking(master_fd, False)
    return pid, master_fd


class Screen:
    def __init__(self, fd):
        self.fd = fd
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.Stream(self.screen)
        self._respond_buf = b""

    def _respond(self, data):
        buf = self._respond_buf + data
        if re.search(rb"\x1b\[6n", buf):
            os.write(self.fd, f"\x1b[{ROWS // 2};{COLS // 2}R".encode())
            buf = re.sub(rb"\x1b\[6n", b"", buf)
        if re.search(rb"\x1b\[14t", buf):
            os.write(self.fd, f"\x1b[4;{YPIX};{XPIX}t".encode())
            buf = re.sub(rb"\x1b\[14t", b"", buf)
        if re.search(rb"\x1b\[18t", buf):
            os.write(self.fd, f"\x1b[8;{ROWS};{COLS}t".encode())
            buf = re.sub(rb"\x1b\[18t", b"", buf)
        if re.search(rb"\x1b\[>c", buf):
            os.write(self.fd, b"\x1b[>1;10;0c")
            buf = re.sub(rb"\x1b\[>c", b"", buf)
        if re.search(rb"\x1b\[c", buf):
            os.write(self.fd, b"\x1b[?62;22c")
            buf = re.sub(rb"\x1b\[c", b"", buf)
        if re.search(rb"\x1b\[\?u", buf):
            os.write(self.fd, b"\x1b[?0u")
            buf = re.sub(rb"\x1b\[\?u", b"", buf)
        for mode in (b"2026", b"1016"):
            pat = rb"\x1b\[\?" + mode + rb"\$p"
            if re.search(pat, buf):
                os.write(self.fd, b"\x1b[?" + mode + b";0$y")
                buf = re.sub(pat, b"", buf)
        if re.search(rb"\x1bP\+q[0-9a-f;]*\x1b\\", buf):
            os.write(self.fd, b"\x1bP0+r\x1b\\")
            buf = re.sub(rb"\x1bP\+q[0-9a-f;]*\x1b\\", b"", buf)
        buf = re.sub(rb"\x1b\]4;\d+;\?\x1b\\", b"", buf)
        buf = re.sub(rb"\x1b\]1[01];\?\x1b\\", b"", buf)
        buf = re.sub(rb"\x1b_Gi=1,a=q;\x1b\\", b"", buf)
        self._respond_buf = buf

    def pump(self, timeout=0.3, settle=0.08):
        # `end` is extended by `settle` on every read so a quiet period ends
        # the pump early - but during active playback the app produces
        # steady ~20-25ms redraw traffic, which would push `end` forward
        # forever. Cap the total wall-clock time unconditionally so pump()
        # always returns within `timeout` regardless of how busy the app is.
        hard_deadline = time.time() + timeout
        end = hard_deadline
        while time.time() < end and time.time() < hard_deadline:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if self.fd in r:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    break
                if not data:
                    break
                self._respond(data)
                self.stream.feed(data.decode("utf-8", errors="ignore"))
                end = min(time.time() + settle, hard_deadline)

    def dump(self):
        return "\n".join(self.screen.display)

    def send(self, b):
        os.write(self.fd, b)


def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])


def wait_ready(scr, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        scr.pump(0.3)
        if "pattern:" in scr.dump():
            return True
    return False


def new_buffer(scr, name):
    """Switches to a fresh, empty buffer named `name` via C-x b
    (select-named-buffer, Emacs's own switch-to-buffer) - there's no
    standalone new-song/Ctrl-N command any more. switchToBuffer() creates
    a fresh blank buffer for a name that isn't already open, same as
    Emacs's own switch-to-buffer, so this doubles as "New" too."""
    scr.send(ctrl('x'))
    scr.pump(0.3)
    scr.send(b"b")
    scr.pump(0.3)
    scr.send(name.encode() + b"\r")
    scr.pump(1.0)


def is_playing(scr):
    d = scr.dump()
    info_lines = [l for l in d.splitlines() if "pattern:" in l]
    return bool(info_lines) and "PLAYING" in info_lines[-1]


_ROW_RE = re.compile(r"^([0-9a-f]{2})\s")


def dump_pattern_rows(scr):
    """{row_hex: line_text} for every currently visible pattern-editor row
    (two hex digits, a space, then track content) - a Launchpad take's
    own row isn't knowable in advance (it depends on real wall-clock/
    audio-thread timing once Record Arm has started playback), so callers
    scan whatever's visible rather than assuming a fixed row."""
    rows = {}
    for line in scr.screen.display:
        m = _ROW_RE.match(line.strip())
        if m and "│" in line:
            rows[m.group(1)] = line
    return rows


def has_note(line):
    """True if a pattern-row line's NOTE column holds a real, named note
    (not the "···" placeholder an empty cell or an aftertouch-only entry
    both show)."""
    return line is not None and line[5:8] not in ("···", "   ")


def has_off(line):
    """True if a pattern-row line's NOTE column holds the OFF sentinel."""
    return line is not None and line[5:8] == "OFF"


def has_defined_velocity(line):
    """True if a pattern-row line's VELOCITY column holds a real hex value
    rather than the undefined "--" placeholder."""
    return line is not None and line[9:11] != "--"


def note_columns(line):
    """Every note sub-column's own 3-char NOTE text on a pattern-row
    line, in order - a chord/multi-voice track packs NOTE(3)+space+
    VELOCITY(2)+space+DELAY(2)+space into a fixed 10-char group per
    column, followed by the row's own shared EFFECT(4) column (no
    trailing group of its own, so the walk below stops there
    naturally). Position-based rather than matching against note-name
    text, since a note's own accidental glyph varies by tuning (31-EDO's
    own double-sharp/double-flat symbols included) - has_note()'s own
    single-column equivalent, generalized to however many columns a
    track actually has."""
    if "│" not in line:
        return []
    content = line[line.index("│") + 1:]
    cols = []
    i = 0
    while i + 10 <= len(content):
        col = content[i:i + 3]
        # A genuine column is always "···" (undefined), "OFF", or a real
        # note's own text - never dashes: that's only ever a fragment of
        # the row's own trailing EFFECT(4) column, caught here when
        # `content`'s length isn't an exact multiple of 10 (e.g. a
        # shorter trailing divider glyph shifting everything by a
        # character or two).
        if col.strip("-") != "":
            cols.append(col)
        i += 10
    return cols


def other_window(scr):
    """C-x o (Emacs's own other-window): toggles focus between the
    pattern editor and the arrangement/overview grid. A fresh buffer (see
    new_buffer() above) defaults to overview focus, not the pattern
    editor - keystrokes PatternEditor itself handles (note entry, mark/
    kill/yank, ...) need this first; Launchpad input is unaffected either
    way (LaunchpadManager is deliberately UI-focus-agnostic)."""
    scr.send(ctrl('x'))
    scr.pump(0.3)
    scr.send(b"o")
    scr.pump(0.5)
