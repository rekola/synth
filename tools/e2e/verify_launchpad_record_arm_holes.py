"""Regression test for the per-track Record Arm mechanism's actual
*recording* gesture, end to end - not just the arm/disarm picker
(verify_launchpad_record_arm_picker.py's own job), but a real NOTE-mode
note landing in the exact Session-view clip index that was pressed while
armed. Exercises "holes are allowed" directly: the fixture track starts
with no clips at all, the fake device arms it and presses clip index 2
(not 0), so a correct implementation has to backfill indices 0/1 with
empty fillers (Song::ensureClipAt()) rather than collapsing the take to
whichever slot happens to be first unused - the exact regression this
script exists to catch.

Verifies the result via the terminal SessionView widget itself (opened
with M-x session-view, EscapeSequenceCoalescer folding a bare ESC then
'x' into one Alt-x event the same way a real terminal's own Alt-x would
arrive - see StatusLine.h's own Alt/Meta check): row 0 and row 1 should
show the plain empty-slot stop icon, row 2 should show a real, named
clip with no leftover record indicator once the take is disarmed."""
import sys, os, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []


def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)


fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_holes.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_holes")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_record_arm_holes_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_record_arm_holes's own scripted sequence (6s startup +
# ~11 drains of ~0.5-1s each) takes a bit under 15s - give it comfortable
# margin before reading anything back.
time.sleep(16)
scr.pump(0.5)

# M-x session-view: opens (or switches to) the SessionView aspect of the
# active song - StatusLine's own Alt-x detection is a single check on an
# Alt/Meta-modified 'x' event, which EscapeSequenceCoalescer assembles
# from a bare ESC followed, arbitrarily later, by 'x' (no deadline between
# them - CLAUDE.md's own EscapeCoalescer note), exactly what a raw ESC
# then 'x' byte pair over this pty produces.
scr.send(b"\x1b")
scr.pump(0.3)
scr.send(b"x")
scr.pump(0.3)
scr.send(b"session-view\r")
scr.pump(1.0)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_holes.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_record_arm_holes log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

session_view_text = scr.dump()
print("\n--- SessionView screen dump ---")
print(session_view_text)

lines = session_view_text.splitlines()

# The clip rows this track's own column occupies, found by their own
# leading glyph (SessionView.cpp's own " ⏹"/"▸" icons) as the row's very
# first non-space character - not by a fixed row/column offset (the exact
# screen position depends on window layout), and not by a plain substring
# search either: the status line's own "Octave: ◂ 4 ▸" also contains "▸".
empty_lines = [l for l in lines if l.strip().startswith("⏹")]
check("row 0 (an empty filler backfilled by Song::ensureClipAt()) shows the plain empty-slot icon",
      len(empty_lines) > 0, session_view_text)
check("exactly 7 of the 8 displayed rows are empty (SessionView pads the display to 8 regardless of the underlying clip list's own real length)",
      len(empty_lines) == 7, empty_lines)

populated_lines = [l for l in lines if l.strip().startswith("▸")]
check("exactly one row shows a real, populated clip (not two, and not zero)",
      len(populated_lines) == 1, populated_lines)

if populated_lines:
    check("the populated row is not named '(unnamed)' - Song::ensureClipAt()'s filler got a real name once it received content",
          "(unnamed)" not in populated_lines[0], populated_lines[0])

check("no row still shows the '●' record indicator - the take was disarmed and finalized",
      "●" not in session_view_text, session_view_text)

print(f"\n{sum(1 for _, ok in results if ok)}/{len(results)} checks passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
