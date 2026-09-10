"""Regression test for PatternEditor's own clipboard cross-tuning fix
(ClipboardEntry::track_tunings). PatternEditor's yank always targets
wherever the cursor currently is, not the track a copy came from - so a
same-song cross-tuning mismatch is directly reachable here, and is the
main risk this fix addresses.

Uses ArrangementGrid purely as a reliable "teleport" - its Enter commits
the cursor cell and hands focus to PatternEditor at an exact (track,
section, row) position (UI::commitOverviewCell()) - rather than guessing
how many arrow-key presses PatternEditor's own sub-column navigation
needs to cross from one track to another.

cross_tuning_paste_test.xml: track 0 pitched, track 1 percussion (one
populated cell, section 0, row 0).
  1. Teleport onto track 1/row 0, cut it (kill-region, degenerates to the
     single cell under the cursor).
  2. Teleport onto track 0/row 0 (pitched) and yank - must refuse, must
     leave the pitched cell empty.
  3. Teleport back onto track 1/row 0, move to row 1, yank - same tuning,
     must succeed (confirms the refusal above didn't drop the clipboard).
"""
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import harness as vk

SONG = os.path.join(SCRIPT_DIR, "cross_tuning_paste_test.xml")

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

def editor_row(scr, row_offset):
    return scr.dump().splitlines()[8 + row_offset]

pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    os.kill(pid, 9)
    sys.exit(1)

matrix_idx = 0

def teleport(target_idx):
    """Move ArrangementGrid's cursor to track `target_idx` and commit
    (Enter) - hands focus to PatternEditor positioned exactly there."""
    global matrix_idx
    scr.send(b"\x1b[<0;1;2M"); scr.pump(0.2)
    scr.send(b"\x1b[<0;1;2m"); scr.pump(0.4)
    delta = target_idx - matrix_idx
    seq = b"\x1b[C" if delta > 0 else b"\x1b[D"
    for _ in range(abs(delta)):
        scr.send(seq); scr.pump(0.15)
    matrix_idx = target_idx
    scr.send(b"\r"); scr.pump(0.5)

# --- 1: cut the percussion cell ---
teleport(1)
scr.send(vk.ctrl('w')); scr.pump(0.5)
check("kill-region shows 'Region killed'", status(scr) == "Region killed", status(scr))
check("percussion cell is empty after the cut", "BD" not in editor_row(scr, 0), editor_row(scr, 0))

# --- 2: attempt to yank onto the pitched track - must refuse ---
teleport(0)
scr.send(vk.ctrl('y')); scr.pump(0.5)
check("yanking a percussion cell onto a pitched track is refused",
      status(scr) == "Cannot paste: incompatible tuning", status(scr))
check("the pitched cell stays empty after the refused yank", "BD" not in editor_row(scr, 0), editor_row(scr, 0))

# --- 3: yank onto a different row of the percussion track - same tuning,
# must succeed ---
teleport(1)
scr.send(b"\x1b[B"); scr.pump(0.3)  # row 1
scr.send(vk.ctrl('y')); scr.pump(0.5)
check("yanking the same clipboard onto a same-tuning track succeeds", status(scr) == "Yanked", status(scr))
check("the percussion note landed on row 1", "BD" in editor_row(scr, 1), editor_row(scr, 1))

try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass

n_fail = sum(1 for _, ok in results if not ok)
print(f"\n{len(results) - n_fail}/{len(results)} checks passed")
sys.exit(1 if n_fail else 0)
