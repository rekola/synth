"""Shared driver for the Launchpad (and general keybinding) end-to-end
scripts in this directory: forks the compiled `synth` binary under a
pty and feeds its output through `pyte` so a script can screen-scrape the
pattern editor exactly as a person would see it, without a real terminal.

The pty and screen reading, including answering the terminal's start-up
queries, are cellui's `tools/e2e/ptyscreen.py`; this file adds what is about
synth (the binary, the song, the app's screens).

Not part of the CMake/ctest build on purpose: these scripts drive real
ALSA I/O (via the fake_launchpad_*.c simulators, see README.md), which
`ctest`'s pure-function unit tests deliberately don't touch.
"""
import os
import re
import signal
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO_ROOT, "build", "synth")
SONG = os.path.join(REPO_ROOT, "songs", "demo3.xml")


def cellui_dir():
    """cellui's source: $CELLUI_SRC, the folder cmake was given with
    -DFETCHCONTENT_SOURCE_DIR_CELLUI, or the copy it fetched."""
    if os.environ.get("CELLUI_SRC"):
        return os.environ["CELLUI_SRC"]
    try:
        with open(os.path.join(REPO_ROOT, "build", "CMakeCache.txt")) as f:
            for line in f:
                if line.startswith("FETCHCONTENT_SOURCE_DIR_CELLUI:PATH=") and line.strip().split("=", 1)[1]:
                    return line.strip().split("=", 1)[1]
    except OSError:
        pass
    return os.path.join(REPO_ROOT, "build", "_deps", "cellui-src")


sys.path.insert(0, os.path.join(cellui_dir(), "tools", "e2e"))
import ptyscreen  # noqa: E402  (cellui's pty screen reader; needs pyte)
from ptyscreen import Screen  # noqa: E402,F401  (scripts use harness.Screen)

ROWS, COLS = ptyscreen.ROWS, ptyscreen.COLS


def spawn(song=SONG, view="arrangement"):
    """Starts synth on `song` in a pty. `view` is the --view it starts in:
    Arrangement by default, the view most scripts were written against,
    though the app itself starts in Live View."""
    # A fake_launchpad_*.c simulator registers under the exact same device
    # name a real Launchpad does, so on a machine that also has one
    # physically connected, synth would otherwise auto-connect to both at
    # once - see LaunchpadIO.h's own ignore_hardware_ comment.
    env = {"SYNTH_LAUNCHPAD_NO_HARDWARE": "1"}
    # SYNTH_E2E_NULL_AUDIO=1 runs without a sound device (CI, containers).
    extra = ["--null-audio"] if os.environ.get("SYNTH_E2E_NULL_AUDIO") else []
    # --ui terminal: the default (auto) opens a window where there is a display.
    return ptyscreen.spawn([BINARY, *extra, "--ui", "terminal", "--view", view, song], env=env)


def fake_env():
    """The environment for a simulator that must wait for go() before
    acting - for a script with terminal setup to do first."""
    env = dict(os.environ)
    env["FAKE_WAIT_FOR_GO"] = "1"
    return env


def go(proc):
    """Lets a simulator started with fake_env() begin its sequence."""
    os.kill(proc.pid, signal.SIGUSR1)


def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])


def wait_ready(scr, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        scr.pump(0.3)
        if "voices:" in scr.dump():
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
    info_lines = [l for l in d.splitlines() if "voices:" in l]
    return bool(info_lines) and "▸" in info_lines[-1]


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
    VELOCITY(2)+space+DELAY(2)+space+local FX(3)+space into a fixed
    14-char group per column, followed by the row's own shared EFFECT(4) column (no
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
    while i + 14 <= len(content):
        col = content[i:i + 3]
        # A genuine column is always "···" (undefined), "OFF", or a real
        # note's own text - never dashes: that's only ever a fragment of
        # the row's own trailing EFFECT(4) column, caught here when
        # `content`'s length isn't an exact multiple of 14 (e.g. a
        # shorter trailing divider glyph shifting everything by a
        # character or two). The same goes for a trailing divider glyph
        # ("│") caught in the window.
        if col.strip("-") != "" and "│" not in col:
            cols.append(col)
        i += 14
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


def hide_outline(scr):
    """M-x toggle-outline: hides Live View's outline panel (shown by
    default, on the clip grid's left), so the clip grid starts at the
    screen's left edge - for scripts that read its rows from the start of
    each line."""
    scr.send(b"\x1b")
    scr.pump(0.3)
    scr.send(b"x")
    scr.pump(0.3)
    scr.send(b"toggle-outline\r")
    scr.pump(0.8)
