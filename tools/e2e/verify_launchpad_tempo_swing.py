"""Tempo and Swing views on the Launchpad: shift + Send B opens Tempo
(blue / white), shift + Stop Clip switches to Swing (orange / white), CC91 /
CC92 are the up / down arrows and CC95 leaves. The value is drawn as a number
on the pads (LaunchpadLayout::renderNumber()): the tens digit in white, the
others in the view's colour.

Verified through raw LED bytes. The song is launchpad_session_test.xml:
tempo 120 and the default swing, 50.

Pad (x, y) is LED index 0x0b + 10 * y + x. The glyph rows start at pad row y
= 6, so the tens digit's top row is on pads 0x49 (x=2), 0x4a and 0x4b, and
the units digit's top row on 0x4d (x=6) and 0x4e."""
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
    end = text.find(end_marker, start + 1) if end_marker else len(text)
    if end_marker and end < 0:
        end = len(text)
    return text[start:end]

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_tempo_swing.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_tempo_swing")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_session_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

scr.wait_for_exit(fake, 15)
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_tempo_swing.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_tempo_swing log ---")
print(fake_output)

WHITE = ('7f', '7f', '7f')
BLUE = ('00', '32', '7f')
ORANGE = ('7f', '37', '00')
BLACK = ('00', '00', '00')

tempo_view = phase(fake_output, "step: shift + Send B", "step: CC92")
tempo_down = phase(fake_output, "step: CC92", "step: CC91 up")
tempo_up = phase(fake_output, "step: CC91 up", "step: shift + Stop Clip")
swing_view = phase(fake_output, "step: shift + Stop Clip", "step: CC95")
after_leave = phase(fake_output, "step: CC95", None)

# Tempo 120: a white 2 (tens digit, top row ###) and a blue 0 beside it.
check("Tempo view: the tens digit's top row is white", all(last_led_color(tempo_view, i) == WHITE for i in ("49", "4a", "4b")), [last_led_color(tempo_view, i) for i in ("49", "4a", "4b")])
check("Tempo view: the units digit is blue", last_led_color(tempo_view, "4d") == BLUE, last_led_color(tempo_view, "4d"))
check("Tempo view: the up arrow (CC91) and down arrow (CC92) are lit", last_led_color(tempo_view, "5b") not in (None, BLACK) and last_led_color(tempo_view, "5c") not in (None, BLACK))
check("Tempo view: Send B (CC59) is lit blue", last_led_color(tempo_view, "3b") == ('00', '32', '7f'), last_led_color(tempo_view, "3b"))

# 119: the tens digit becomes a 1, whose top row (" # ") leaves x=2 dark.
check("Down arrow: tempo 119 redraws the tens digit as a 1", last_led_color(tempo_down, "49") == BLACK and last_led_color(tempo_down, "4a") == WHITE, [last_led_color(tempo_down, i) for i in ("49", "4a")])
check("Up arrow tap: back to tempo 120", last_led_color(tempo_up, "49") == WHITE, last_led_color(tempo_up, "49"))

# Swing 50: a white 5 and an orange 0.
check("Swing view: the tens digit's top row is white", all(last_led_color(swing_view, i) == WHITE for i in ("49", "4a", "4b")), [last_led_color(swing_view, i) for i in ("49", "4a", "4b")])
check("Swing view: the units digit is orange", last_led_color(swing_view, "4d") == ORANGE, last_led_color(swing_view, "4d"))
check("Swing view: Stop Clip (CC49) is lit orange", last_led_color(swing_view, "31") == ORANGE, last_led_color(swing_view, "31"))

check("CC95 leaves the view: the number is gone from the pads", last_led_color(after_leave, "49") != WHITE, last_led_color(after_leave, "49"))

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
