"""Regression test for the shift+pad "open for editing" gesture
(LaunchpadManager::handleSessionPadEvent()'s own CC91-held comment) on a
clip belonging to a *lane-less* PercussionTrack: Controller::
toggleDrumClipFocus() doesn't gate on lane count at all (its own doc
comment) - it opens exactly the same way a step-sequenced clip does, and
the step grid it switches every connected device to just shows empty (no
lane has anything to light - DeviceState::show_step_grid's own comment),
rather than either silently doing nothing (the original bug) or routing
to the lane picker instead (an earlier, rejected fix - a performer
reaching for "open editing" should land on the editing surface itself,
not somewhere else that also happens to relate to drum tracks).

The fixture's Session view starts showing real, colorful per-track
identity colors; after the gesture, every one of the 64 grid pads should
go fully black (the step grid's own empty-lane-list rendering - every
row past drum_lane_notes.size(), and there are zero of them, reads as
"past the track's actual lane count") and CC96 ("Note")'s own LED should
go dark too (show_step_grid forces it there regardless of grid_mode -
refreshLeds()'s own comment), rather than CC97 ("Custom") ever lighting
up - this gesture never opens the lane picker.

Scans every LED dump in the run rather than trusting any one dump's
position in the log."""
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

def led_dumps(text):
    return re.findall(r"received sysex[^:]*:\s*(f0 00 20 29 02 0c 03(?: [0-9a-f]{2})+ f7)", text)

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_no_lanes.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_shift_no_lanes")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_shift_no_lanes_test.xml")
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_no_lanes.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_shift_no_lanes log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

found_empty_grid = False
found_custom_active = False
for dump in led_dumps(fake_output):
    m97 = re.search(r"03 61 ([0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2})", dump)
    if m97 and m97.group(1) == "5a 00 7f":
        found_custom_active = True

    m96 = re.search(r"03 60 ([0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2})", dump)
    note_dark = m96 is not None and m96.group(1) == "00 00 00"

    body = dump.split(" ")[7:-1]
    all_grid_black = True
    saw_any_grid_pad = False
    for i in range(0, len(body) - 4, 5):
        typ, led_hex, r, g, b = body[i:i + 5]
        if typ != "03":
            continue
        led = int(led_hex, 16)
        if 11 <= led <= 88:
            saw_any_grid_pad = True
            if (r, g, b) != ("00", "00", "00"):
                all_grid_black = False
    if note_dark and saw_any_grid_pad and all_grid_black:
        found_empty_grid = True

check("some point in the run shows the step grid completely empty (every pad black) with CC96 dark",
      found_empty_grid)
check("CC97 (Custom/lane picker) never lights up active - this gesture doesn't open it",
      not found_custom_active)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
