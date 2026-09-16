"""Regression test for CC91 ("move-row-up") as a held shift modifier:
LaunchpadManager::handleShiftButton()/handleSessionPadEvent()'s own new
combo - holding CC91 and pressing a Session-view pad opens that pad's own
clip for direct step-grid editing (Controller::toggleDrumClipFocus())
instead of triggering/assigning it, and the same combo on the same pad
again closes it. Verified through the terminal SessionView widget's own
text (the "*" focus marker SessionView.cpp already draws on whichever row
is open for editing, Controller::getFocusedClip()), the same mechanism
verify_launchpad_record_arm_holes.py uses - this gesture never touches
real audio/ALSA capture at all (pure Song/Controller state), so it isn't
expected to hit the sandboxed-environment LED-read flakiness documented
in docs/known_bugs.md the way a SampleTrack's own real-audio-capture arm
does.

Two mid-run screen reads against one spawn (fake_launchpad_shift_stepgrid.c
paces its own two phases with a generous internal wait in between) rather
than two separate spawns: unlike the SampleTrack record-arm test, nothing
here needs a fully-settled disarm before it can be safely read - opening
and closing are both plain, synchronous Controller state changes."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_shift_stepgrid_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_shift_stepgrid's own phase 1 (6s startup + ~2.1s of
# drains) settles a bit past 8s - give it comfortable margin before the
# first read, but not so much it strays into phase 2's own 6s waiting
# window (which starts right after phase 1 settles, ending around 14.1s).
time.sleep(10)
scr.pump(0.5)

# M-x session-view: opens (or switches to) the SessionView aspect of the
# active song - same mechanism verify_launchpad_record_arm_holes.py
# already uses.
scr.send(b"\x1b")
scr.pump(0.3)
scr.send(b"x")
scr.pump(0.3)
scr.send(b"session-view\r")
scr.pump(1.0)

phase1_text = scr.dump()
print("\n--- SessionView screen dump (phase 1 - opened) ---")
print(phase1_text)

# fake_launchpad_shift_stepgrid's own phase 2 starts about 4s after phase
# 1 settled and takes another ~1.6s - give it comfortable margin before
# the second read.
time.sleep(8)
scr.pump(0.5)

phase2_text = scr.dump()
print("\n--- SessionView screen dump (phase 2 - closed) ---")
print(phase2_text)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_shift_stepgrid log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

phase1_lines = [l for l in phase1_text.splitlines() if "Beat 1" in l]
check("phase 1: the 'Beat 1' row shows the '*' focus marker once shift+pad opened it",
      len(phase1_lines) == 1 and "*" in phase1_lines[0], phase1_text)

phase2_lines = [l for l in phase2_text.splitlines() if "Beat 1" in l]
check("phase 2: the '*' focus marker is gone once the same shift+pad combo closed it again",
      len(phase2_lines) == 1 and "*" not in phase2_lines[0], phase2_text)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
