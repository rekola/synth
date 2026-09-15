"""Regression test for a real bug: LaunchpadManager::handlePadEvent()'s
own "recording supersedes the assigned track" override only fired when
the recording track's internal id happened to sort numerically lower
than the assigned/cursor track's id - comparing a fresh candidate
straight against the already-assigned track_id, rather than against a
fresh sentinel the way refresh()'s own identical override (the LED/tuning-
preview path) already did it correctly. Track "1" here (armed/targeted
for Session recording) is created after, and so has a higher internal id
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

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_record_arm_wrong_track_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_record_arm_wrong_track's own scripted sequence (6s
# startup + ~11 drains of ~0.5-1s each) takes a bit under 15s.
time.sleep(16)
scr.pump(0.5)

# M-x session-view - same mechanism verify_launchpad_record_arm_holes.py
# already uses.
scr.send(b"\x1b")
scr.pump(0.3)
scr.send(b"x")
scr.pump(0.3)
scr.send(b"session-view\r")
scr.pump(1.0)

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

session_view_text = scr.dump()
print("\n--- SessionView screen dump ---")
print(session_view_text)

# Two tracks means two SessionView columns share each screen row (track
# 0's own cell, then a "│" divider, then track 1's), so "▸" (SessionView.
# cpp's own populated-clip icon) isn't necessarily the row's first
# character the way the single-track holes test could assume - split each
# row on "│" and check each column's own cell instead. Excludes the
# status line's own "Octave: ◂ 4 ▸", which also contains "▸" but isn't a
# SessionView row at all (no "│" divider in it in the right place).
track0_populated, track1_populated = [], []
for line in session_view_text.splitlines():
    cells = line.split("│")
    if len(cells) < 2 or "Octave:" in line:
        continue
    if "▸" in cells[0]:
        track0_populated.append(line)
    if "▸" in cells[1]:
        track1_populated.append(line)

check("track 0 (left as the assigned/cursor track, never armed) shows no populated clip at all",
      len(track0_populated) == 0, track0_populated)
check("track 1 (armed and targeted for recording) shows exactly one populated clip - the note was correctly recorded there",
      len(track1_populated) == 1, track1_populated)

print(f"\n{sum(1 for _, ok in results if ok)}/{len(results)} checks passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
