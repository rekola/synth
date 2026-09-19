"""Regression test for the shift+pad combo's own LED feedback while held,
before release ever commits anything: CC91 ("move-row-up") lights full
bright white the instant it's held (dim white beforehand - always lit in
Session view now, never dark, since it has a real meaning there -
LaunchpadManager::refreshLeds()'s own comment), and the pad it's
combined with gets the identical bright-white treatment the moment it's
pressed, still held (DeviceState::row_up_shift_pending_pad) - so a
performer sees both lit together before ever releasing, confirming what
the release will do.

Verified through raw LED bytes, not terminal text."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_highlight.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_shift_highlight")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_shift_stepgrid_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Until the simulator has finished its scripted sequence.
scr.wait_for_exit(fake, 11)
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_shift_highlight.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_shift_highlight log ---")
print(fake_output)

check("synth sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

before_shift = phase(fake_output, "ready as client", "sending CC91 press")
shift_only = phase(fake_output, "sending CC91 press", "sending press (not release) on pad")
shift_and_pad = phase(fake_output, "sending press (not release) on pad", "sending release on pad")
after_release = phase(fake_output, "sending CC91 release", None)

cc91_before = last_led_color(before_shift, "5b")
cc91_held = last_led_color(shift_only, "5b")
pad_before_press = last_led_color(shift_only, "0b")
pad_while_pending = last_led_color(shift_and_pad, "0b")
cc91_still_held = last_led_color(shift_and_pad, "5b")
cc91_after = last_led_color(after_release, "5b")

print("CC91 LED before ever holding shift:      ", cc91_before)
print("CC91 LED while held, no pad yet:         ", cc91_held)
print("Pad (0,0) LED while held, no pad yet:    ", pad_before_press)
print("Pad (0,0) LED while held AND pad pressed:", pad_while_pending)
print("CC91 LED while still held (with pad):    ", cc91_still_held)
print("CC91 LED after release:                  ", cc91_after)

check("CC91 shows dim white before ever being held (always lit in Session now, not dark)",
      cc91_before == ('1e', '1e', '1e'), cc91_before)
check("CC91 lights full bright white the instant it's held",
      cc91_held == ('7f', '7f', '7f'), cc91_held)
check("Pad (0,0) is not yet highlighted while shift is held but the pad itself hasn't been pressed",
      pad_before_press != ('7f', '7f', '7f'), pad_before_press)
check("Pad (0,0) lights full bright white once pressed while shift is still held",
      pad_while_pending == ('7f', '7f', '7f'), pad_while_pending)
check("CC91 stays bright white the whole time the pad is also held",
      cc91_still_held == ('7f', '7f', '7f'), cc91_still_held)
check("CC91 reverts to dim white once shift is released",
      cc91_after == ('1e', '1e', '1e'), cc91_after)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
