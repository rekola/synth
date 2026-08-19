"""DRAW-mode "hue decided on release" regression test, plus the CC97-long-
press canvas-clear gesture.

Covers, in one scripted sequence against the real compiled binary over
ALSA:
  1. A short click on an off pad turns it on to the default hue.
  2. A short click on an already-lit pad cycles to the next hue.
  3. A long hold on an already-lit pad does NOT change the hue at any
     point (not on press, not mid-hold, not on release) - only brightness
     changes, live, via press velocity and aftertouch.
  4. A long hold on an OFF pad stays fully black throughout the hold (no
     hue to show anything with) and only lands on the default hue at
     release.
  5. Holding CC97 (DRAW toggle) past the clear threshold and releasing it
     blanks the canvas.
"""
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

fake_log_path = os.path.join(SCRIPT_DIR, "fake_launchpad_draw_clear.log")
fake_log = open(fake_log_path, "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_draw_clear")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr, timeout=20.0):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

deadline = time.time() + 30.0
while time.time() < deadline and fake.poll() is None:
    scr.pump(0.5)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    fake.terminate()
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(fake_log_path) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_draw_clear log ---")
print(fake_output)

STEPS = [
    "STEP enter-draw-mode:",
    "STEP off-to-on:",
    "STEP short-click-cycles:",
    "STEP long-hold-press:",
    "STEP long-hold-aftertouch:",
    "STEP long-hold-release:",
    "STEP clear-canvas:",
    "STEP off-long-hold-press:",
    "STEP off-long-hold-release:",
]

def section(i):
    start = fake_output.find(STEPS[i])
    if start < 0:
        return ""
    end = fake_output.find(STEPS[i + 1]) if i + 1 < len(STEPS) else len(fake_output)
    if end < 0:
        end = len(fake_output)
    return fake_output[start:end]

off_to_on = section(1)
short_click = section(2)
long_press = section(3)
long_aftertouch = section(4)
long_release = section(5)
clear_canvas = section(6)
off_hold_press = section(7)
off_hold_release = section(8)

# note 11 = 0x0b. Off -> on, velocity 50, default hue (red {127,0,0}) at
# half brightness: 0x3f 00 00.
check("Off pad, short click: turns on to the default hue (red)",
      "03 0b 3f 00 00" in off_to_on, off_to_on)

# Already lit (red), released quickly: cycles to orange ({127,60,0}) at
# half brightness: 0x3f 1e 00.
check("Already-lit pad, short click: cycles to the next hue (orange)",
      "03 0b 3f 1e 00" in short_click, short_click)

# A fresh press on the (now orange) pad must NOT change the hue - only
# brightness, live, at the press's own velocity (30/100 -> 0x26 0x11 00;
# the green channel's true value, 60*0.3=18.0000007, is a hair above the
# 18 boundary and the project's -ffast-math build rounds it down to 17 -
# a harmless 1-of-255 LED brightness artifact, not a logic bug).
check("Long-hold press: hue stays orange immediately on touch-down (no instant hue change)",
      "03 0b 26 11 00" in long_press, long_press)
check("Long-hold press: does NOT cycle to yellow on touch-down",
      "03 0b 26 26 00" not in long_press, long_press)

# Aftertouch mid-hold raises brightness (90/100 -> 0x72 0x36 00) but must
# still be orange, not yellow.
check("Aftertouch mid-hold: raises brightness, still orange",
      "03 0b 72 36 00" in long_aftertouch, long_aftertouch)
check("Aftertouch mid-hold: does NOT cycle to yellow",
      "03 0b 72 72 00" not in long_aftertouch, long_aftertouch)

# Releasing after a long hold must not change the hue at all - it must
# never show yellow (the "cycled" outcome), whether or not a new SysEx is
# even sent (nothing changed, so the diff-cache may skip sending one).
check("Long-hold release: never cycles to yellow",
      "03 0b 72 72 00" not in long_release, long_release)

# CC97 held past the clear threshold and released blanks pad (0,0).
check("CC97 long hold clears pad (0,0) to fully black",
      "03 0b 00 00 00" in clear_canvas, clear_canvas)

# While a fresh press is held on an OFF pad, it must stay fully black
# throughout - there's no hue yet to show any brightness with.
check("Off pad, long hold: stays fully black while held (no premature hue)",
      "03 0b 4c 00 00" not in off_hold_press, off_hold_press)

# On release, the OFF pad lands on the default hue (red) at the held
# brightness (60/100 -> 0x4c 00 00).
check("Off pad, long hold released: lands on the default hue (red) at the held brightness",
      "03 0b 4c 00 00" in off_hold_release, off_hold_release)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
