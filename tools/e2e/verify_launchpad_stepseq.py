"""Drum-machine step-grid regression test: the step grid only ever shows/
edits a clip actually open for editing on the assigned track (Controller::
getFocusedClipTrackId()) - never the section's own background Pattern,
which has no pagination and spans the whole scene, far more than this
fixed grid could ever show meaningfully (a real user report: merely
navigating the shared cursor onto a step-sequenced PercussionTrack used
to show/allow editing the background pattern directly, unconditionally,
even with Record Arm off). Opening a clip is a Launchpad gesture (shift + pad in Live View) - it
switches that Launchpad into NOTES mode showing the clip's own step grid
(TerminalUI.cpp's own drum-edit-request listener,
LaunchpadManager::openStepView()). The simulated device does that, then
this script confirms a pad press there toggles that lane/step immediately,
reflected in the LED colors sent back to the device - not silently
falling through to ordinary ambient NOTES-mode chord entry.
"""
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
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_stepseq")], stderr=fake_log, stdout=fake_log, env=vk.fake_env())
time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# The simulated Launchpad opens the test song's only clip (track 0, clip
# row 0) itself, once told to go.
scr.pump(2.0)

vk.go(fake)
scr.wait_for_exit(fake, 16)

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

# Pad (0,4) = led_index 51 (0x33), step 0 of the selected sound (the kick, note 36). Before the press, it is
# all-rest - kStepUnlitColor {12,12,12} = hex 0c 0c 0c
# (LaunchpadManager.cpp). After the press, it must show kStepLitColor
# {0,110,20} = hex 00 6e 14 - not the pitched/percussion note-grid colors,
# proving the step grid (not ordinary NOTES-mode entry) handled the press.
def leds(label):
    return "\n".join(line for line in fake_output.splitlines() if f"received sysex {label} " in line)

check("Before the press, pad (0,4) shows the step grid's unlit color (33 0c 0c 0c)",
      "03 33 0c 0c 0c" in leds("before press"), leds("before press")[:400])
check("After the press, pad (0,4) shows the step grid's lit color (33 00 6e 14) - the step actually toggled",
      "03 33 00 6e 14" in leds("after press"), leds("after press")[:400])

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
