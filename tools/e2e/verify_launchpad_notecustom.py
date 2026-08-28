"""Regression test for two Launchpad LED/mode-switch fixes:

1. Note (CC96) previously had no active-state LED at all (always the
   same static dim white) - it now lights up (same convention as
   Session/Custom) once GridMode::NOTES is actually selected.
2. Custom/DRAW (CC97) previously only switched GridMode on *release* -
   it now switches immediately on press (LaunchpadManager::
   handleDrawToggleButton()), matching CC95's own instant Session
   switch. Verified by confirming DRAW's own bright LED arrives while
   CC97 is still held down, before any release is ever sent."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_notecustom.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_notecustom")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_notecustom's whole scripted sequence (6s startup delay,
# CC96 press/release, CC97 press + up to 1s actively drained before its
# own release) takes a bit over 8s - give it comfortable margin.
time.sleep(11)
scr.pump(0.5)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_notecustom.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_notecustom log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

# CC96 (led index 0x60) bright white once NOTES is actually selected -
# same (90,90,90) active-state color Session/Custom already use.
check("CC96 (Note) LED lit up once NOTES mode was actually selected",
      "60 5a 5a 5a" in fake_output, fake_output)

# CC97 (led index 0x61) bright (0x5a 0x00 0x7f) - must appear inside a
# "received sysex while CC97 still held" block, i.e. before the fake
# device ever sent CC97's release.
held_section_start = fake_output.find("sending CC97 press")
release_marker = fake_output.find("sending CC97 release")
held_section = fake_output[held_section_start:release_marker] if held_section_start >= 0 and release_marker > held_section_start else ""
check("DRAW mode's own LED (CC97) lit up while the button was still held, before release",
      "61 5a 00 7f" in held_section, held_section or fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
