"""Baseline single-device Launchpad regression test: connects
fake_launchpad (a simulated Launchpad X ALSA client), which switches into
NOTES mode and arms Record Arm (a plain press only ever auditions - it
never writes into the pattern without Record Arm on, and arming it starts
playback, so there is no "step entry while stopped" state to test any
more), then verifies a press/aftertouch/release sequence on pad (0,0)
writes a note into the pattern, modulates its velocity via aftertouch on
a later row as the transport advances, and writes a real OFF on release."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)

# Give the fake device time to register its ALSA client before synth's
# startup-time scan runs (LaunchpadIO does no hotplug yet - it must already
# exist when synth starts).
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# demo3.xml (the harness's default song) has many tracks/voices and hits an
# already-documented, unrelated bug (Space becoming unresponsive under heavy
# playback load, see docs/known_bugs.md) - switch to a fresh, simple new
# song first.
vk.new_buffer(scr, "e2e_baseline")
if vk.is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not vk.is_playing(scr))
vk.other_window(scr)

# fake_launchpad sleeps 6s after its own startup, then sends CC96 (NOTES
# mode) and CC19 (Record Arm) a second apart before its first press.
# Record Arm's own rising edge starts playback immediately, so the note
# lands wherever the transport happens to be by the time the press is
# actually processed - scan every visible row rather than assuming row 00.
deadline = time.time() + 15.0
note_row = None
while time.time() < deadline and note_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if vk.has_note(line):
            note_row = row_hex
            break
print("row the note-on landed on:", note_row)
check("Pad press from the simulated Launchpad X entered a note into the pattern", note_row is not None)
check("Playback is running (Record Arm's own rising edge should have started it)", vk.is_playing(scr))

# fake_launchpad holds 5s between press and aftertouch, then 5s more
# before release. Poll continuously (rather than one fixed sleep) so a
# row showing a real velocity value is caught even if it later scrolls
# out of view.
velocity_row = None
deadline = time.time() + 9.0
while time.time() < deadline and velocity_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if row_hex != note_row and vk.has_defined_velocity(line):
            velocity_row = row_hex
            break
print("row aftertouch modulated:", velocity_row)
check("Aftertouch became visible as a real velocity value on a row other than the note-on's own",
      velocity_row is not None and velocity_row != note_row)

# fake_launchpad's release fires 5s after aftertouch - poll for a genuine
# OFF to appear somewhere (the transport is playing now, unlike the old
# step-entry semantics this test used to check, so a release writes a
# real OFF at whatever row it lands on).
off_row = None
deadline = time.time() + 9.0
while time.time() < deadline and off_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if vk.has_off(line):
            off_row = row_hex
            break
print("row release wrote OFF on:", off_row)
check("Release wrote a real OFF while playing", off_row is not None)

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

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "sysex" in fake_output and "0e 01" in fake_output.replace(",", " "), fake_output)
check("synth sent a Device Inquiry SysEx to the simulated device",
      "7e 7f 06 01" in fake_output, fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
