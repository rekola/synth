"""Regression test for LaunchpadIO::clearAllLeds(), its destructor's own
last act: quitting synth (Emacs's own save-buffers-kill-terminal binding,
C-x C-c) should send one final LED-lighting SysEx that blanks every pad
and extra-button LED, so a connected Launchpad doesn't sit there still
showing whatever Session view/step grid/etc. happened to be lit the
moment the app quit - rather than just closing the ALSA connection and
leaving the device's last-sent colors standing.

Spawns against a fixture with real Session content (two tracks, each
with a populated clip - launchpad_scene_row_test.xml, reused from the
scene-row test since it already has exactly this shape), confirms the
startup LED dump actually lights something (proving there's real content
to clear, not just coincidentally-already-dark LEDs), then quits
gracefully via C-x C-c and confirms the very last SysEx the fake device
received is an all-black lighting message covering every colorspec."""
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

def led_dumps(text):
    # Each "received sysex ... (N bytes): f0 ... f7" line that's an
    # LED-lighting message (header "f0 00 20 29 02 0c 03" - device id 0c
    # is Launchpad X, matching this fixture's own fake device identity).
    return re.findall(r"received sysex[^:]*:\s*(f0 00 20 29 02 0c 03(?: [0-9a-f]{2})+ f7)", text)

def is_all_black(dump_hex):
    bytes_ = dump_hex.split(" ")
    # Skip the 7-byte header (f0 00 20 29 02 0c 03) and trailing f7;
    # every colorspec here is 5 bytes: type(03=STATIC_RGB), led_index,
    # r, g, b - clearAllLeds() only ever sends STATIC_RGB black.
    body = bytes_[7:-1]
    if len(body) % 5 != 0 or len(body) == 0:
        return False, "unexpected message shape"
    for i in range(0, len(body), 5):
        typ, led, r, g, b = body[i:i + 5]
        if typ != "03" or r != "00" or g != "00" or b != "00":
            return False, f"non-black colorspec at offset {i}: {body[i:i + 5]}"
    return True, f"{len(body) // 5} colorspecs, all black"

fake_log = open(os.path.join(SCRIPT_DIR, "fake_launchpad_clear_on_exit.log"), "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_clear_on_exit")], stderr=fake_log, stdout=fake_log)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_scene_row_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

scr.wait(3)
scr.pump(0.5)

scr.send(vk.ctrl('x'))
scr.pump(0.3)
scr.send(vk.ctrl('c'))
scr.pump(1.0)
if "discard and quit" in scr.dump():
    scr.send(b"y\r")

wpid = 0
end = time.time() + 10.0
while time.time() < end:
    try:
        wpid, status = os.waitpid(pid, os.WNOHANG)
    except ChildProcessError:
        wpid = pid
    if wpid == pid:
        break
    scr.pump(0.2)
check("C-x C-c (quit) terminates the process", wpid == pid)

time.sleep(0.5)
try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    fake.wait(timeout=5)
except subprocess.TimeoutExpired:
    fake.kill()
fake_log.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_clear_on_exit.log")) as f:
    fake_output = f.read()
print("\n--- fake_launchpad_clear_on_exit log ---")
print(fake_output)

dumps = led_dumps(fake_output)
check(f"the fake device received at least two LED-lighting messages (startup + quit)", len(dumps) >= 2, f"got {len(dumps)}")

if len(dumps) >= 1:
    first_ok, first_reason = is_all_black(dumps[0])
    check("the startup LED dump lights real content (not already all black)", not first_ok, first_reason)

if len(dumps) >= 2:
    last_ok, last_reason = is_all_black(dumps[-1])
    check("the very last LED message sent before quitting blanks every colorspec", last_ok, last_reason)

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
