"""Disconnect-pruning regression test: device A connects, arms Record Arm
and does a pad press/release, then fully exits (ALSA client closes ->
LaunchpadIO's PORT_EXIT -> LaunchpadManager's device-pruning loop erases
its DeviceState) without ever disarming - Record Arm is Song state, not
per-device connection state, so it stays armed. Only after A has
completely exited does device B connect and press (relying on that still-
armed state, never arming itself) - this specifically exercises the
erase-while-iterating prune path with a *real* prior entry to prune, not
just an empty map, and confirms B starts clean and can still write (no
leftover per-device state from A causing a crash or a silently dropped
write)."""
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

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_a.log"), "w")
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "PruneA", "arm", "11"], stderr=log_a, stdout=log_a)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate()
    os.kill(pid, 9)
    sys.exit(1)

vk.new_buffer(scr, "disconnect_prune_test")
vk.other_window(scr)

# Let device A connect, switch to NOTES mode, arm Record Arm, press+
# release pad (0,0), and fully exit (its script sleeps ~1s after release
# then closes the ALSA client) - without ever disarming.
proc_a.wait(timeout=20)
check("device A's simulator process exited cleanly", proc_a.returncode == 0, f"returncode={proc_a.returncode}")

# Give synth a moment to process the PORT_EXIT hotplug event.
scr.pump(2.0)

# Now start device B - a fresh connection, arriving *after* A's
# DeviceState should have been pruned. It never arms Record Arm itself -
# relies entirely on A's still-armed state having survived A's disconnect.
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_b.log"), "w")
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "PruneB", "plain", "11"], stderr=log_b, stdout=log_b)

note_row = None
deadline = time.time() + 16.0
while time.time() < deadline and note_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if vk.has_note(line):
            note_row = row_hex
            break

check("synth is still responsive after A's disconnect (no crash/hang)", vk.wait_ready(scr))
print("row device B's press landed on:", note_row)
check("device B's press entered a note (no leftover per-device state from A, Record Arm survived the disconnect)",
      note_row is not None)

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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_a.log")) as f:
    print("\n--- device A (pruned) log ---\n" + f.read())
with open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_b.log")) as f:
    print("\n--- device B (post-prune) log ---\n" + f.read())

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
