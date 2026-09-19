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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_perc.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_perc")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

# Track 0 pitched, track 1 a percussionTrack (shared with the cross-tuning
# paste test).
pid, fd = vk.spawn(os.path.join(SCRIPT_DIR, "cross_tuning_paste_test.xml"))
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Focus the pattern editor and move its cursor onto track 1, the
# percussion track, which every connected Launchpad follows. The simulator
# waits 8s after its own startup before switching into Note mode, arming
# Record (which starts playback) and pressing - comfortably past this
# navigation.
scr.pump(2.0)
vk.other_window(scr)
scr.send(b"\x1b[1;5C")  # Ctrl+Right: the next track
scr.pump(1.0)

dump_before = scr.dump()
count_bd_before = dump_before.count("BD2") # pad (0,0) -> note 35 "BD2" (Acoustic Bass Drum)

# Poll until either a new "BD2" appears (the press landed) or we time out -
# playback only ever READS existing notes, never writes new ones, so any
# increase in "BD2" occurrences can only be caused by the simulated press.
deadline = time.time() + 15.0
got_press = False
while time.time() < deadline:
    scr.pump(0.5)
    if scr.dump().count("BD2") > count_bd_before:
        got_press = True
        break

dump_after = scr.dump()
check("Pad press on the percussion track entered a 'BD2' (Acoustic Bass Drum) note - layout was switched, not silently dropped",
      got_press, f"BD2 count before={count_bd_before}, after={dump_after.count('BD2')}")

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
# show the percussion family colors (red, idle-dimmed to 59 00 00 since
# nothing is sounding on that pad yet - see
# LaunchpadManager::padColor - for the core kit at LED index 11 = pad
# (0,0)), NOT the pitched tonic-green 00 7f 00.
note_mode_leds = "\n".join(line for line in fake_output.splitlines() if "after Note mode" in line)
check("A percussion-colored LED SysEx (red core-kit pad 11) was sent after landing on the percussion track",
      "0b 59 00 00" in note_mode_leds, note_mode_leds[:400])

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
