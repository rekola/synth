"""Drum-machine step-grid regression test (plans/drum-machine.md, Phase 5):
navigating onto a DrumMachineTrack must switch the Launchpad's grid to the
step-grid surface automatically (no mode toggle), and a pad press there
must toggle that lane/step immediately, reflected in the LED colors sent
back to the device - not silently fall through to ordinary NOTES-mode
chord entry.

The second check (press -> new LED frame reflecting the toggle) is
currently failing in at least one sandboxed test environment for reasons
unrelated to this feature - see docs/known_bugs.md's entry on
verify_launchpad_e2e.py, whose pre-existing, unmodified pad-press checks
fail the identical way in the same environment. Re-check this script
specifically (not just known_bugs.md's entry) before assuming a future
failure here is the same known issue rather than a real regression."""
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

def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])

SONG = os.path.join(SCRIPT_DIR, "drum_machine_stepgrid_test.xml")

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_stepseq.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_stepseq")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# The test song's only track is the DrumMachineTrack - Ctrl-E ("last
# track") lands the cursor there regardless of the default cursor
# position, mirroring verify_percussion_layout.py's own navigation.
# The simulator waits 8s after its own startup before pressing, then 2s
# more before releasing - stay well past both.
scr.pump(2.0)
scr.send(ctrl('e'))
scr.pump(16.0)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    fake.terminate()
    fake.wait(timeout=8)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_stepseq.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad log ---")
print(fake_output)

# Pad (0,0) = led_index 11 (0x0b). Before the press, lane 0 (note 36) is
# all-rest - kStepUnlitColor {12,12,12} = hex 0c 0c 0c
# (LaunchpadManager.cpp). After the press, it must show kStepLitColor
# {0,110,20} = hex 00 6e 14 - not the pitched/percussion note-grid colors,
# proving the step grid (not ordinary NOTES-mode entry) handled the press.
check("Before the press, pad (0,0) shows the step grid's unlit color (0b 0c 0c 0c)",
      "03 0b 0c 0c 0c" in fake_output, fake_output)
check("After the press, pad (0,0) shows the step grid's lit color (0b 00 6e 14) - the step actually toggled",
      "03 0b 00 6e 14" in fake_output, fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
