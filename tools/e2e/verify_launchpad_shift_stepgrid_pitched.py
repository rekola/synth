"""Regression test for the shift+pad "open for editing" gesture on a
*pitched* InstrumentTrack's own clip - Controller::toggleDrumClipFocus()
no longer gates on percussion at all (its own doc comment), so this
should open the step grid exactly the same way it already does for a
PercussionTrack (verify_launchpad_shift_stepgrid.py, its own sibling
script - same mechanism, same "*" focus marker verification), just with
rows drawn from the song's own scale (Song::getScaleDegrees(),
LaunchpadManager::resolveStepGridLaneNotes()) instead of a manually-
picked lane list. This gesture never touches real audio/ALSA capture at
all (pure Song/Controller state), so it isn't expected to hit the
sandboxed-environment LED-read flakiness documented in docs/known_bugs.md
- verified entirely through the terminal ClipGrid widget's own text.

Two mid-run screen reads against one spawn, same structure as
verify_launchpad_shift_stepgrid.py - see its own docstring for why."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid_pitched.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid_pitched")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_shift_stepgrid_pitched_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

time.sleep(10)
scr.pump(0.5)

scr.send(b"\x1b")
scr.pump(0.3)
scr.send(b"x")
scr.pump(0.3)
scr.send(b"session-view\r")
scr.pump(1.0)

phase1_text = scr.dump()
print("\n--- ClipGrid screen dump (phase 1 - opened) ---")
print(phase1_text)

time.sleep(8)
scr.pump(0.5)

phase2_text = scr.dump()
print("\n--- ClipGrid screen dump (phase 2 - closed) ---")
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_stepgrid_pitched.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_shift_stepgrid_pitched log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

phase1_lines = [l for l in phase1_text.splitlines() if "Beat 1" in l]
check("phase 1: the 'Beat 1' row shows the '*' focus marker once shift+pad opened it (pitched track)",
      len(phase1_lines) == 1 and "*" in phase1_lines[0], phase1_text)

phase2_lines = [l for l in phase2_text.splitlines() if "Beat 1" in l]
check("phase 2: the '*' focus marker is gone once a lone CC95 press closed it again",
      len(phase2_lines) == 1 and "*" not in phase2_lines[0], phase2_text)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
