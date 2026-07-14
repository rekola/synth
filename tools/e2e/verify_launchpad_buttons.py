"""Extra-button regression test: sends a CC94 (next-track) press/release
and verifies both that the command actually fired (cursor moved from
track 0 into track 1) and that the corresponding button LEDs were sent as
part of the same combined LED SysEx as the pads."""
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

def find_pattern_row(screen, target="00"):
    for y in range(screen.lines):
        line = screen.display[y]
        if line.strip().startswith(target) and "│" in line:
            return y
    return None

def ctrl(c):
    return bytes([ord(c.lower()) & 0x1F])

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_button.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_button")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn()
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Jump to the very first track (Ctrl-A), a known, stable starting point.
scr.send(ctrl('a'))
scr.pump(1.0)

y = find_pattern_row(scr.screen)
line_before = scr.screen.display[y]
print("row before CC94 (next-track) press:", repr(line_before))
bar1 = line_before.index("│")
bar2 = line_before.index("│", bar1 + 1)
print("track 0/1 boundary at bar columns:", bar1, bar2)

# Wait for the connect handshake + LED refresh, then for the simulated
# device's CC94 press (fires 6s after its own startup).
deadline = time.time() + 15.0
line_after = line_before
while time.time() < deadline:
    scr.pump(0.5)
    y2 = find_pattern_row(scr.screen)
    if y2 is not None:
        line_after = scr.screen.display[y2]

# Confirm the on-screen highlighted/cursor column moved from track 0's span
# into track 1's (i.e. past the second bar) - the observable effect of the
# "next-track" command actually firing.
def highlighted_cols(screen, y, bg="a0ffa0"):
    return [x for x in range(screen.columns) if screen.buffer[y][x].bg == bg]

y_final = find_pattern_row(scr.screen)
cols_final = highlighted_cols(scr.screen, y_final)
print("highlighted columns after CC94 press:", cols_final, "(track 1 starts after column", bar2, ")")
check("CC94 (next-track) moved the cursor from track 0 into track 1",
      cols_final and min(cols_final) > bar2, f"cols={cols_final}, bar2={bar2}")

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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_button.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_button log ---")
print(fake_output)

check("musiceditor sent a Programmer-Mode-enter SysEx to the simulated device",
      "0e 01" in fake_output.replace(",", " "), fake_output)

# Button LED colors: CC93=0x5d, CC94=0x5e (prev/next-track) should be dim
# blue (0,0,60 -> 00 00 3c); sent as part of the same combined LED SysEx as
# the pads.
check("Button LED for CC93 (prev-track) is dim blue",
      "03 5d 00 00 3c" in fake_output, fake_output)
check("Button LED for CC94 (next-track) is dim blue",
      "03 5e 00 00 3c" in fake_output, fake_output)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
