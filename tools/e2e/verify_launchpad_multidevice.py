"""Multi-device regression test: two simulated Launchpad X clients connect
simultaneously; device A does one octave-up press before pressing pad
(0,0), device B presses pad (0,0) immediately with no octave change. If
LaunchpadManager's per-device state is genuinely independent, the two
notes must be the same pitch class exactly one octave apart - proving A's
octave-up did not leak into B (or vice versa). Also exercises the
per-device (not global) chord/gesture auto-advance: B's own release
advances the row before A (still mid-gesture) has pressed, so the two
notes are expected to land on different rows, not the same one."""
import sys, os, subprocess, time, re

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

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_a.log"), "w")
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_b.log"), "w")
# Device A does one octave-up before pressing; device B presses immediately -
# if per-device octave state works, A's note should land an octave above B's.
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "A", "1"], stderr=log_a, stdout=log_a)
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "B", "0"], stderr=log_b, stdout=log_b)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate(); proc_b.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Fresh, empty song (Ctrl-N) - a clean row 0 / track 0 with no pre-existing
# notes to confuse column parsing.
scr.send(ctrl('n'))
scr.pump(1.0)

# Wait for both fake devices to connect, do their scripted button/pad
# presses, and settle.
deadline = time.time() + 14.0
while time.time() < deadline:
    scr.pump(1.0)

# Collect every note name (e.g. "C-4", "C-5") appearing anywhere on screen -
# the two notes may land on different rows (see module docstring).
notes_found = []
for y in range(scr.screen.lines):
    notes_found += re.findall(r"[A-G][#-][0-9]", scr.screen.display[y])
print("notes found across all rows:", notes_found)

check("exactly two distinct note writes happened (one per device)",
      len(set(notes_found)) == 2, f"notes_found={notes_found}")

if len(set(notes_found)) == 2:
    letters = sorted(set(notes_found))
    def parse(n):
        return n[:-1], int(n[-1])
    (pc1, oct1), (pc2, oct2) = parse(letters[0]), parse(letters[1])
    check("both notes are the same pitch class (same pad, no track drift)",
          pc1 == pc2, f"{letters}")
    check("the two notes are exactly one octave apart (independent per-device octave)",
          abs(oct1 - oct2) == 1, f"{letters}")
else:
    check("both notes are the same pitch class (same pad, no track drift)", False, "skipped - wrong note count")
    check("the two notes are exactly one octave apart (independent per-device octave)", False, "skipped - wrong note count")

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
for proc in (proc_a, proc_b):
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
log_a.close()
log_b.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_a.log")) as f:
    print("\n--- device A log ---\n" + f.read())
with open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_b.log")) as f:
    print("\n--- device B log ---\n" + f.read())

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
