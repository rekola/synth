"""GridMode::SESSION regression test - the redesigned Launchpad session/
launch view (rows are a track's own pooled patterns, columns are tracks;
CC95/96/97 are now the only way in/out, fully decoupled from terminal UI
focus - see LaunchpadManager.h's own GridMode doc comment). Unlike the
retired verify_launchpad_overview.py this replaces, nothing needs to be
clicked/toggled to reach Session view: DeviceState::grid_mode now defaults
to SESSION, so a freshly connected device is already there. This confirms
that default via the CC95/CC97 LED colors sent in the very first LED
refresh, arms Record Arm (CC19) via its own dedicated button, then lets the
simulated Launchpad X press pad (0,0) and checks that the resulting assign
(LaunchpadManager::handleSessionPadEvent) actually copied the pool's own
pattern content into the current scene."""
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

def find_pattern_row(screen, target="00"):
    for y in range(screen.lines):
        line = screen.display[y]
        if line.strip().startswith(target) and "│" in line:
            return y
    return None

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_session.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_session")], stderr=fake_log, stdout=fake_log)

# Same startup ordering as verify_launchpad_e2e.py - the fake device must
# already exist as an ALSA client before synth's startup-time scan runs.
time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

y = find_pattern_row(scr.screen)
line_before = scr.screen.display[y]
print("row 00 before any Launchpad input:", repr(line_before))
check("Scene starts with the placeholder note (C-4)", "C-4" in line_before, line_before)

# fake_launchpad_session presses CC19 (Record Arm) ~6-7s after its own
# startup, then pad (0,0) ~2s after that - poll robustly rather than a
# single fixed sleep.
deadline = time.time() + 15.0
line_after = line_before
while time.time() < deadline:
    scr.pump(0.5)
    y2 = find_pattern_row(scr.screen)
    if y2 is not None:
        line_after = scr.screen.display[y2]

print("row 00 after simulated Record-Arm + pad (0,0) press:", repr(line_after))
check("Pad press in Session view (Record Arm on) assigned pool index 7's own pattern (E-4) into the current scene",
      "E-4" in line_after, line_after)
check("The old placeholder note (C-4) is gone - a real assign, not a note added alongside it",
      "C-4" not in line_after, line_after)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

fake.terminate()
fake.wait(timeout=5)
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_session.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_session log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

# LED colors sent in the very first refresh (before any button/pad input at
# all) - proves DeviceState::grid_mode now defaults to SESSION rather than
# NOTES: CC95 (led index 0x5f = 95) bright, CC97 (0x61 = 97, Custom/DRAW)
# dim - see LaunchpadManager::refreshLeds()'s own Session/Custom LED
# comment.
check("CC95 (Session) LED is lit by default, with no button pressed yet",
      "03 5f 5a 7f 00" in fake_output, fake_output)
check("CC97 (Custom/DRAW) LED is dim by default (not showing DRAW mode active)",
      "03 61 14 00 14" in fake_output, fake_output)

# Record Arm's own LED update (a later SysEx, after CC19 is pressed) isn't
# checked here - the fake device only drains incoming SysEx once, right
# after connecting (see fake_launchpad_button.c's identical precedent for
# its own always-static button LEDs), so it never captures a later one. The
# E-4 assign check above already proves capture_enabled_ actually flipped
# true - a plain command couldn't have written into the scene otherwise.

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
