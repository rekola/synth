"""Regression test for the step grid's own page-shift gesture moving
every connected device together, in lockstep, rather than just whichever
one a pad-prev-track/pad-next-track press happened to land on
(LaunchpadManager::handleCommand()'s own comment on repurposing those two
buttons while a clip's step grid is showing). Before this fix, a press
only ever updated the *pressed* device's own DeviceState::drum_edit_step_offset -
with two Launchpads connected, that let them drift onto the very same
page (or any other independent combination), defeating
resetStepGridView()'s own "device i shows page i" tiling that's meant
to split a longer-than-32-step clip across however many are connected
without anyone paging by hand first.

Two simulated devices (fake_launchpad_paging_lockstep) connect to a
3-page (96-step) clip; the "opener" opens it (shift-held pad (0,0)) and,
once every connected device is showing it, presses pad-next-track once - the
"follower" never presses anything itself. Confirms both devices start on
two *different* pages (resetStepGridView()'s own device-order split),
and that after the single press, *both* devices' own windows scrolled by
the step grid's scroll step (4) - not just the opener's."""
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

SCROLL_STEP = 4  # kStepGridScrollStep
WINDOW = 32      # steps a device shows (the top four pad rows)

# The fixture's BD hits sit at rows 5, 40 and 70; the set of lit steps in a
# 32-step window tells where that window starts, with no playhead to read.
HITS = (5, 40, 70)
OFFSET_BY_LIT = {}
for offset in (0, SCROLL_STEP, WINDOW, WINDOW + SCROLL_STEP):
    lit = frozenset(row - offset for row in HITS if offset <= row < offset + WINDOW)
    assert lit and lit not in OFFSET_BY_LIT
    OFFSET_BY_LIT[lit] = offset

def shown_page(dump_hex):
    """The step offset a device's step view shows, or None. Step s is pad
    (s % 8, 4 + s // 8) - led index 11 + x + 10*y."""
    lit = set()
    for step in range(WINDOW):
        led = 11 + step % 8 + 10 * (4 + step // 8)
        m = re.search(rf"03 {led:02x} ([0-9a-f]{{2}} [0-9a-f]{{2}} [0-9a-f]{{2}})", dump_hex)
        if m and m.group(1) == LIT:
            lit.add(step)
    return OFFSET_BY_LIT.get(frozenset(lit))

def shown_pages(log_text):
    """Every step window a device's LED frames showed, in order - the first
    is where opening the clip put it, the last where paging left it."""
    pages = [shown_page(dump) for dump in led_dumps(log_text)]
    return [page for page in pages if page is not None]

log_a = open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_a.log"), "w")
log_b = open(os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep_b.log"), "w")
proc_a = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep"), "A", "opener"], stderr=log_a, stdout=log_a)
proc_b = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad_paging_lockstep"), "B", "follower"], stderr=log_b, stdout=log_b)

time.sleep(0.3)  # the simulator registers with ALSA before synth scans for it

SONG = os.path.join(SCRIPT_DIR, "launchpad_paging_lockstep_test.xml")
pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    proc_a.terminate(); proc_b.terminate()
    os.kill(pid, 9)
    sys.exit(1)

# Until both simulators have finished their scripted sequences.
scr.wait_for_exit(proc_a, 11)
scr.wait_for_exit(proc_b, 5)
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

pages_a, pages_b = shown_pages(text_a), shown_pages(text_b)
page_a_before = pages_a[0] if pages_a else None
page_b_before = pages_b[0] if pages_b else None
print(f"before paging: A shows step {page_a_before}, B shows step {page_b_before}")
check("both devices start on a real page (device-order split from resetStepGridView())",
      page_a_before is not None and page_b_before is not None, (page_a_before, page_b_before))
check("the two devices start on two *different* pages",
      page_a_before is not None and page_a_before != page_b_before, (page_a_before, page_b_before))

page_a_after = pages_a[-1] if pages_a else None
page_b_after = pages_b[-1] if pages_b else None
print(f"after paging: A shows step {page_a_after}, B shows step {page_b_after}")

if page_a_before is not None and page_a_after is not None:
    check("A's own window scrolled by one step size after its own pad-next-track press",
          page_a_after == page_a_before + SCROLL_STEP, (page_a_before, page_a_after))
if page_b_before is not None and page_b_after is not None:
    check("B's own window ALSO scrolled by one step size - moved in lockstep with A, not left behind",
          page_b_after == page_b_before + SCROLL_STEP, (page_b_before, page_b_after))

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
