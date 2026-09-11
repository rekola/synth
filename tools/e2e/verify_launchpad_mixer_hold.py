"""Regression test for the mixer radio group's own momentary
hold-to-preview gesture (LaunchpadManager::armMixerHoldPreview()/
handleMixerFunctionRelease()): a quick tap on a different member switches
to it and stays there, but a genuine hold (>= 600ms) reverts back to
whatever was showing right before that press once released.

Enters Session's own mixer submode, quick-taps Send A (CC69, led index
0x45) so it becomes the sticky selection, long-holds Mute (CC39, led index
0x27) past the threshold and releases - expecting Send A's own LED to be
bright again afterward (reverted), not Mute's - then quick-taps Mute again
as a control, expecting Mute to stay bright this time (sticky, no hold
involved)."""
import sys, os, re, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []

def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

def last_led_color(text, led_index_hex):
    matches = re.findall(rf"03 {led_index_hex} ([0-9a-f]{{2}}) ([0-9a-f]{{2}}) ([0-9a-f]{{2}})", text)
    return matches[-1] if matches else None

def phase(text, start_marker, end_marker=None):
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find(end_marker, start) if end_marker else len(text)
    if end_marker and end < 0:
        end = len(text)
    return text[start:end]

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_mixer_hold.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_mixer_hold")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_mixer_hold's own scripted sequence (6s startup + ~7s of
# drains/holds) takes a bit over 13s - give it comfortable margin.
time.sleep(16)
scr.pump(0.5)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_mixer_hold.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_mixer_hold log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

after_tap_a = phase(fake_output, "after CC69 quick tap - showing Send A", "sending CC39 press")
mid_hold = phase(fake_output, "mid-hold on CC39 - previewing Mute", "sending CC39 release")
after_hold_release = phase(fake_output, "after CC39 long-hold release - should be back on Send A", "sending CC39 quick tap")
after_tap_mute = phase(fake_output, "after CC39 quick tap - showing Mute")

send_a_after_tap = last_led_color(after_tap_a, "45")
mute_mid_hold = last_led_color(mid_hold, "27")
send_a_after_hold_release = last_led_color(after_hold_release, "45")
mute_after_hold_release = last_led_color(after_hold_release, "27")
mute_after_tap = last_led_color(after_tap_mute, "27")

print("Send A (0x45) LED after quick tap:            ", send_a_after_tap)
print("Mute (0x27) LED mid-hold:                      ", mute_mid_hold)
print("Send A (0x45) LED after long-hold release:     ", send_a_after_hold_release)
print("Mute (0x27) LED after long-hold release:       ", mute_after_hold_release)
print("Mute (0x27) LED after a later quick tap:       ", mute_after_tap)

check("Send A's own LED is bright cyan after a quick tap (sticky selection)",
      send_a_after_tap == ('00', '7f', '7f'), send_a_after_tap)
check("Mute's own LED is bright yellow mid-hold (previewing)",
      mute_mid_hold == ('7f', '7f', '00'), mute_mid_hold)
check("Send A's own LED is bright cyan again after the long hold released (reverted)",
      send_a_after_hold_release == ('00', '7f', '7f'), send_a_after_hold_release)
check("Mute's own LED is dim again after the long hold released (no longer showing)",
      mute_after_hold_release == ('14', '14', '00'), mute_after_hold_release)
check("Mute's own LED is bright yellow after a later quick tap (sticky this time, no hold)",
      mute_after_tap == ('7f', '7f', '00'), mute_after_tap)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
