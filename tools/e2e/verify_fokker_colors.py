"""Fokker/Archiphone LED-coloring regression test: loads a 31-EDO song
(songs/song.xml - song-level key=C, temperament=31edo) and asserts the
literal RGB SysEx bytes sent for a handful of hand-verified pads
(tonic/diatonic/sharp/flat/diesis) match the documented FOKKER_* color
constants in LaunchpadManager.cpp."""
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

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_fokker.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
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

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_fokker.log")) as f:
    log = f.read()

print("--- fake_launchpad log tail ---")
print(log[-4000:])

# padToNoteNumber(x,y) = 11 + x + 10y.
# 31-EDO, tonic=C(pitch0), degrees{0,5,10,13,18,23,28}. T=5,S=3.
def pad(x, y):
    return 11 + x + 10 * y

checks = [
    ("(0,0) tonic (pitch 0) -> bright green",        pad(0, 0), "00 7f 00"),
    ("(2,0) diatonic E (pitch 10) -> bright white",  pad(2, 0), "7f 7f 7f"),
    ("(0,1) flat Db (pitch 3) -> dim amber",         pad(0, 1), "46 23 00"),
    ("(6,1) sharp C# (pitch 2) -> dim red",          pad(6, 1), "46 00 00"),
    ("(4,4) diesis C-half-sharp (pitch 1) -> med blue", pad(4, 4), "00 46 7f"),
    ("(0,4) diesis E#/Fb (pitch 12) -> med blue",    pad(0, 4), "00 46 7f"),
]

log_norm = log.replace(",", " ")
for name, note, rgb_hex in checks:
    needle = f"03 {note:02x} {rgb_hex}"
    check(name, needle in log_norm, f"looked for '{needle}'")

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
