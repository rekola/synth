"""Regression test for PatternEditor's own clipboard cross-tuning fix
(ClipboardEntry::track_tunings). PatternEditor's yank always targets
wherever the cursor currently is, not the track a copy came from - so a
same-song cross-tuning mismatch is directly reachable here, and is the
main risk this fix addresses.

Moves between tracks with Ctrl+Right/Left (jump a whole track, landing on
its first column) rather than guessing how many plain arrow presses the
sub-column navigation needs to cross from one track to another.

cross_tuning_paste_test.xml: track 0 pitched, track 1 percussion (one
populated cell, section 0, row 0).
  1. Onto track 1/row 0, cut it (kill-region, degenerates to the single
     cell under the cursor).
  2. Onto track 0/row 0 (pitched) and yank - must refuse, must leave the
     pitched cell empty.
  3. Back onto track 1, down to row 1, yank - same tuning, must succeed
     (confirms the refusal above didn't drop the clipboard).
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
    """The pattern editor's row `row_offset` (its first line labelled so)."""
    label = f" {row_offset:02x} │"
    return next((l for l in scr.dump().splitlines() if l.startswith(label)), "")

CTRL_RIGHT, CTRL_LEFT, DOWN = b"\x1b[1;5C", b"\x1b[1;5D", b"\x1b[B"

pid, fd = vk.spawn(SONG)
scr = vk.Screen(fd)
if not vk.wait_ready(scr):
    print("synth not ready")
    os.kill(pid, 9)
    sys.exit(1)
vk.other_window(scr)  # the pattern editor, on track 0/row 0

# --- 1: cut the percussion cell ---
scr.send(CTRL_RIGHT); scr.pump(0.3)
scr.send(vk.ctrl('w')); scr.pump(0.5)
check("kill-region shows 'Region killed'", status(scr) == "Region killed", status(scr))
check("percussion cell is empty after the cut", "BD" not in editor_row(scr, 0), editor_row(scr, 0))

# --- 2: attempt to yank onto the pitched track - must refuse ---
scr.send(CTRL_LEFT); scr.pump(0.3)
scr.send(vk.ctrl('y')); scr.pump(0.5)
check("yanking a percussion cell onto a pitched track is refused",
      status(scr) == "Cannot paste: incompatible tuning", status(scr))
check("the pitched cell stays empty after the refused yank", "BD" not in editor_row(scr, 0), editor_row(scr, 0))

# --- 3: yank onto a different row of the percussion track - same tuning,
# must succeed ---
scr.send(CTRL_RIGHT); scr.pump(0.3)
scr.send(DOWN); scr.pump(0.3)  # row 1
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
