"""Regression test for LaunchpadManager::triggerSceneRow() - a single
scene-row button press (CC19/89/79/69/59/49/39/29, the classic Launchpad
right-column convention, reachable with Session's own mixer submode off,
the default) is supposed to launch every visible track's own clip at that
row *together*. A real bug: triggerSceneRow() calls triggerSessionClip()
once per track in a plain loop, and that function's own "is anything
already triggered/queued anywhere -> launch immediately, otherwise queue"
decision (triggered_pattern_by_track_.empty() && ...) was being
recomputed fresh on every one of those calls - so the very first track's
own call would populate triggered_pattern_by_track_ with its own entry,
and every track *after* it in the same loop would then see something
"already playing" and queue against it instead of joining it, leaving
only the first track's own clip launched and every other one queued
until the next quantization boundary.

Presses CC19 once (row 0, clip index 7 on both fixture tracks) and
expects *both* tracks' own pad (0,0)/(1,0) LEDs to start pulsing green
immediately, not just the first one."""
import sys, os, re, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []

def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

def last_led_state(text, led_index_hex):
    pattern = (rf"(03 {led_index_hex} [0-9a-f]{{2}} [0-9a-f]{{2}} [0-9a-f]{{2}}"
               rf"|02 {led_index_hex} [0-9a-f]{{2}}"
               rf"|01 {led_index_hex} [0-9a-f]{{2}} [0-9a-f]{{2}})")
    matches = re.findall(pattern, text)
    return matches[-1] if matches else None

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_scene_row.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_scene_row")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_scene_row_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Until the simulator has finished its scripted sequence.
scr.wait_for_exit(fake, 10)
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_scene_row.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_scene_row log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

state0 = last_led_state(fake_output, "0b")
state1 = last_led_state(fake_output, "0c")
print("Track 0's own pad (0,0) [LED 0b] final state:", state0)
print("Track 1's own pad (1,0) [LED 0c] final state:", state1)

check("Track 0's own clip pulses green (SessionPadHighlight::PLAYING) after the scene launch",
      state0 is not None and state0.startswith("02 0b 15"), state0)
check("Track 1's own clip ALSO pulses green - joined the same scene launch, not left queued",
      state1 is not None and state1.startswith("02 0c 15"), state1)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
