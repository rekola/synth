"""Regression test for SessionPlayer::triggerClip()'s own
SampleTrack branch: a Live-View pad press on a SampleTrack armed via
the track-picker overlay (CC19) now actually arms real audio capture
(Controller::armSessionTrackRecording()/armThresholdRecording()) instead
of silently falling through to plain audition/assign the way it used to
(the `is_sample_track` carve-out this test closes), and a second press on
that same pad cancels the still-idle arm again.

Verified through the terminal ClipGrid widget's own text (M-x
live-view), not LED bytes - the same approach
verify_launchpad_record_arm_holes.py uses.

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

    # Until the simulator has finished its scripted sequence.
    scr.wait_for_exit(fake, 16)
    scr.pump(0.5)

    # M-x live-view: switches to Live View (ClipGrid focused) for
    # the active song - same mechanism verify_launchpad_record_arm_holes.py
    # already uses.
    scr.send(b"\x1b")
    scr.pump(0.3)
    scr.send(b"x")
    scr.pump(0.3)
    scr.send(b"live-view\r")
    scr.pump(1.0)
    vk.hide_outline(scr)

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

    clip_grid_text = scr.dump()
    print(f"\n--- ClipGrid screen dump ({label}) ---")
    print(clip_grid_text)
    return clip_grid_text

arm_only_text = run(cancel=False)
cancel_text = run(cancel=True)

if arm_only_text is not None:
    check("a single Live-grid press on the armed track shows the '●' record indicator",
          "●" in arm_only_text, arm_only_text)

if cancel_text is not None:
    check("a second press on that same pad cancels the still-idle arm - no '●' left showing",
          "●" not in cancel_text, cancel_text)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
