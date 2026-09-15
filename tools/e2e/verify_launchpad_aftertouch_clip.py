"""Record-Arm-driven live take, the "Clip-based note recording" path
(Controller::ensureNoteRecordingClip()): switches into NOTES grid mode,
arms Record Arm, holds a note across several rows of real playback, and
sends aftertouch partway through the hold - verifies the pressure actually
becomes visible in the PatternEditor's own velocity column on the row the
transport had reached by the time each aftertouch message arrived, not
just on the row the note itself landed on. A regression here would show as
the velocity column staying "--" on every row but the note-on's own, even
though the note itself is plainly visible."""
import sys, os, subprocess, time, re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []

def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

ROW_RE = re.compile(r"^([0-9a-f]{2})\s")

def dump_pattern_rows(screen):
    """{row_hex: line_text} for every visible line that looks like a
    pattern row (two hex digits, a space, then track content)."""
    rows = {}
    for line in screen.display:
        m = ROW_RE.match(line.strip())
        if m and "│" in line:
            rows[m.group(1)] = line
    return rows

def is_playing(scr):
    d = scr.dump()
    info_lines = [l for l in d.splitlines() if "pattern:" in l]
    return bool(info_lines) and "PLAYING" in info_lines[-1]

def has_note(line):
    """True if a pattern-row line's NOTE column holds a real, named note
    (not the "···" placeholder an empty cell or an aftertouch-only entry
    both show)."""
    return line is not None and line[5:8] not in ("···", "   ")

def has_defined_velocity(line):
    """True if a pattern-row line's VELOCITY column holds a real hex value
    rather than the undefined "--" placeholder."""
    return line is not None and line[9:11] != "--"

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_aftertouch_clip.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_aftertouch_clip")], stderr=fake_log, stdout=fake_log)

# Give the fake device time to register its ALSA client before synth's
# startup-time scan runs (LaunchpadIO does no hotplug yet - it must already
# exist when synth starts).
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# demo3.xml (the harness's default song) has many tracks/voices and hits an
# already-documented, unrelated bug (Space becoming unresponsive under heavy
# playback load, see docs/known_bugs.md) - switch to a fresh, simple new
# song first. There's no standalone new-song command any more - C-x b
# (select-named-buffer, Emacs's own switch-to-buffer) creates a fresh blank
# buffer for a name that isn't already open, same as Emacs.
if is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
scr.send(vk.ctrl('x'))
scr.pump(0.3)
scr.send(b"b")
scr.pump(0.3)
scr.send(b"aftertouchtest\r")
scr.pump(1.0)
check("Playback stopped on the new song", not is_playing(scr))

# A fresh buffer defaults to ArrangementGrid/overview focus, not the
# pattern editor (see CLAUDE.md's own note on this) - switch panes
# (Emacs's own other-window) so the pattern editor is what's actually
# rendering/scrolling below. LaunchpadManager's own input handling is
# UI-focus-agnostic, but this is still what a person would actually be
# looking at.
scr.send(vk.ctrl('x'))
scr.pump(0.3)
scr.send(b"o")
scr.pump(0.5)

rows_before_press = dump_pattern_rows(scr.screen)
print("row 00 before any Launchpad input:", repr(rows_before_press.get("00")))

# fake_launchpad_aftertouch_clip sleeps 6s after its own startup, sends
# CC96 (NOTES grid mode - GridMode defaults to SESSION, where a plain
# note-on would launch a Session View clip slot instead of entering a
# note), then arms Record Arm via a quick CC98 tap a second later, then
# presses the pad.
# Record Arm's own rising edge starts playback immediately (see
# LaunchpadManager.cpp's own comment on why), so this take is the
# "clip-based note recording" path, not step entry.
deadline = time.time() + 15.0
row00_after_press = rows_before_press.get("00")
while time.time() < deadline:
    scr.pump(0.5)
    rows = dump_pattern_rows(scr.screen)
    if rows.get("00") != rows_before_press.get("00"):
        row00_after_press = rows.get("00")
        break
print("row 00 after simulated pad press:       ", repr(row00_after_press))
check("Playback is running (Record Arm's own rising edge should have started it)",
      is_playing(scr))

# The note-on write lands wherever the transport happens to be by the time
# the (fixed-latency) press is actually processed, not necessarily row 00 -
# scan every currently-visible row instead of assuming one fixed position.
deadline = time.time() + 6.0
note_row = None
while time.time() < deadline and note_row is None:
    scr.pump(0.5)
    for row_hex, line in dump_pattern_rows(scr.screen).items():
        if has_note(line):
            note_row = row_hex
            break
print("row the note-on actually landed on:", note_row)
check("Pad press from the simulated Launchpad X entered a note into the pattern",
      note_row is not None)

# fake_launchpad_aftertouch_clip holds 2s between press and its first
# aftertouch, then another 2s before its second - plenty of rows (at the
# default 90bpm/16-rows-per-bar new-song tempo) for the transport to have
# moved well past the note-on's own row by the time either aftertouch
# message arrives. Poll continuously (rather than one fixed sleep) so a
# row showing a real velocity value is caught even if it later scrolls out
# of view.
seen_velocity_rows = []
deadline = time.time() + 9.0
while time.time() < deadline:
    scr.pump(0.5)
    for row_hex, line in dump_pattern_rows(scr.screen).items():
        if row_hex == note_row:
            continue  # the note-on's own row - not what this test is about
        if has_defined_velocity(line) and row_hex not in [r for r, _ in seen_velocity_rows]:
            seen_velocity_rows.append((row_hex, line))

print("rows that showed a real (non-'--') velocity after the note-on's own row:", seen_velocity_rows)
check("At least one aftertouch message became visible as a real velocity value on a row other than the note-on's own",
      len(seen_velocity_rows) > 0, seen_velocity_rows)
check("Both aftertouch messages became visible as real velocity values on distinct rows",
      len(seen_velocity_rows) >= 2, seen_velocity_rows)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

fake.wait(timeout=5)
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_aftertouch_clip.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_aftertouch_clip log ---")
print(fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
