#!/usr/bin/env python3
"""Drive the compiled synth binary through a pty and verify the
centralized keybinding dispatch: Ctrl-B/Ctrl-W/Ctrl-Y/Ctrl-G in
PatternEditor and C-x C-c/Ctrl-N/Space in UI. General Emacs-keybinding
smoke test, independent of the Launchpad-specific scripts in this
directory (which all import harness.py directly instead).

Two environmental quirks are worked around rather than tested here (both
reproduce on any commit, not caused by any particular change):
  - ESC is not reliably delivered as a standalone NCKEY_ESC in this scripted
    pty (traced to TerminalMenu::offerInput delegating to notcurses's own
    ncmenu_offer_input(), which appears to intercept/consume Escape itself
    before it reaches StatusLine's M-x meta_pressed logic or PatternEditor's
    Alt-modified chords) - so Alt-W and the M-x path aren't exercised here.
  - Ctrl-N (new-song) leaves the freshly created song in a state where a
    following Space no longer toggles playback - so Space is tested before
    Ctrl-N below.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk


def main():
    pid, fd = vk.spawn()
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        print(scr.dump())
        os.kill(pid, 9)
        sys.exit(2)

    results = []

    def check(name, ok, screen_dump):
        results.append((name, ok))
        print(f"[{'PASS' if ok else 'FAIL'}] {name}")
        if not ok:
            print("----- screen dump (last 5 rows) -----")
            for l in screen_dump.splitlines()[-5:]:
                print(l)
            print("--------------------------------------")

    # --- set-mark via Ctrl-B ---
    scr.send(vk.ctrl('b'))
    scr.pump()
    d = scr.dump()
    check("Ctrl-B (set-mark) shows 'Mark set'", "Mark set" in d, d)

    scr.send(b"\x1b[B")
    scr.pump(0.2)
    scr.send(b"\x1b[B")
    scr.pump(0.2)

    # --- kill-region via Ctrl-W ---
    scr.send(vk.ctrl('w'))
    scr.pump()
    d = scr.dump()
    check("Ctrl-W (kill-region) shows 'Region killed'", "Region killed" in d, d)

    # --- yank via Ctrl-Y ---
    scr.send(vk.ctrl('y'))
    scr.pump()
    d = scr.dump()
    check("Ctrl-Y (yank) shows 'Yanked'", "Yanked" in d, d)

    # --- set-mark, then keyboard-quit via Ctrl-G ---
    scr.send(vk.ctrl('b'))
    scr.pump(0.2)
    scr.send(vk.ctrl('g'))
    scr.pump()
    d = scr.dump()
    check("Ctrl-G (keyboard-quit) shows 'Mark deactivated'", "Mark deactivated" in d, d)

    # --- Space (toggle-playing), before Ctrl-N (see module docstring) ---
    scr.send(b" ")
    scr.pump()
    d1 = scr.dump()
    check("Space (toggle-playing) shows 'Playing' or 'Stopped'",
          "Playing" in d1 or "Stopped" in d1, d1)

    # --- Ctrl-N (new-song) ---
    scr.send(vk.ctrl('n'))
    scr.pump()
    d = scr.dump()
    check("Ctrl-N (new-song) shows 'New song'", "New song" in d, d)

    # --- C-x C-c quits (Emacs's own save-buffers-kill-terminal binding -
    # there is no separate Ctrl-Q quit shortcut; graceful shutdown joins
    # the audio thread, so allow several seconds rather than expecting a
    # near-instant exit) ---
    scr.send(vk.ctrl('x'))
    scr.pump(0.3)
    scr.send(vk.ctrl('c'))
    wpid = 0
    end = time.time() + 10.0
    while time.time() < end:
        try:
            wpid, status = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            wpid = pid
        if wpid == pid:
            break
        time.sleep(0.2)
    check("C-x C-c (quit) terminates the process", wpid == pid, scr.dump())

    try:
        os.kill(pid, 9)
    except ProcessLookupError:
        pass

    n_fail = sum(1 for _, ok in results if not ok)
    print(f"\n{len(results) - n_fail}/{len(results)} checks passed")
    sys.exit(1 if n_fail else 0)


if __name__ == "__main__":
    main()
