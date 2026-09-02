"""SampleTrack's own Session-view audition path, end to end: Stop Clip
(CC49) triggering/stopping a raw-audio clip rather than a note-based one.
Reuses fake_launchpad_stopclip.c unchanged (it's plain CC/pad events, no
assumption about what the track holds) against
launchpad_sampletrack_session_test.xml (see that fixture's own comment) -
this is what actually proves LaunchpadManager::fireOrTriggerClipStep()'s
SAMPLE branch (PlaybackControlEvent::PLAY_SAMPLE_CLIP, consumed by
Player.cpp on the audio thread) is wired all the way through the real ALSA
+ audio-thread path, not just reachable in-process the way
SampleTrackTests.cpp's own triggerClip() calls are. Same LED-based
checks as verify_launchpad_stopclip.py: pad (0,0)'s own LED brightens on
trigger and reverts once a CC49-held re-press queues, then the quantized
stop actually lands."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_sampletrack_stopclip.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_stopclip")], stderr=fake_log, stdout=fake_log)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_sampletrack_session_test.xml")
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_sampletrack_stopclip.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_stopclip log (against a SampleTrack fixture) ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

before_trigger = phase(fake_output, "ready as client", "sending press on pad (0,0) [note 11] - triggers")
after_trigger = phase(fake_output, "after trigger", "sending CC49 press")
after_stop = phase(fake_output, "after stop should have taken effect")

color_before = last_led_color(before_trigger, "0b")
color_playing = last_led_color(after_trigger, "0b")
color_after_stop = last_led_color(after_stop, "0b")

print("pad (0,0) LED color before trigger:", color_before)
print("pad (0,0) LED color while playing: ", color_playing)
print("pad (0,0) LED color after stop:    ", color_after_stop)

check("Pad (0,0)'s own LED changed once the sample clip was triggered",
      color_before is not None and color_playing is not None and color_before != color_playing,
      (color_before, color_playing))
check("Pad (0,0)'s own LED reverted once the CC49-queued stop took effect",
      color_after_stop is not None and color_after_stop == color_before,
      (color_before, color_after_stop))

# CC49 (led index 0x31) lights up while held.
after_cc49_press = phase(fake_output, "sending CC49 press", "sending press on pad (0,0) while CC49 held")
check("CC49 (Stop Clip) LED lit up while held",
      "03 31 7f 00 00" in after_cc49_press, after_cc49_press)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
