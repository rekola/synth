"""Disconnect-pruning regression test: device A connects, builds up
per-device state (an octave-up + a pad press/release), then fully exits
(ALSA client closes -> LaunchpadIO's PORT_EXIT -> LaunchpadManager's
device-pruning loop erases its DeviceState). Only after A has completely
exited does device B connect and press - this specifically exercises the
erase-while-iterating prune path with a *real* prior entry to prune, not
just an empty map, and confirms B starts clean (no leftover octave state
from A, no crash)."""
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

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_a.log"), "w")
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "PruneA", "1"], stderr=log_a, stdout=log_a)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate()
    os.kill(pid, 9)
    sys.exit(1)

scr.send(ctrl('n'))
scr.pump(1.0)

# Let device A connect, octave-up, press+release pad (0,0), and fully exit
# (its script sleeps ~1s after release then closes the ALSA client).
proc_a.wait(timeout=20)
check("device A's simulator process exited cleanly", proc_a.returncode == 0, f"returncode={proc_a.returncode}")

# Give synth a moment to process the PORT_EXIT hotplug event.
scr.pump(2.0)

# Now start device B - a fresh connection, no octave-up, arriving *after*
# A's DeviceState should have been pruned.
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_prune_b.log"), "w")
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_device"), "PruneB", "0"], stderr=log_b, stdout=log_b)

deadline = time.time() + 14.0
while time.time() < deadline:
    scr.pump(1.0)

check("synth is still responsive after A's disconnect (no crash/hang)", vk.wait_ready(scr))

notes_found = []
for y in range(scr.screen.lines):
    notes_found += re.findall(r"[A-G][#-][0-9]", scr.screen.display[y])
print("notes found across all rows:", notes_found)

check("device B's press landed at the un-shifted octave (no leftover state from A)",
      "C-4" in notes_found, f"notes_found={notes_found}")

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
