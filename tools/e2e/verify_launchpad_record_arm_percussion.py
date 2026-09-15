"""Regression test for a real bug: recording a Session View take into a
step-sequenced PercussionTrack showed the step-grid editor in NOTES mode
instead of letting the performer actually play it live -
handlePadEvent()'s own step-grid short-circuit ran before (and so was
never superseded by) the recording-supersedes-assigned-track override,
so a pad press toggled a step in the *background* pattern instead of
ever reaching the armed take. Deliberately avoids matching exact LED
byte sequences for the step grid vs. free-drumming layout (this
sandboxed environment's own pad-press-to-LED round trip for the step
grid specifically is unreliable even on an unmodified checkout - see
verify_launchpad_stepseq.py's own docstring and docs/known_bugs.md) -
instead verifies the actual functional outcome through the terminal
SessionView widget: did the note actually land in the armed clip at
all."""
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


fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_percussion.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_percussion")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_record_arm_percussion_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_record_arm_percussion's own scripted sequence (6s
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_percussion.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_record_arm_percussion log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

session_view_text = scr.dump()
print("\n--- SessionView screen dump ---")
print(session_view_text)

# SessionView.cpp's own populated-clip icon ("▸"), as a row's own first
# non-space character - not a plain substring search, since the status
# line's own "Octave: ◂ 4 ▸" also contains "▸".
lines = session_view_text.splitlines()
populated_lines = [l for l in lines if l.strip().startswith("▸")]
check("the armed clip shows real, recorded content - the note reached the take, not the step grid's background pattern",
      len(populated_lines) == 1, populated_lines)

print(f"\n{sum(1 for _, ok in results if ok)}/{len(results)} checks passed")
sys.exit(0 if all(ok for _, ok in results) else 1)
