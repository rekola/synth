"""Baseline single-device Launchpad regression test: connects
fake_launchpad (a simulated Launchpad X ALSA client), then verifies a
press/aftertouch/release sequence on pad (0,0) writes a note into the
pattern with correct step-entry semantics (a release while stopped must
NOT overwrite the note it belongs to with an OFF - see
PatternEditor::handleLaunchpadPadEvent's RELEASE branch)."""
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
    """Poll in small increments (robust against system load / scheduling
    jitter) until predicate(current_row_text) is true, or timeout."""
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        scr.pump(interval)
        y = find_pattern_row(scr.screen)
        last = scr.screen.display[y] if y is not None else None
        if last is not None and predicate(last):
            return last, True
    return last, False

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)

# Give the fake device time to register its ALSA client before musiceditor's
# startup-time scan runs (LaunchpadIO does no hotplug yet - it must already
# exist when musiceditor starts).
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])

# demo3.xml (the harness's default song) has many tracks/voices and hits an
# already-documented, unrelated bug (Space becoming unresponsive under heavy
# playback load, see docs/known_bugs.md) - switch to a fresh, simple new
# song first.
scr.send(ctrl('n'))
scr.pump(1.0)
if is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not is_playing(scr))

y = find_pattern_row(scr.screen)
line_before = scr.screen.display[y]
print("row before any Launchpad input:", repr(line_before))

# fake_launchpad sleeps 6s after its own startup before sending its first
# note, then holds 5s before aftertouch, then 5s before release. Poll
# robustly (rather than a single fixed sleep) so this isn't sensitive to
# system scheduling jitter.
#
# Step-entry (not playing): press should enter the note at row 00 AND
# auto-advance the cursor off of it (matching keyboard step-entry), so
# check row 00 specifically throughout rather than "whatever the cursor's
# row currently shows" - aftertouch/release must keep targeting row 00
# (where the note actually landed), not wherever the cursor has since
# moved to.
line_after_press, got_press = wait_until(scr, lambda l: l != line_before, timeout=15.0)
print("row 00 after simulated pad press:       ", repr(line_after_press))
check("Pad press from the simulated Launchpad X entered a note into the pattern",
      got_press, f"before={line_before!r} after={line_after_press!r}")
check("Pressed note is NOT the OFF sentinel (this is a fresh press, not a release)",
      got_press and "OFF" not in line_after_press, line_after_press)

y01 = find_pattern_row(scr.screen, target="01")
row01_after_press = scr.screen.display[y01] if y01 is not None else None
print("row 01 after simulated pad press (should now be the cursor row):", repr(row01_after_press))

# fake_launchpad holds 5s between press and aftertouch, and another 5s
# before release - bound this wait well under 10s so it can't accidentally
# catch the release instead of the aftertouch.
line_after_aftertouch, got_aftertouch = wait_until(scr, lambda l: l != line_after_press, timeout=8.0)
print("row 00 after simulated aftertouch:       ", repr(line_after_aftertouch))
print("is_playing at this point:", is_playing(scr))
row01_after_aftertouch = scr.screen.display[find_pattern_row(scr.screen, target="01")]
print("row 01 after simulated aftertouch (diagnostic):", repr(row01_after_aftertouch))
check("Aftertouch modulated row 00's velocity in place (the row the note actually landed on, not wherever the cursor auto-advanced to)",
      got_aftertouch and "OFF" not in line_after_aftertouch,
      line_after_aftertouch)

time.sleep(6)  # fake_launchpad's release fires 5s after aftertouch
scr.pump(1.0)
line_after_release = scr.screen.display[find_pattern_row(scr.screen)]
print("row 00 after simulated release:         ", repr(line_after_release))
check("Release did NOT overwrite the note with OFF while stopped (the step-entry bug this session's feedback was about)",
      "OFF" not in line_after_release and line_after_release == line_after_aftertouch,
      line_after_release)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

fake.wait(timeout=5)
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad log ---")
print(fake_output)

check("musiceditor sent a Programmer-Mode-enter SysEx to the simulated device",
      "sysex" in fake_output and "0e 01" in fake_output.replace(",", " "), fake_output)
check("musiceditor sent a Device Inquiry SysEx to the simulated device",
      "7e 7f 06 01" in fake_output, fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
