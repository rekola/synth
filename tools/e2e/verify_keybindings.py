#!/usr/bin/env python3
"""Drive the compiled synth binary through a pty and verify the
centralized keybinding dispatch: Ctrl-B/Ctrl-W/Ctrl-Y/Ctrl-G in
PatternEditor, C-x o/C-x b in UI, and C-x C-c/Space in UI. General
Emacs-keybinding smoke test, independent of the Launchpad-specific scripts
in this directory (which all import harness.py directly instead).
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

    # A fresh session defaults to overview/ArrangementGrid focus, not the
    # pattern editor (see CLAUDE.md's own note on this) - every
    # PatternEditor-specific binding below needs C-x o (other-window)
    # first, or none of them would ever reach it.
    vk.other_window(scr)

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

    # --- Space (toggle-playing) ---
    scr.send(b" ")
    scr.pump()
    d1 = scr.dump()
    check("Space (toggle-playing) shows 'Playing' or 'Stopped'",
          "Playing" in d1 or "Stopped" in d1, d1)
    if "Playing" in d1:
        scr.send(b" ")  # stop before switching buffers below
        scr.pump()

    # --- C-x b (select-named-buffer, Emacs's own switch-to-buffer) ---
    # There's no standalone new-song/Ctrl-N command any more - typing a
    # name that isn't already an open buffer creates a fresh blank one,
    # same as Emacs's own switch-to-buffer.
    before = scr.dump()
    vk.new_buffer(scr, "keybindings_test")
    d = scr.dump()
    check("C-x b (select-named-buffer) switched to the new buffer",
          "keybindings_test" in d and d != before, d)

    # --- C-x C-c quits (Emacs's own save-buffers-kill-terminal binding -
    # there is no separate Ctrl-Q quit shortcut; graceful shutdown joins
    # the audio thread, so allow several seconds rather than expecting a
    # near-instant exit) ---
    #
    # Ctrl-W above actually killed content, so this buffer (demo3.xml, the
    # one Ctrl-B/W/Y/G ran against - not the fresh one from C-x b) is
    # dirty: save-buffers-kill-terminal prompts for confirmation rather
    # than quitting outright, same as Emacs's own version of this
    # binding - answer it before waiting for the process to actually exit.
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
