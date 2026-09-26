#!/usr/bin/env python3
"""A fader move during a Session View take records into the take's own
clip (LaunchpadManager::recordFaderAutomationIfArmed()): a track Session
view has taken over ignores its arrangement automation, so a move written
there would never be heard. fake_launchpad_session_automation.c arms the
fixture's only track through the track picker, starts a take in an empty
clip slot, and moves the track's Send A fader while it records; the
Session view pattern editor, showing that clip, must then show a YAxy
command in the track's effect column."""
import os
import re
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []


def check(name, ok, extra=None):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print(extra)


log_path = os.path.join(SCRIPT_DIR, "fake_launchpad_session_automation.log")
fake_log = open(log_path, "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_session_automation")], stderr=fake_log, stdout=fake_log)
time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

pid, fd = vk.spawn(os.path.join(SCRIPT_DIR, "launchpad_session_test.xml"), view="session")
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.kill()
    os.kill(pid, 9)
    sys.exit(2)

# Watch the screen throughout: the command scrolls with the take.
recorded = False
deadline = time.time() + 20
while time.time() < deadline and (fake.poll() is None or not recorded):
    scr.pump(0.2)
    recorded |= re.search(r"YA[0-9a-f]{2}", scr.dump()) is not None
    if fake.poll() is not None and time.time() > deadline - 17:
        scr.wait(1.0)
        recorded |= re.search(r"YA[0-9a-f]{2}", scr.dump()) is not None
        break

check("the take was recording", "●" in scr.dump() or recorded, scr.dump())
check("the fader move landed in the take's clip as a YA command", recorded, scr.dump())

os.kill(pid, 9)
try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()
sys.exit(0 if all(results) else 1)
