"""Auto-create-missing-tracks regression test: loads songs/songtest1.xml
(2 tracks), toggles a simulated Launchpad X into Send A grid mode (CC69),
and presses column 5 - which has no track yet. Confirms
PatternEditor::handleLaunchpadPadEvent creates tracks up to that column
(rather than silently doing nothing) by checking column 5's LED bargraph
goes from fully dark (LaunchpadManager::refreshLeds paints has_track=false
columns black regardless of stored value) to a real, lit bargraph matching
the pressed row. Uses a small-track-count fixture rather than Ctrl-N/a
fresh song to get down to fewer tracks - Ctrl-N has a known post-new-song
staleness quirk in this harness (see verify_keybindings.py's docstring)."""
import sys, os, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

SONG = os.path.join(vk.REPO_ROOT, "songs", "songtest1.xml")

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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode_autocreate.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode_autocreate")], stderr=fake_log, stdout=fake_log)

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_sendmode_autocreate.log")) as f:
    log = f.read().replace(",", " ")

print("--- fake_launchpad_sendmode_autocreate log ---")
print(log)

before = section(log, "Send A mode entered - before press - column 5 has no track yet")
after = section(log, "after pad press - column 5 track auto-created")

check("log captured a 'before press' LED snapshot", bool(before.strip()), log)
check("log captured an 'after press' LED snapshot", bool(after.strip()), log)

for y in range(8):
    check(f"(5,{y}) is dark before the press (column 5 has no track yet)",
          f"03 {pad(5,y):02x} 00 00 00" in before, before)

for y in range(5):
    check(f"(5,{y}) is cyan after the press (track auto-created, row {y} <= lit_row=4)",
          f"03 {pad(5,y):02x} 00 7f 7f" in after, after)
for y in range(5, 8):
    check(f"(5,{y}) stays dark after the press (row {y} > lit_row=4)",
          f"03 {pad(5,y):02x} 00 00 00" in after, after)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
