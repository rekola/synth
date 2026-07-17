"""Active-voice LED brightness regression test: loads a dedicated fixture
(launchpad_brightness_test.xml - a single 31-EDO oscilator track holding one
sustained note at row 0, no envelope/decay) and asserts the Launchpad grid
is at idle brightness (LAUNCHPAD_IDLE_BRIGHTNESS) before playback starts,
and that starting playback (Space) brightens at least one grid pad above
idle once the note is actually sounding - exercising the normal pattern-
playback path (SongState::render -> InstrumentTrackState voices ->
PlaybackInfo -> LaunchpadManager::refresh), not just live pad presses.

Uses the play/pause button's own LED (index 98, bright green iff playing -
see LaunchpadManager::refreshLeds) as the ground-truth "is playing" signal
instead of screen-scraping, since it's driven by the exact PlaybackInfo
snapshot this test cares about anyway."""
import sys, os, re, subprocess, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

SONG = os.path.join(SCRIPT_DIR, "launchpad_brightness_test.xml")
LOG_PATH = os.path.join(SCRIPT_DIR, "fake_launchpad_brightness.log")
PLAY_BUTTON_LED = 98

# Grid pads only (padToNoteNumber(x,y) = 11 + x + 10y for x,y in 0..7);
# excludes the extra-button LEDs (91-99, right column, etc.) which aren't
# part of this feature.
GRID_LEDS = {11 + x + 10 * y for x in range(8) for y in range(8)}

results = []
def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

def all_colors(log_text):
    """The most recently sent color for every LED index mentioned in the
    log, as {led_index: (r,g,b)}.

    Each combined RGB SysEx is "f0 00 20 29 02 <dev> 03 (03 <idx> r g b)* f7"
    (see LaunchpadProtocol::buildRgbLedSysEx) - the lighting-type byte (03)
    right before the first tuple is indistinguishable, token-wise, from a
    tuple's own leading 03 marker, so a naive "03 xx xx xx xx" regex
    misaligns on the very first tuple in every dump (matching the preceding
    header byte as if it were that tuple's marker) and silently drops it.
    Isolate each full message first, then walk its tuples by fixed-width
    token position instead."""
    log_text = log_text.replace(",", " ")
    colors = {}
    for msg in re.finditer(r"f0 00 20 29 02 [0-9a-f]{2} 03 ([0-9a-f ]+?) f7", log_text):
        tokens = msg.group(1).split()
        for i in range(0, len(tokens) - 4, 5):
            if tokens[i] != "03":
                continue
            led = int(tokens[i + 1], 16)
            colors[led] = tuple(int(tokens[i + 2 + j], 16) for j in range(3))
    return colors

def is_playing(colors):
    g = colors.get(PLAY_BUTTON_LED)
    return g is not None and g[1] > 100  # bright green (0,127,0) vs dim (20,20,20)

def any_grid_pad_above_idle(colors):
    # Idle pads are their base color blended 50% towards black
    # (LaunchpadManager::padColor); the brightest base channel used by any
    # Fokker/percussion color is 127, so no idle channel exceeds 63 (0x3f) -
    # a pad with a voice sounding ramps towards its 50%-white blend instead,
    # comfortably clearing that bound (e.g. the tonic's green channel alone
    # already reaches 127 at full loudness).
    return any(max(rgb) > 0x3f for led, rgb in colors.items() if led in GRID_LEDS)

fake_log = open(LOG_PATH, "w")
fake = subprocess.Popen([os.path.join(SCRIPT_DIR, "fake_launchpad")], stderr=fake_log, stdout=fake_log)
time.sleep(1)

pid, fd = vk.spawn(song=SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("musiceditor not ready")
    fake.terminate()
    os.kill(pid, 9)
    sys.exit(1)

scr.pump(2.0)
fake_log.flush()
colors = all_colors(open(LOG_PATH).read())

# The fixture's note plays as soon as the transport runs, so stop it first
# (retrying the toggle if needed) to get a clean idle baseline.
stop_deadline = time.time() + 6.0
while is_playing(colors) and time.time() < stop_deadline:
    scr.send(b" ")
    scr.pump(1.0)
    fake_log.flush()
    colors = all_colors(open(LOG_PATH).read())
check("Playback is stopped before the idle snapshot", not is_playing(colors), colors.get(PLAY_BUTTON_LED))

scr.pump(2.0)
fake_log.flush()
colors = all_colors(open(LOG_PATH).read())
check("All grid pads are at idle brightness (<=0x3f per channel) before playback starts",
      not any_grid_pad_above_idle(colors),
      {led: rgb for led, rgb in colors.items() if led in GRID_LEDS})

# Start playback - the sustained note at row 0 should now become audible.
start_deadline = time.time() + 6.0
while not is_playing(colors) and time.time() < start_deadline:
    scr.send(b" ")
    scr.pump(1.0)
    fake_log.flush()
    colors = all_colors(open(LOG_PATH).read())
check("Playback started", is_playing(colors), colors.get(PLAY_BUTTON_LED))

brightened = False
deadline = time.time() + 10.0
while time.time() < deadline:
    scr.pump(0.5)
    fake_log.flush()
    colors = all_colors(open(LOG_PATH).read())
    if any_grid_pad_above_idle(colors):
        brightened = True
        break
check("At least one grid pad brightened above idle once the sustained note started playing",
      brightened, {led: rgb for led, rgb in colors.items() if led in GRID_LEDS})

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

with open(LOG_PATH) as f:
    log = f.read()
print("--- fake_launchpad log tail ---")
print(log[-4000:])

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results)-n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
