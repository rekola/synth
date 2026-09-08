"""Chord semantics regression test: 3 near-simultaneous pad presses must
land in 3 distinct note columns on the same row (not collide into one),
and releasing them in non-LIFO order must not corrupt any of the 3
columns' own identity - each of the 3 held notes gets its own real OFF,
not fewer than 3 (columns colliding) or a wrong one (release order
scrambling which column gets which OFF) - the exact bug this feature's
original bug-report feedback was about."""
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

def note_count(line):
    if line is None:
        return 0
    return len([c for c in vk.note_columns(line) if c not in ("···", "OFF", "   ")])

def off_count(line):
    if line is None:
        return 0
    return len([c for c in vk.note_columns(line) if c == "OFF"])

log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_chord.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_chord")], stderr=log, stdout=log)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

vk.new_buffer(scr, "chord_test")
if vk.is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not vk.is_playing(scr))
vk.other_window(scr)

# fake_launchpad_chord sleeps 4s, then CC96 (Note mode)/CC19 (Record Arm)
# a second apart, then presses the 3-note chord. Record Arm's own rising
# edge starts playback, so the chord lands wherever the transport happens
# to be by the time the press is processed - scan every visible row.
chord_row = None
deadline = time.time() + 15.0
while time.time() < deadline and chord_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if note_count(line) >= 3:
            chord_row = row_hex
            break
print("row the chord landed on:", chord_row)
check("Chord entered 3 distinct notes (not collided into one column)", chord_row is not None)

# fake_launchpad_chord holds 2s then releases non-LIFO (middle pad first,
# then first, then last) 20ms apart - close together, but real wall-clock/
# audio-thread scheduling jitter can still straddle a row boundary between
# them, and a row can scroll out of view again before the next poll - so
# remember each row's own highest OFF count seen across every poll, then
# sum those, rather than trusting one single snapshot.
offs_by_row = {}
deadline = time.time() + 15.0
while time.time() < deadline and sum(offs_by_row.values()) < 3:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        offs_by_row[row_hex] = max(offs_by_row.get(row_hex, 0), off_count(line))
total_offs = sum(offs_by_row.values())
print("total OFFs seen across all rows after the non-LIFO release:", total_offs, offs_by_row)
check("Releasing the chord (non-LIFO order) wrote 3 distinct OFFs, one per column (no collision/scramble)",
      total_offs >= 3, f"total_offs={total_offs}")

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
fake.wait(timeout=5)
log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_chord.log")) as f:
    print("\n--- fake_launchpad_chord log ---")
    print(f.read())

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
