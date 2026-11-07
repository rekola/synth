#!/usr/bin/python3.12
"""A minimal VTE terminal (the widget GNOME Terminal is made of) that runs a program and types keys at it on a
schedule, for real_terminals.py: needs python3-gi and gir1.2-vte-2.91, and a display (xvfb-run).

    vte_host.py SECONDS KEYS_AT:TEXT ... -- PROGRAM ARGS...

TEXT is sent as typed ('\\x20' for a space); the window closes after SECONDS.
"""
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Vte", "2.91")
from gi.repository import GLib, Gtk, Vte  # noqa: E402


def main():
    argv = sys.argv[1:]
    split = argv.index("--")
    seconds = float(argv[0])
    keys = []
    for spec in argv[1:split]:
        at, text = spec.split(":", 1)
        keys.append((float(at), text.encode().decode("unicode_escape").encode()))
    program = argv[split + 1:]

    win = Gtk.Window()
    term = Vte.Terminal()
    term.set_size(120, 40)
    win.add(term)
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    term.spawn_async(Vte.PtyFlags.DEFAULT, None, program, None, GLib.SpawnFlags.DEFAULT, None, None, -1, None, None)
    for at, data in keys:
        GLib.timeout_add(int(at * 1000), lambda d=data: (term.feed_child(d), False)[1])
    GLib.timeout_add(int(seconds * 1000), lambda: (Gtk.main_quit(), False)[1])
    Gtk.main()


if __name__ == "__main__":
    main()
