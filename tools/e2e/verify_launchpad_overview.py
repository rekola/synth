"""GridMode::OVERVIEW regression test: clicks the PatternMatrix widget to
give it UI focus (forcing every connected Launchpad into OVERVIEW mode -
see LaunchpadManager::refresh()'s own OverviewWindow handling), then lets
the simulated Launchpad X press pad (0,0) and confirms the resulting
commit (LaunchpadManager::handleOverviewPadEvent -> UI::commitOverviewCell)
actually moved the playhead - proof the pad press reached the OVERVIEW
dispatch path in UI::handleLaunchpadPadEvent rather than falling through to
handlePadEvent()'s ordinary NOTES-mode note entry (which would instead
write a note into the pattern, not move the playhead)."""
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

def info_line(scr):
    for line in scr.dump().splitlines():
        if "pattern:" in line:
            return line
    return None

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_overview.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)

# Same startup ordering as verify_launchpad_e2e.py - the fake device must
# already exist as an ALSA client before synth's startup-time scan runs.
time.sleep(1)

# fake_launchpad always presses pad (0,0) [note 11] - x=0 (the leftmost
# visible track column), y=0 (the *bottom* row of the 8-row grid - see
# LaunchpadManager::handleOverviewPadEvent's own y-flip comment), so with
# no scrolling that's scene index 7 - see launchpad_overview_test.xml's own
# comment for why it's shaped exactly this way. Not one of the user's own
# songs/*.xml (those can be renamed/edited/deleted at any time - this
# script needs a fixture that won't move under it, matching tests/
# fixtures/'s own role for the C++ ctest suite, just kept alongside the
# other e2e-specific songs already in this directory).
SONG = os.path.join(SCRIPT_DIR, "launchpad_overview_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

before = info_line(scr)
print("info line before anything:", repr(before))

# Click PatternMatrix (its own grid sits in the scope row, columns 0+,
# rows 1-5 - see UI::layout()) to give it focus, before fake_launchpad's
# own 6s startup delay elapses.
scr.send(b"\x1b[<0;1;2M")
scr.pump(0.2)
scr.send(b"\x1b[<0;1;2m")
scr.pump(0.5)

after_click = info_line(scr)
print("info line after clicking PatternMatrix (should be unchanged):", repr(after_click))
check("Clicking PatternMatrix alone doesn't move the playhead", after_click == before, (before, after_click))

# fake_launchpad presses pad (0,0) [note 11] ~6s after its own startup,
# holds for aftertouch at +5s, releases at +5s more - poll robustly rather
# than a single fixed sleep.
deadline = time.time() + 15.0
after_press = after_click
while time.time() < deadline:
    scr.pump(0.5)
    after_press = info_line(scr)
    if after_press != after_click:
        break

print("info line after simulated pad (0,0) press:", repr(after_press))
check("Pad press in OVERVIEW mode changed the playhead (reached commitOverviewCell, not ordinary note entry)",
      after_press != after_click, (after_click, after_press))
check("Pad press did NOT enter a note (no note-entry text - just the position changed)",
      after_press is not None and "OFF" not in after_press, after_press)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

# Exits as soon as the press itself is confirmed, not the aftertouch/
# release fake_launchpad's own script still has queued (~10s more) - kill
# it rather than waiting that out.
fake.terminate()
fake.wait(timeout=5)
fake_log.close()
with open(os.path.join(SCRIPT_DIR, "fake_launchpad_overview.log")) as f:
    print("\n--- fake_launchpad log ---")
    print(f.read())

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
