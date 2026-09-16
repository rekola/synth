"""Regression test for LaunchpadManager::triggerSessionClip()'s own
SampleTrack branch: a Session-view pad press on a SampleTrack armed via
the track-picker overlay (CC19) now actually arms real audio capture
(Controller::armSessionTrackRecording()/armThresholdRecording()) instead
of silently falling through to plain audition/assign the way it used to
(the `is_sample_track` carve-out this test closes), and a second press on
that same pad cancels the still-idle arm again.

Verified through the terminal SessionView widget's own text (M-x
session-view), not LED bytes - the same reasoning
verify_launchpad_record_arm_holes.py already uses, and the same one that
sidesteps this sandboxed environment's documented ALSA-contention
flakiness for LED-based checks (docs/known_bugs.md); a real armed
SampleTrack take also engages Player.cpp's own threshold-triggered ALSA
capture logic, exactly the class of "real audio path" behavior that
documented flakiness is about, so LED reads here would be doubly fragile.

Two independent spawns, since the interesting state (an armed-but-not-yet-
disarmed take) can only be read reliably once everything has settled, and
disarming via the picker (Controller::disarmTrack()) unconditionally
clears the record indicator regardless of whether the cancel gesture
itself worked, which would mask a broken cancel - see the fixture's own
comment: spawn 1 presses the armed pad once and expects the "●" record
indicator; spawn 2 presses it a second time and expects it gone again."""
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

def run(cancel):
    label = "cancel" if cancel else "arm-only"
    fake_log = open(os.path.join(SCRIPT_DIR, f"fake_launchpad_sampletrack_record_arm_{label}.log"), "w")
    fake_args = [os.path.join(SCRIPT_DIR, "fake_launchpad_sampletrack_record_arm")]
    if cancel:
        fake_args.append("cancel")
    fake = subprocess.Popen(fake_args, stderr=fake_log, stdout=fake_log)

    time.sleep(1)

    SONG = os.path.join(SCRIPT_DIR, "launchpad_sampletrack_record_arm_test.xml")
    pid, fd = vk.spawn(SONG)
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("synth not ready")
        fake.terminate()
        os.kill(pid, 9)
        return None

    # fake_launchpad_sampletrack_record_arm's own scripted sequence (6s
    # startup + ~5.5s of drains, the last one 3s to settle) takes a bit
    # under 15s - give it comfortable margin.
    time.sleep(16)
    scr.pump(0.5)

    # M-x session-view: opens (or switches to) the SessionView aspect of
    # the active song - same mechanism verify_launchpad_record_arm_holes.py
    # already uses.
    scr.send(b"\x1b")
    scr.pump(0.3)
    scr.send(b"x")
    scr.pump(0.3)
    scr.send(b"session-view\r")
    scr.pump(1.0)

    try:
        os.kill(pid, 9)
    except ProcessLookupError:
        pass

    try:
        fake.wait(timeout=5)
    except subprocess.TimeoutExpired:
        fake.kill()
    fake_log.close()

    with open(os.path.join(SCRIPT_DIR, f"fake_launchpad_sampletrack_record_arm_{label}.log")) as f:
        fake_output = f.read()
    print(f"\n--- fake_launchpad_sampletrack_record_arm ({label}) log ---")
    print(fake_output)

    check(f"[{label}] synth sent a Programmer-Mode-enter SysEx to the simulated device",
          "0e 01" in fake_output.replace(",", " "), fake_output)

    session_view_text = scr.dump()
    print(f"\n--- SessionView screen dump ({label}) ---")
    print(session_view_text)
    return session_view_text

arm_only_text = run(cancel=False)
cancel_text = run(cancel=True)

if arm_only_text is not None:
    check("a single Session-grid press on the armed track shows the '●' record indicator",
          "●" in arm_only_text, arm_only_text)

if cancel_text is not None:
    check("a second press on that same pad cancels the still-idle arm - no '●' left showing",
          "●" not in cancel_text, cancel_text)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
