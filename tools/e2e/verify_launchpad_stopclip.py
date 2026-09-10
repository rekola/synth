"""Regression test for the track-picker overlay Stop Clip (CC49) opens: a
plain press-only toggle, not a held modifier - pressing it shows a picker
row (the grid's bottom row) with one pad per selectable track, colored by
that purpose's own per-track state (bright red = a clip is playing, for
STOP_CLIP), and picking one there stops that track, regardless of which
column originally triggered it
(LaunchpadManager::handleRawButton()/handleTrackPickerPadEvent()). The
overlay deliberately stays open after a pick (every purpose is a toggle,
so several picks in a row can act on several tracks) - only pressing the
same opener button again closes it.

Triggers pool index 7 for the fixture's only track via pad (0,0), confirms
its own LED (note 11 / led index 0x0b) brightens, then presses CC49 (the
picker row's own bright red confirms it sees the track as playing) and
picks that same track's column in the picker row - pad (0,0) again, this
time read as "column 0" rather than "clip index 7" - to queue a stop.
Confirms the picker row's own LED dims to red once the quantized stop
actually takes effect (the fixture's pooled pattern has no explicit
length, so the stop resolves on the very next audition-clock step - see
triggerPooledPatternStep()'s own current_length <= 0 -> 1 fallback), while
CC49's own LED stays lit throughout (the overlay is still open); a second
CC49 press then closes it, reverting both CC49's own LED and pad (0,0)'s
back to plain Session view."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_stopclip.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_stopclip")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# fake_launchpad_stopclip's own scripted sequence (6s startup + ~4.6s of
# drains) takes a bit over 10s - give it comfortable margin.
time.sleep(13)
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_stopclip.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_stopclip log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

before_trigger = phase(fake_output, "ready as client", "sending press on pad (0,0) [note 11] - triggers")
after_trigger = phase(fake_output, "after trigger", "sending CC49 press - opens")
after_cc49_press = phase(fake_output, "sending CC49 press - opens", "sending press on pad (0,0) [note 11] again")
after_pick = phase(fake_output, "sending press on pad (0,0) [note 11] again", "sending CC49 press again")
after_close = phase(fake_output, "sending CC49 press again")

color_before = last_led_color(before_trigger, "0b")
color_playing = last_led_color(after_trigger, "0b")
color_picker_open = last_led_color(after_cc49_press, "0b")
color_after_stop = last_led_color(after_pick, "0b")
color_after_close = last_led_color(after_close, "0b")

print("pad (0,0) LED color before trigger:      ", color_before)
print("pad (0,0) LED color while playing:        ", color_playing)
print("pad (0,0) LED color once overlay opened:  ", color_picker_open)
print("pad (0,0) LED color after queued stop:    ", color_after_stop)
print("pad (0,0) LED color after overlay closed: ", color_after_close)

check("Pad (0,0)'s own LED changed once its pattern was triggered",
      color_before is not None and color_playing is not None and color_before != color_playing,
      (color_before, color_playing))
check("Picker row shows pad (0,0) as bright red once the overlay opened (a clip is playing)",
      color_picker_open == ('7f', '00', '00'), color_picker_open)
check("Picker row dims pad (0,0) to dark red once the picker-queued stop took effect",
      color_after_stop == ('14', '00', '00'), color_after_stop)
check("Pad (0,0)'s own LED reverted to plain Session view once the overlay closed",
      color_after_close is not None and color_after_close == color_before,
      (color_before, color_after_close))

# CC49 (led index 0x31) lights up once the overlay opens and *stays* lit
# through the pick - it no longer auto-closes on a pick - only reverting
# once a second CC49 press explicitly closes it.
check("CC49 (Stop Clip) LED lit up while the track-picker overlay was open",
      "03 31 7f 00 00" in after_cc49_press, after_cc49_press)
check("CC49 (Stop Clip) LED stayed lit after picking a track (overlay still open)",
      "03 31 7f 00 00" in after_pick and "03 31 14 00 00" not in after_pick, after_pick)
check("CC49 (Stop Clip) LED reverted once a second press closed the overlay",
      "03 31 14 00 00" in after_close, after_close)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
