"""Send A grid-mode regression test: toggles a simulated Launchpad X into
Send A mode (CC69), presses a grid pad to raise track 0's Send A level,
and checks the LED bargraph SysEx actually reflects the new value before
and after - the non-NOTES branch of PatternEditor::handleLaunchpadPadEvent
(Send A/B/Main/Pan) had no e2e coverage before this script.

songs/demo3.xml's track 0 starts at sendA=0.3 -> lit_row = round(0.3*7) = 2,
so pad (0,5) [note 61, per padToNoteNumber(x,y) = 11 + x + 10y] starts dark.
Pressing it sets sendA = 5/7 ~= 0.714 -> lit_row = round(0.714*7) = 5, so
(0,5) is lit afterward. Send A's bargraph color is cyan (Rgb{0,127,127} =
"00 7f 7f"), unlike the padColor()-blended NOTES-mode pads (see
verify_fokker_colors.py) - Send/Pan mode paints full-brightness base color
with no idle-brightness scaling (LaunchpadManager::refreshLeds)."""
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

def pad(x, y):
    return 11 + x + 10 * y

def section(log, label):
    marker = f"--- draining ({label}) ---"
    start = log.find(marker)
    if start < 0:
        return ""
    start += len(marker)
    end = log.find("--- draining (", start)
    return log[start:end if end >= 0 else len(log)]

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# The fake device's own script runs on fixed sleeps (connect settle + mode
# toggle + press + a final mode-toggle back to NOTES) - just pump the pty
# the whole time so synth keeps processing/redrawing.
deadline = time.time() + 20.0
while time.time() < deadline and fake.poll() is None:
    scr.pump(0.5)
scr.pump(1.0)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    fake.terminate()
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode.log")) as f:
    log = f.read().replace(",", " ")

print("--- fake_launchpad_sendmode log ---")
print(log)

before = section(log, "Send A mode entered - before press")
after = section(log, "after pad press - Send A changed")

check("log captured a 'before press' LED snapshot", bool(before.strip()), log)
check("log captured an 'after press' LED snapshot", bool(after.strip()), log)

check("(0,0) [track 0, row 0] is cyan before the press (sendA=0.3 already lights row 0)",
      f"03 {pad(0,0):02x} 00 7f 7f" in before, before)
check("(0,5) [track 0, row 5] is dark before the press (sendA=0.3 -> lit_row=2)",
      f"03 {pad(0,5):02x} 00 00 00" in before, before)
check("(0,5) [track 0, row 5] is cyan after the press (sendA now ~0.714 -> lit_row=5)",
      f"03 {pad(0,5):02x} 00 7f 7f" in after, after)
check("(0,6) [track 0, row 6] stays dark after the press (still above lit_row=5)",
      f"03 {pad(0,6):02x} 00 00 00" in after, after)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
