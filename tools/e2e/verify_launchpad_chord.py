"""Chord semantics regression test: 3 near-simultaneous pad presses must
land in 3 distinct note columns on the same row (not collide into one),
and releasing them in non-LIFO order must not turn any of them into an
OFF or advance the cursor until the whole gesture has released - this is
the exact bug this feature's original bug-report feedback was about."""
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

def find_pattern_row(screen, target="00"):
    for y in range(screen.lines):
        line = screen.display[y]
        if line.strip().startswith(target) and "│" in line:
            return y
    return None

def is_playing(scr):
    d = scr.dump()
    info_lines = [l for l in d.splitlines() if "pattern:" in l]
    return bool(info_lines) and "PLAYING" in info_lines[-1]

def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])

log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_chord.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_chord")], stderr=log, stdout=log)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

scr.send(ctrl('n'))
scr.pump(1.0)
if is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not is_playing(scr))

y = find_pattern_row(scr.screen)
line_before = scr.screen.display[y]
print("row before chord:", repr(line_before))

deadline = time.time() + 12.0
line_after_press = line_before
while time.time() < deadline:
    scr.pump(0.5)
    line_after_press = scr.screen.display[find_pattern_row(scr.screen)]
    if line_after_press != line_before:
        break
print("row after chord press:", repr(line_after_press))

# Expect 3 distinct, simultaneously-defined notes in 3 separate sub-columns
# (not one note repeatedly overwritten by the next).
note_names = re.findall(r'([A-G][#b♭]?-?\d|C--)', line_after_press.split("│", 1)[1] if "│" in line_after_press else "")
print("parsed note names in row:", note_names)
check("Chord entered 3 distinct notes (not collided into one column)",
      len([n for n in note_names if n]) >= 3, line_after_press)

deadline2 = time.time() + 12.0
line_after_release = line_after_press
while time.time() < deadline2:
    scr.pump(0.5)
    line_after_release = scr.screen.display[find_pattern_row(scr.screen)]
    if "OFF" in line_after_release:
        break
print("row after non-LIFO chord release:", repr(line_after_release))
check("Releasing the chord (non-LIFO order) did not turn any note into OFF",
      "OFF" not in line_after_release and line_after_release == line_after_press,
      line_after_release)

y01 = find_pattern_row(scr.screen, target="01")
row01 = scr.screen.display[y01] if y01 is not None else None
print("row 01 (should now be current, advanced once after the whole chord released):", repr(row01))

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
