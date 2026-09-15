"""Regression test for the track-picker overlay's RECORD_ARM purpose
(CC19): one of the eight members of the same right-column mixer-submode
radio group as Stop Clip/Mute/Solo (LaunchpadManager.h's own GridMode
comment), gated on that submode the same way they are - a plain CC19
press launches scene row 0 instead while mixer submode is off. Reuses
Stop Clip's own red hue (the two purposes never show at once) rather
than a fifth, distinct color. Deliberately avoids ever triggering
playback, same reasoning as verify_launchpad_mute_picker.py - arming is
pure bookkeeping with nothing to hear.

Presses CC95 a second time first to enter Session's own mixer submode
(required before CC19 opens the overlay rather than launching a scene -
also confirms Session's own LED turns orange), then presses CC19 to open the
overlay, confirms both CC19's own LED and the picker row's pad (0,0) (the
fixture's only track, unarmed by default) show dim red before/bright red
once armed, that pad (0,7) - outside the picker row - stays untouched,
that the overlay stays open after picking (a second CC19 press is what
finally closes it), and that everything reverts once closed."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_picker.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_picker")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_record_arm_picker's own scripted sequence (6s startup +
# ~4s of drains) takes a bit under 10s - give it comfortable margin.
time.sleep(12)
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_record_arm_picker.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_record_arm_picker log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

before_mixer = phase(fake_output, "ready as client", "sending CC95 press")
after_mixer = phase(fake_output, "sending CC95 press", "sending CC19 press - opens")
after_open = phase(fake_output, "sending CC19 press - opens", "sending press on pad (0,0)")
after_pick = phase(fake_output, "sending press on pad (0,0)", "sending CC19 press again")
after_close = phase(fake_output, "sending CC19 press again")

color_before_mixer = last_led_color(before_mixer, "0b")
color_after_open = last_led_color(after_open, "0b")
color_after_pick = last_led_color(after_pick, "0b")
color_after_close = last_led_color(after_close, "0b")
row7_before_mixer = last_led_color(before_mixer, "51")
row7_after_open = last_led_color(after_open, "51")
cc19_before_mixer = last_led_color(before_mixer, "13")
cc19_in_mixer = last_led_color(after_mixer, "13")
cc19_open = last_led_color(after_open, "13")
cc19_after_pick = last_led_color(after_pick, "13")
cc19_after_close = last_led_color(after_close, "13")
session_before_mixer = last_led_color(before_mixer, "5f")
session_in_mixer = last_led_color(after_mixer, "5f")

print("pad (0,0) LED before entering mixer submode:", color_before_mixer)
print("pad (0,0) LED once the overlay opened:      ", color_after_open)
print("pad (0,0) LED once the track was armed:     ", color_after_pick)
print("pad (0,0) LED once the overlay closed:      ", color_after_close)
print("pad (0,7) LED before mixer/once overlay open:", row7_before_mixer, row7_after_open)
print("CC19 LED before-mixer/in-mixer/open/after-pick/closed:",
      cc19_before_mixer, cc19_in_mixer, cc19_open, cc19_after_pick, cc19_after_close)
print("CC95 (Session) LED before/in mixer submode:  ", session_before_mixer, session_in_mixer)

check("CC19 (Record Arm) LED is a plain dim scene-launch white before entering mixer submode",
      cc19_before_mixer == ('1e', '1e', '1e'), cc19_before_mixer)
check("CC95 (Session) LED turns orange once mixer submode is entered",
      session_before_mixer == ('00', '7f', '00') and session_in_mixer == ('7f', '40', '00'),
      (session_before_mixer, session_in_mixer))
check("CC19 (Record Arm) LED becomes dim red (its own idle hue) once in mixer submode, before opening",
      cc19_in_mixer == ('14', '00', '00'), cc19_in_mixer)
check("CC19 (Record Arm) LED lit up bright red once the overlay opened",
      cc19_open == ('7f', '00', '00'), cc19_open)
check("Picker row shows pad (0,0) as dim red once opened (the track isn't armed yet)",
      color_after_open == ('14', '00', '00'), color_after_open)
check("Pad (0,7) - outside the picker row - is untouched (not dimmed) once the overlay opened",
      row7_after_open is not None and row7_after_open == row7_before_mixer,
      (row7_before_mixer, row7_after_open))
check("Picker row lights pad (0,0) bright red once the track was armed",
      color_after_pick == ('7f', '00', '00'), color_after_pick)
check("CC19 (Record Arm) LED stayed lit after picking a track (overlay still open)",
      cc19_after_pick == ('7f', '00', '00'), cc19_after_pick)
check("CC19 (Record Arm) LED reverted to its dim mixer-submode hue once a second press closed the overlay",
      cc19_after_close == ('14', '00', '00'), cc19_after_close)
check("Pad (0,0)'s own LED reverted to plain Session view once the overlay closed",
      color_after_close is not None and color_after_close == color_before_mixer,
      (color_before_mixer, color_after_close))

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
