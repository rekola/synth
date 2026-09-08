"""Hotplug regression test: synth starts with no Launchpad
connected at all (confirms it doesn't crash/misbehave with zero devices),
then a simulated Launchpad X is "plugged in" while it's already running -
must be noticed via the ALSA announce-port subscription (hotplug), not
just the startup-time scan. The hotplugged device switches into NOTES
mode and arms Record Arm before pressing (a plain press only ever
auditions, never writes into the pattern, without Record Arm on) and its
release, now while playing, writes a real OFF."""
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

# synth starts with NO Launchpad connected at all - confirms it
# doesn't crash/misbehave with zero devices, and gives a clean baseline to
# detect the live hotplug connection against.
pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    os.kill(pid, 9)
    sys.exit(1)

vk.new_buffer(scr, "hotplug_test")
if vk.is_playing(scr):
    scr.send(b" ")
    scr.pump(1.0)
check("Playback stopped on the new song", not vk.is_playing(scr))
vk.other_window(scr)

# Now "plug in" the simulated Launchpad X - synth is already running
# and must notice it via the ALSA announce-port subscription (hotplug),
# not the startup-time scan (which already ran with nothing connected).
hotplug_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_hotplug.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_hotplug")], stderr=hotplug_log, stdout=hotplug_log)

# fake_launchpad_hotplug sends CC96 (NOTES mode)/CC19 (Record Arm) a
# second apart, then presses. Record Arm's own rising edge starts
# playback, so the note lands wherever the transport happens to be by the
# time the press is processed - scan every visible row.
note_row = None
deadline = time.time() + 15.0
while time.time() < deadline and note_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if vk.has_note(line):
            note_row = row_hex
            break
print("row the hotplugged press landed on:", note_row)
check("Hotplugged device's press entered a note (device was NOT connected at synth startup)",
      note_row is not None)

# Playing now (Record Arm's own auto-start), unlike the step-entry
# semantics this test used to check - a release writes a real OFF at
# whatever (later) row the transport has reached.
off_row = None
deadline = time.time() + 9.0
while time.time() < deadline and off_row is None:
    scr.pump(0.5)
    for row_hex, line in vk.dump_pattern_rows(scr).items():
        if vk.has_off(line):
            off_row = row_hex
            break
print("row the hotplugged device's release wrote OFF on:", off_row)
check("Hotplugged device's release wrote a real OFF while playing", off_row is not None)

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

check("synth sent Programmer-Mode-enter to the hotplugged device",
      "sysex" in fake_output and "0e 01" in fake_output.replace(",", " "), fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
