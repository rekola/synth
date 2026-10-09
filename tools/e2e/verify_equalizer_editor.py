"""The equalizer editor: opened with M-x edit-equalizer, it shows the band
table and the response curve, keys and the mouse edit the selected band, and
Ctrl-G closes it. Verified through the terminal text."""
import sys, os, re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

results = []

def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

def mx(scr, command):
    scr.send(b"\x1b")
    scr.pump(0.3)
    scr.send(b"x")
    scr.pump(0.3)
    scr.send(command.encode() + b"\r")
    scr.pump(0.8)

pid, fd = vk.spawn(os.path.join(SCRIPT_DIR, "equalizer_editor_test.xml"))
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    os.kill(pid, 9)
    sys.exit(1)

mx(scr, "edit-equalizer")
screen = scr.dump()
check("the editor opens", "Equalizer" in screen, screen)
check("the table shows the song's bands", "900 Hz" in screen and "-8.0 dB" in screen and "Q 2.50" in screen and "+6.0 dB" in screen, screen)
check("the curve is drawn (braille)", any(0x2800 < ord(c) <= 0x28ff for c in screen), screen)
check("the frequency axis is labelled", "1k" in screen and "10k" in screen, screen)

# Band 1 is selected first (first active band is the low shelf, band 2).
scr.send(b"\x1b[1;2C")  # Shift+Right: nudges frequency up
scr.pump(0.5)
scr.send(b"\x1b[A")      # Up: +0.5 dB
scr.pump(0.5)
screen = scr.dump()
check("Up raises the selected band's gain", "+6.5 dB" in screen, screen)

scr.send(b"4")           # select band 4 (the -8 dB peak)
scr.pump(0.4)
scr.send(b"\x1b[B")      # Down
scr.pump(0.4)
screen = scr.dump()
check("a digit selects a band and Down lowers it", "-8.5 dB" in screen, screen)

scr.send(b" ")           # toggle band 4 off
scr.pump(0.4)
check("space switches the band off", "off" in scr.dump(), scr.dump())

scr.send(b"\x07")  # Ctrl-G closes (a bare Escape is held back by the terminal layer in a pty)
scr.pump(0.6)
screen = scr.dump()
check("the editor closes", "Equalizer ──" not in screen, screen)

# Reopen: the edits were kept in the song.
mx(scr, "edit-equalizer")
screen = scr.dump()
check("edits persist in the song", "+6.5 dB" in screen and "-8.5 dB" in screen, screen)
# A drag in the plot moves the nearest band (band 3, a 250 Hz peak, is the
# closest to this click) and one undo step takes the whole drag back.
def mouse(kind, x, y, button=0):
    scr.send(f"\x1b[<{button};{x};{y}{kind}".encode())
    scr.pump(0.3)

check("band 3 starts at 250 Hz", "250 Hz" in scr.dump(), scr.dump())
mouse("M", 60, 14)
mouse("M", 62, 10, 32)
mouse("M", 66, 9, 32)
mouse("m", 66, 9)
screen = scr.dump()
check("dragging moves the band", "250 Hz" not in screen, screen)
scr.send(b"\x07")
scr.pump(0.4)
mx(scr, "undo")
mx(scr, "edit-equalizer")
screen = scr.dump()
check("one undo takes the whole drag back", "250 Hz" in screen, screen)
scr.send(b"\x07")
scr.pump(0.4)

os.kill(pid, 9)
failed = [n for n, ok in results if not ok]
print(f"{len(results) - len(failed)}/{len(results)} passed")
sys.exit(1 if failed else 0)
