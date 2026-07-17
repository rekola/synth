"""Percussion layout regression test: navigating onto a percussion track
must switch the Launchpad's pad->note mapping and LED coloring to the GM
percussion layout (not silently drop input, and not keep showing the
pitched isomorphic coloring)."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_perc.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_perc")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn()  # loads demo3.xml, which has a percussionTrack as its LAST track
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# demo3.xml has many tracks/voices and hits an already-documented, unrelated
# bug (Space unresponsive under heavy playback load, see docs/known_bugs.md)
# - rather than fight it, just test the "recording while playing" path,
# which is a legitimate, real usage mode (handleLaunchpadPadEvent's
# info.isPlaying() branch) and doesn't depend on stopping transport at all.

# Wait for the fake device's Programmer-Mode-enter handshake, then jump the
# cursor onto the last track (Ctrl-E) - demo3.xml's percussionTrack. The
# simulator waits 12s after its own startup before pressing, comfortably
# past this navigation.
scr.pump(2.0)
scr.send(ctrl('e'))
scr.pump(1.0)

dump_before = scr.dump()
count_bd_before = dump_before.count("BD") # pad (0,0) -> note 35 "BD" (Acoustic Bass Drum)

# Poll until either a new "BD" appears (the press landed) or we time out -
# playback only ever READS existing notes, never writes new ones, so any
# increase in "BD" occurrences can only be caused by the simulated press.
deadline = time.time() + 15.0
got_press = False
while time.time() < deadline:
    scr.pump(0.5)
    if scr.dump().count("BD") > count_bd_before:
        got_press = True
        break

dump_after = scr.dump()
check("Pad press on the percussion track entered a 'BD' (Acoustic Bass Drum) note - layout was switched, not silently dropped",
      got_press, f"BD count before={count_bd_before}, after={dump_after.count('BD')}")

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    fake.terminate()
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_perc.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad log ---")
print(fake_output)

# The LED SysEx sent after the cursor lands on the percussion track should
# show the percussion family colors (e.g. bright red 7f 00 00, blended 50%
# towards black to 3f 00 00 since nothing is sounding on that pad yet - see
# LaunchpadManager::padColor - for the core kit at LED index 11 = pad
# (0,0)), NOT the pitched tonic-green 00 7f 00.
check("A percussion-colored LED SysEx (red core-kit pad 11) was sent after landing on the percussion track",
      "0b 3f 00 00" in fake_output, fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
