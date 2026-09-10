"""GridMode::SESSION regression test - the redesigned Launchpad session/
launch view (rows are a track's own pooled patterns, columns are tracks;
CC95/96/97/98 are now the only way in/out, fully decoupled from terminal UI
focus - see LaunchpadManager.h's own GridMode doc comment). Unlike the
retired verify_launchpad_overview.py this replaces, nothing needs to be
clicked/toggled to reach Session view: DeviceState::grid_mode now defaults
to SESSION, so a freshly connected device is already there. This confirms
that default via the CC95/CC97 LED colors sent in the very first LED
refresh, arms Record Arm (CC19) via its own dedicated button, then lets the
simulated Launchpad X press pad (0,0) and checks that the resulting assign
(LaunchpadManager::handleSessionPadEvent) actually placed a real clip
instance (ArrangementOps.h's placeClipInstance()) in the arrangement grid -
not PatternEditor's own row display, which never resolves an instance's
own content (only its section's background pattern, untouched by an assign),
so it wouldn't show this at all."""
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

def arrangement_grid_area(screen):
    # The scope row (ArrangementGrid always occupies its own leftmost
    # columns - see UI::layout()) - rows 1-5, a generous column slice wide
    # enough for the grid regardless of exactly how much space CoverArt
    # claims first.
    return "\n".join(line[:40] for line in screen.display[1:6])

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

grid_before = arrangement_grid_area(scr.screen)
print("arrangement grid before any Launchpad input:")
print(grid_before)
check("No clip instance placed yet (no '7' - clip index 7's own digit - anywhere in the grid)",
      "7" not in grid_before, grid_before)

# fake_launchpad_session presses CC19 (Record Arm) ~6-7s after its own
# startup, then pad (0,0) ~2s after that - poll robustly rather than a
# single fixed sleep.
deadline = time.time() + 15.0
grid_after = grid_before
while time.time() < deadline:
    scr.pump(0.5)
    grid_after = arrangement_grid_area(scr.screen)

print("arrangement grid after simulated Record-Arm + pad (0,0) press:")
print(grid_after)
check("Pad press in Session view (Record Arm on) placed clip index 7's own instance (digit '7') in the arrangement grid",
      "7" in grid_after, grid_after)

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
# NOTES: CC95 (led index 0x5f = 95) bright green (session_mixer_mode
# defaults off - orange means mixer submode instead, see GridMode's own
# comment), CC97 (0x61 = 97, Custom) dim - see LaunchpadManager::
# refreshLeds()'s own Session/Custom LED comment.
check("CC95 (Session) LED is lit by default, with no button pressed yet",
      "03 5f 00 7f 00" in fake_output, fake_output)
check("CC97 (Custom) LED is dim by default (not showing Custom mode active)",
      "03 61 14 00 14" in fake_output, fake_output)

# Record Arm's own LED update (a later SysEx, after CC19 is pressed) isn't
# checked here - the fake device only drains incoming SysEx once, right
# after connecting (see fake_launchpad_button.c's identical precedent for
# its own always-static button LEDs), so it never captures a later one. The
# instance-placement check above already proves note-capture-armed actually
# flipped true - the disarmed (trigger-live) path never places an instance.

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
