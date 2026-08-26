"""Regression test for the cross-tuning/cross-song clipboard fixes
(plans/drum-machine-per-scene-patterns.md's Phase -1): a Note::getValue()
means something different under a different tuning (GM percussion key vs.
a pitched scale degree), so pasting a copied cell onto a track with a
different tuning must be refused rather than silently reinterpreted -
and, since this app supports several songs open at once as Emacs-style
buffers, that check has to keep working once the active song itself has
changed underneath the clipboard (PatternMatrix's own yank used to
silently no-op after switching buffers at all - see the plan's own root
cause).

Navigates PatternMatrix with arrow keys only, never Enter - Enter commits
the cell under the cursor and hands focus to PatternEditor (by design),
which would silently redirect every subsequent keystroke there instead of
exercising PatternMatrix's own clipboard at all.

Three cases, driving both cross_tuning_paste_test.xml (2 tracks: 0 =
pitched, 1 = percussion) and cross_tuning_paste_test_song_b.xml (a second,
independent song, 1 pitched track) as separate buffers:
  1. Cut the percussion cell (track 1, row 0), move the cursor onto the
     pitched track (track 0, same song) and yank - PatternMatrix always
     pastes back into the track a same-song copy came from regardless of
     where the cursor is (plans/pattern-matrix.md's own design), so this
     lands back on track 1, not track 0 - the tuning check can never
     actually refuse a same-song paste here, only a cross-song one (case 3
     below) can put the cursor on a genuinely different-tuned track.
  2. Yank the same clipboard onto a different row of the percussion track
     - must succeed, confirming the clipboard is still intact after case 1.
  3. Open the second song as a new buffer and attempt to yank the
     still-held percussion cell onto its own (pitched) track 0 - must
     refuse via the tuning check, not via the old "track doesn't exist in
     this song -> silent no-op" bug (there would be no log message at all
     in that failure mode).
"""
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

SONG_A = os.path.join(SCRIPT_DIR, "cross_tuning_paste_test.xml")
SONG_B_BASENAME = "cross_tuning_paste_test_song_b.xml"

results = []

def check(name, ok, extra=None):
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and extra:
        print("  ", extra)

def status(scr):
    for l in reversed(scr.dump().splitlines()):
        if l.strip():
            return l.strip()
    return None

def pitched_cell(scr):
    return scr.dump().splitlines()[2][0]

def perc_cell(scr, scene_row):
    return scr.dump().splitlines()[2 + scene_row][2]

pid, fd = vk.spawn(SONG_A)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    os.kill(pid, 9)
    sys.exit(1)

# Focus PatternMatrix once; cursor starts at (scene 0, track 0).
scr.send(b"\x1b[<0;1;2M"); scr.pump(0.2)
scr.send(b"\x1b[<0;1;2m"); scr.pump(0.4)

# --- 1: move onto the percussion column (track 1), cut it, move onto the
# pitched column (track 0) and yank - same-song yank always targets the
# track the copy came from (track 1), not the cursor's column, so this
# lands back on the percussion cell and the pitched one stays untouched ---
scr.send(b"\x1b[C"); scr.pump(0.2)  # -> track 1 (percussion)
scr.send(vk.ctrl('w')); scr.pump(0.5)
check("kill-region on the percussion cell shows 'Cell killed'", status(scr) == "Cell killed", status(scr))
check("percussion cell is empty after the cut", perc_cell(scr, 0) == " ", perc_cell(scr, 0))

scr.send(b"\x1b[D"); scr.pump(0.2)  # -> track 0 (pitched)
scr.send(vk.ctrl('y')); scr.pump(0.5)
check("yanking with the cursor on the pitched track still succeeds (targets the original track)",
      status(scr) == "Cell pasted", status(scr))
check("the pitched cell stays untouched", pitched_cell(scr) == " ", pitched_cell(scr))
check("the percussion cell (the copy's real origin) got its content back", perc_cell(scr, 0) != " ", perc_cell(scr, 0))

# --- 2: same clipboard, move onto the percussion column's next row - same
# tuning, must succeed (proves the refusal above didn't drop the clipboard) ---
scr.send(b"\x1b[C"); scr.pump(0.2)  # -> track 1 (percussion)
scr.send(b"\x1b[B"); scr.pump(0.2)  # -> row 1
scr.send(vk.ctrl('y')); scr.pump(0.5)
check("yanking the same cell onto a same-tuning track succeeds", status(scr) == "Cell pasted", status(scr))
check("the percussion note landed on row 1", perc_cell(scr, 1) != " ", perc_cell(scr, 1))

# --- 3: open a second, independent song (only a pitched track 0), attempt
# to yank the still-held percussion cell onto it - must refuse via the
# tuning check, not silently no-op via the old cross-song track-id bug ---
scr.send(vk.ctrl('x')); scr.pump(0.2)
scr.send(vk.ctrl('f')); scr.pump(0.3)
for ch in SONG_B_BASENAME:
    scr.send(ch.encode()); scr.pump(0.02)
scr.send(b"\r"); scr.pump(0.8)
opened = any("opened" in l.lower() for l in scr.dump().splitlines())
check("open-song switches to the second buffer", opened, scr.dump())

scr.send(vk.ctrl('y')); scr.pump(0.5)
check("cross-song yank onto a differently-tuned track is refused (not a silent no-op)",
      status(scr) == "Cannot paste: incompatible tuning", status(scr))
check("song B's own pitched cell stays empty", pitched_cell(scr) == " ", pitched_cell(scr))

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results) - n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
