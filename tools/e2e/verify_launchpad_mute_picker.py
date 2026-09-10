"""Regression test for the track-picker overlay's Mute purpose (CC39/Pro
MK3 30): opens the same overlay Stop Clip does, but colored yellow, and
polarity-inverted from Stop Clip/Solo - bright means the track is *not*
already muted, dark means it is (a muted channel reads as dark, not lit).
Deliberately avoids ever triggering playback, unlike
verify_launchpad_stopclip.py - Mute needs no clip playing at all, which
also sidesteps the sandboxed-environment audio/ALSA-sequencer contention
documented there (docs/known_bugs.md) for a cleaner, more reliable signal.

CC39 is one of Session's own mixer-submode radio group (Volume/Pan/Send
A/Send B/Stop Clip/Mute/Solo) - while that submode is off (the connect-
time default), all seven are plain scene-launch triggers and show a
uniform dim white instead of any mixer hue (LaunchpadManager.cpp's own
LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR comment), so this script presses CC95
a second time first to enter mixer submode (Session's own LED turning
orange) before CC39 does anything mute-related at all.

Once in mixer submode, presses CC39 to open the overlay, confirms both
CC39's own LED and the picker row's pad (0,0) (the fixture's only track,
unmuted by default) show bright yellow, while pad (0,7) - row 7, same
column, outside the picker row - stays exactly as Session view's own idle
rendering already had it (the overlay no longer dims anything outside the
picker row itself - see LaunchpadManager.cpp's own
LAUNCHPAD_TRACK_PICKER_ROW comment). Picks column 0 to mute it, confirms
the picker row dims to dark yellow while CC39's own LED stays lit (the
overlay deliberately doesn't auto-close on a pick any more), then presses
CC39 again to close it (mixer submode itself stays on) and confirms both
CC39's LED and pad (0,0) revert."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_mute_picker.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_mute_picker")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_mute_picker's own scripted sequence (6s startup + ~4s of
# drains) takes a bit under 10s - give it comfortable margin.
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_mute_picker.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_mute_picker log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

before_mixer = phase(fake_output, "ready as client", "sending CC95 press")
after_mixer = phase(fake_output, "sending CC95 press", "sending CC39 press - opens")
after_open = phase(fake_output, "sending CC39 press - opens", "sending press on pad (0,0)")
after_pick = phase(fake_output, "sending press on pad (0,0)", "sending CC39 press again")
after_close = phase(fake_output, "sending CC39 press again")

color_before_mixer = last_led_color(before_mixer, "0b")
color_after_open = last_led_color(after_open, "0b")
color_after_pick = last_led_color(after_pick, "0b")
color_after_close = last_led_color(after_close, "0b")
row7_before_mixer = last_led_color(before_mixer, "51")
row7_after_open = last_led_color(after_open, "51")
cc39_before_mixer = last_led_color(before_mixer, "27")
cc39_in_mixer = last_led_color(after_mixer, "27")
cc39_open = last_led_color(after_open, "27")
cc39_after_pick = last_led_color(after_pick, "27")
cc39_after_close = last_led_color(after_close, "27")
session_before_mixer = last_led_color(before_mixer, "5f")
session_in_mixer = last_led_color(after_mixer, "5f")

print("pad (0,0) LED before entering mixer submode:", color_before_mixer)
print("pad (0,0) LED once the overlay opened:      ", color_after_open)
print("pad (0,0) LED once the track was muted:     ", color_after_pick)
print("pad (0,0) LED once the overlay closed:      ", color_after_close)
print("pad (0,7) LED before mixer/once overlay open:", row7_before_mixer, row7_after_open)
print("CC39 LED before-mixer/in-mixer/open/after-pick/closed:",
      cc39_before_mixer, cc39_in_mixer, cc39_open, cc39_after_pick, cc39_after_close)
print("CC95 (Session) LED before/in mixer submode:  ", session_before_mixer, session_in_mixer)

check("CC39 (Mute) LED is a plain dim scene-launch white before entering mixer submode",
      cc39_before_mixer == ('1e', '1e', '1e'), cc39_before_mixer)
check("CC95 (Session) LED turns orange once mixer submode is entered",
      session_before_mixer == ('00', '7f', '00') and session_in_mixer == ('7f', '40', '00'),
      (session_before_mixer, session_in_mixer))
check("CC39 (Mute) LED becomes dim yellow (its own idle hue) once in mixer submode, before opening",
      cc39_in_mixer == ('14', '14', '00'), cc39_in_mixer)
check("CC39 (Mute) LED lit up bright yellow once the overlay opened",
      cc39_open == ('7f', '7f', '00'), cc39_open)
check("Picker row shows pad (0,0) as bright yellow once opened (the track isn't muted)",
      color_after_open == ('7f', '7f', '00'), color_after_open)
check("Pad (0,7) - outside the picker row - is untouched (not dimmed) once the overlay opened",
      row7_after_open is not None and row7_after_open == row7_before_mixer,
      (row7_before_mixer, row7_after_open))
check("Picker row dims pad (0,0) to dark yellow once the track was muted",
      color_after_pick == ('14', '14', '00'), color_after_pick)
check("CC39 (Mute) LED stayed lit after picking a track (overlay still open)",
      cc39_after_pick == ('7f', '7f', '00'), cc39_after_pick)
check("CC39 (Mute) LED reverted to its dim mixer-submode hue once a second press closed the overlay",
      cc39_after_close == ('14', '14', '00'), cc39_after_close)
check("Pad (0,0)'s own LED reverted to plain Session view once the overlay closed",
      color_after_close is not None and color_after_close == color_before_mixer,
      (color_before_mixer, color_after_close))

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
