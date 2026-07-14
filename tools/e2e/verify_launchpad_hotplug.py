"""Hotplug regression test: musiceditor starts with no Launchpad
connected at all (confirms it doesn't crash/misbehave with zero devices),
then a simulated Launchpad X is "plugged in" while it's already running -
must be noticed via the ALSA announce-port subscription (hotplug), not
just the startup-time scan."""
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

def wait_until(scr, predicate, timeout=20.0, interval=0.5):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        scr.pump(interval)
        y = find_pattern_row(scr.screen)
        last = scr.screen.display[y] if y is not None else None
        if last is not None and predicate(last):
            return last, True
    return last, False

def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])

# musiceditor starts with NO Launchpad connected at all - confirms it
# doesn't crash/misbehave with zero devices, and gives a clean baseline to
# detect the live hotplug connection against.
pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    os.kill(pid, 9)
    sys.exit(1)

scr.send(ctrl('n'))
scr.pump(1.0)
if is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not is_playing(scr))

# Now "plug in" the simulated Launchpad X - musiceditor is already running
# and must notice it via the ALSA announce-port subscription (hotplug),
# not the startup-time scan (which already ran with nothing connected).
hotplug_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_hotplug.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_hotplug")], stderr=hotplug_log, stdout=hotplug_log)

y = find_pattern_row(scr.screen)
line_before = scr.screen.display[y]
print("row before hotplug press:", repr(line_before))

line_after_press, got_press = wait_until(scr, lambda l: l != line_before, timeout=15.0)
print("row after hotplugged press:       ", repr(line_after_press))
check("Hotplugged device's press entered a note (device was NOT connected at musiceditor startup)",
      got_press, f"before={line_before!r} after={line_after_press!r}")

# Not playing (step entry): release must NOT overwrite the note with OFF -
# the note's row already got auto-advanced away from by the press itself.
time.sleep(3)
scr.pump(1.0)
line_after_release = scr.screen.display[find_pattern_row(scr.screen)]
print("row after hotplugged release:     ", repr(line_after_release))
check("Hotplugged device's release did not overwrite the note (step entry, not playing)",
      "OFF" not in line_after_release and line_after_release == line_after_press, line_after_release)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

fake.wait(timeout=5)
hotplug_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_hotplug.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_hotplug log ---")
print(fake_output)

check("musiceditor sent Programmer-Mode-enter to the hotplugged device",
      "sysex" in fake_output and "0e 01" in fake_output.replace(",", " "), fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
