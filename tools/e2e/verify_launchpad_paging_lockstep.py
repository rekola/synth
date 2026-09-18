"""Regression test for the step grid's own page-shift gesture moving
every connected device together, in lockstep, rather than just whichever
one a prev-track/next-track press happened to land on
(LaunchpadManager::handleCommand()'s own comment on repurposing those two
buttons while a clip's step grid is showing). Before this fix, a press
only ever updated the *pressed* device's own DeviceState::drum_edit_page -
with two Launchpads connected, that let them drift onto the very same
page (or any other independent combination), defeating
resetDrumEditPaging()'s own "device i shows page i" tiling that's meant
to split a longer-than-8-step clip across however many are connected
without anyone paging by hand first.

Two simulated devices (fake_launchpad_paging_lockstep) connect to a
4-page (32-step) clip; the "opener" opens it (shift-held pad (0,0)) and,
once every connected device is showing it, presses next-track once - the
"follower" never presses anything itself. Confirms both devices start on
two *different* pages (resetDrumEditPaging()'s own device-order split),
and that after the single press, *both* devices' own pages advanced by
exactly one - not just the opener's."""
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

LIT = "00 6e 14"  # kStepLitColor (0, 110, 20)

def led_dumps(text):
    return re.findall(r"received sysex[^:]*:\s*(f0 00 20 29 02 0c 03(?: [0-9a-f]{2})+ f7)", text)

def shown_page(dump_hex):
    # BD (lane 0) sits at y=0 - led indices 0x0b..0x12 (x=0..7, since
    # padToNoteNumber(x, 0) = 11 + x) - whichever one is lit is the step
    # column currently showing a hit, which the fixture's own layout
    # (its own docstring) made equal to the page number.
    for x in range(8):
        led = 11 + x
        m = re.search(rf"03 {led:02x} ([0-9a-f]{{2}} [0-9a-f]{{2}} [0-9a-f]{{2}})", dump_hex)
        if m and m.group(1) == LIT:
            return x
    return None

def last_page_before_marker(log_text, marker):
    idx = log_text.find(marker)
    segment = log_text[:idx] if idx >= 0 else log_text
    dumps = led_dumps(segment)
    for dump in reversed(dumps):
        page = shown_page(dump)
        if page is not None:
            return page
    return None

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_a.log"), "w")
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_b.log"), "w")
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep"), "A", "opener"], stderr=log_a, stdout=log_a)
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep"), "B", "follower"], stderr=log_b, stdout=log_b)

time.sleep(1)

SONG = os.path.join(SCRIPT_DIR, "launchpad_paging_lockstep_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate(); proc_b.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Both fake devices' own scripted sequences (6s startup + ~3.5s of
# marker/drain steps each) take a bit under 10s - give comfortable margin.
time.sleep(11)
scr.pump(0.5)

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

for proc in (proc_a, proc_b):
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
log_a.close()
log_b.close()

with open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_a.log")) as f:
    text_a = f.read()
with open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_b.log")) as f:
    text_b = f.read()
print("\n--- opener (A) log ---")
print(text_a)
print("\n--- follower (B) log ---")
print(text_b)

check("A sent a Programmer-Mode-enter SysEx", "0e 01" in text_a.replace(",", " "), text_a)
check("B sent a Programmer-Mode-enter SysEx", "0e 01" in text_b.replace(",", " "), text_b)

page_a_before = last_page_before_marker(text_a, "MARKER post-page")
page_b_before = last_page_before_marker(text_b, "MARKER post-page")
print(f"before paging: A shows page {page_a_before}, B shows page {page_b_before}")
check("both devices start on a real page (device-order split from resetDrumEditPaging())",
      page_a_before is not None and page_b_before is not None, (page_a_before, page_b_before))
check("the two devices start on two *different* pages",
      page_a_before is not None and page_a_before != page_b_before, (page_a_before, page_b_before))

end_marker = "\x00"  # sentinel: no marker after this one, read to end of log
page_a_after = last_page_before_marker(text_a + end_marker, end_marker)
page_b_after = last_page_before_marker(text_b + end_marker, end_marker)
print(f"after paging: A shows page {page_a_after}, B shows page {page_b_after}")

if page_a_before is not None and page_a_after is not None:
    check("A's own page advanced by exactly one after its own next-track press",
          page_a_after == page_a_before + 1, (page_a_before, page_a_after))
if page_b_before is not None and page_b_after is not None:
    check("B's own page ALSO advanced by exactly one - moved in lockstep with A, not left behind",
          page_b_after == page_b_before + 1, (page_b_before, page_b_after))

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
