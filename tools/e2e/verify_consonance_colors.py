"""Consonance-hierarchy LED-coloring regression test: loads a 31-EDO song
(songs/song.xml - song-level key=C, temperament=31edo) and asserts the
literal RGB SysEx bytes sent for a handful of hand-verified pads (tonic/
fourth/fifth/major-third-family/minor-third-family) match what
LaunchpadLayout::computeConsonanceLevels + LaunchpadManager's
consonanceColor()/padColor() are expected to produce, idle-dimmed (nothing
is playing yet when this snapshot is taken - LAUNCHPAD_IDLE_LUMINOSITY via
padColor()'s HSL lightness override) - see
verify_launchpad_note_brightness.py for the "note is sounding" case.

Expected bytes were computed independently (a Python replica of
rgbToHsl/hslToRgb/consonanceColor/padColor, not by reading them back out of
the app) and cross-checked against a standalone LaunchpadLayout build, not
hand-derived - see the plan this scheme was implemented from for the
worked math."""
import sys, os, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

SONG = os.path.join(vk.REPO_ROOT, "songs", "song.xml")

results = []
def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_consonance.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Let the Launchpad connect handshake + initial LED refresh happen.
scr.pump(6.0)

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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_consonance.log")) as f:
    log = f.read()

print("--- fake_launchpad log tail ---")
print(log[-4000:])

# padToNoteNumber(x,y) = 11 + x + 10y.
def pad(x, y):
    return 11 + x + 10 * y

# GRID_ORIGIN_X=1, GRID_ORIGIN_Y=3 - pad (1,3) is the logical tonic. 31-EDO:
# fifth=18, fourth=13, major_third=10, minor_third=8. Idle-dimmed bytes
# computed via a Python replica of consonanceColor()+padColor() (see this
# file's own docstring) - hues/saturations reflect several rounds of
# hardware-feedback tuning (tonic yellow-leaning-red, fourth/fifth a close
# amber pair, depth-3 major/minor kept close together, depth-4 pushed far
# apart from depth-3 within the same family).
checks = [
    ("(1,3) tonic (pitch 0) -> yellow-red, idle-dimmed",                      pad(1, 3), "58 49 00"),
    ("(4,4) fifth (pitch 18) -> amber, idle-dimmed",                          pad(4, 4), "50 25 08"),
    ("(3,4) fourth (pitch 13) -> amber, idle-dimmed",                         pad(3, 4), "50 17 08"),
    ("(3,3) major-third family (pitch 10, E) -> depth-3 hue, idle-dimmed",    pad(3, 3), "3f 06 52"),
    ("(2,4) minor-third family (pitch 8, Eb) -> depth-3 hue, idle-dimmed",    pad(2, 4), "19 06 52"),
]

log_norm = log.replace(",", " ")
for name, note, rgb_hex in checks:
    needle = f"03 {note:02x} {rgb_hex}"
    check(name, needle in log_norm, f"looked for '{needle}'")

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
