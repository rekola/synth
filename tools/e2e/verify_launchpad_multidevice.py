"""Multi-device regression test: two simulated Launchpad X clients connect
simultaneously and each independently switches into its own NOTES mode
(GridMode is per-device); device A additionally arms Record Arm (a single
song-wide flag, not per-device - only one instance ever presses it) and
presses pad (0,0) [note 11], while device B - relying on A's already-armed
state - presses a different pad [note 12] at roughly the same time. If
LaunchpadManager's per-device state (active_notes, keyed by device id) is
genuinely independent, both distinct notes must land correctly - neither
device's own held-note bookkeeping colliding into or corrupting the
other's."""
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

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_a.log"), "w")
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_device_b.log"), "w")
# Device A arms Record Arm and presses note 11; device B relies on A's
# already-armed state and presses a different note (12) - if per-device
# state works, both should land as distinct, uncorrupted notes.
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "A", "arm", "11"], stderr=log_a, stdout=log_a)
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "B", "plain", "12"], stderr=log_b, stdout=log_b)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate(); proc_b.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Fresh, empty song - a clean row 0 / track 0 with no pre-existing notes
# to confuse column parsing.
vk.new_buffer(scr, "multidevice_test")
vk.other_window(scr)

# Wait for both fake devices to connect, arm/press/release, and settle,
# remembering each row's own note columns seen across every poll (a row
# can scroll out of view again before the next one).
notes_by_row = {}
deadline = time.time() + 16.0
while time.time() < deadline:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        cols = [c for c in vk.note_columns(line) if c not in ("···", "OFF", "   ")]
        if cols:
            notes_by_row.setdefault(row_hex, set()).update(cols)

all_notes = set()
for cols in notes_by_row.values():
    all_notes |= cols
print("distinct notes seen across all rows:", sorted(all_notes))
print("by row:", {k: sorted(v) for k, v in notes_by_row.items()})

check("exactly two distinct note writes happened (one per device)",
      len(all_notes) == 2, f"all_notes={all_notes}")

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
