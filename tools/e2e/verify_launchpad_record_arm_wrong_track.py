"""Regression test for a real bug: LaunchpadManager::handlePadEvent()'s
own "recording supersedes the assigned track" override only fired when
the recording track's internal id happened to sort numerically lower
than the assigned/cursor track's id - comparing a fresh candidate
straight against the already-assigned track_id, rather than against a
fresh sentinel the way refresh()'s own identical override (the LED/tuning-
preview path) already did it correctly. Track "1" here (armed/targeted
for clip recording) is created after, and so has a higher internal id
than, track "0" (left as the assigned/cursor track this whole script) -
the common case: a track armed/selected later in a session. Before the
fix, a NOTE-mode note played here would audibly land on track 0 (LED/
tuning preview already correctly showed track 1, so nothing looked wrong
until checking what actually got recorded) and nothing would ever be
recorded onto track 1 at all."""
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


fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_wrong_track.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_wrong_track")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_record_arm_wrong_track_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Until the simulator has finished its scripted sequence.
scr.wait_for_exit(fake, 16)
scr.pump(0.5)

# M-x live-view - same mechanism verify_launchpad_record_arm_holes.py
# already uses.
scr.send(b"\x1b")
scr.pump(0.3)
scr.send(b"x")
scr.pump(0.3)
scr.send(b"live-view\r")
scr.pump(1.0)
vk.hide_outline(scr)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_wrong_track.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_record_arm_wrong_track log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

clip_grid_text = scr.dump()
print("\n--- ClipGrid screen dump ---")
print(clip_grid_text)

# The grid has no divider between tracks, so a row's cells are told apart
# by screen column: the header row ("T0 ... T1 ... Master") gives each
# track's first column (cell columns, since a wide glyph shifts string
# indices). "▸" (ClipGrid.cpp's populated-clip icon) in a track's span
# means a populated clip.
def cells(y):
    buf = scr.screen.buffer[y]
    return "".join(buf[x].data or " " for x in range(scr.screen.columns))

header_y = next((y for y in range(scr.screen.lines)
                 if cells(y).startswith("T0") and "T1" in cells(y) and "Master" in cells(y)), None)
track0_populated, track1_populated = [], []
if header_y is not None:
    header = cells(header_y)
    x1, xm = header.index("T1"), header.index("Master")
    for y in range(header_y + 1, scr.screen.lines):
        row = cells(y)
        if "Sends" in row:
            break
        if "▸" in row[:x1]:
            track0_populated.append(row)
        if "▸" in row[x1:xm]:
            track1_populated.append(row)
check("the clip grid header was found", header_y is not None, clip_grid_text)

check("track 0 (left as the assigned/cursor track, never armed) shows no populated clip at all",
      len(track0_populated) == 0, track0_populated)
check("track 1 (armed and targeted for recording) shows exactly one populated clip - the note was correctly recorded there",
      len(track1_populated) == 1, track1_populated)

print(f"\n{sum(1 for _, ok in results if ok)}/{len(results)} checks passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
