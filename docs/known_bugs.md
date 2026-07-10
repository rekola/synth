# Known bugs

Found 2026-07-11, not yet fixed.

- **M-x via Esc-then-x or Alt-x doesn't work at all in GNOME Terminal** (VTE,
  the Ubuntu default): pressing Esc does not register as its own keystroke,
  so the following `x` arrives as a plain, unmodified key and gets typed as
  a note instead of opening the M-x prompt. Confirmed via a stderr trace at
  the earliest point notcurses hands input to the app (`TerminalUI::readInput`,
  before any app logic runs) — Esc genuinely never arrives as an event at
  all in this terminal, contradicting notcurses's own documented behavior
  ("any error while handling an escape sequence will see the lex aborted,
  and the sequence thus far played back as independent literal keystrokes" —
  `notcurses_input(3)`, BUGS section). This looks like a real notcurses (or
  VTE/notcurses interaction) bug, not something fixable from application
  code — possibly worth reporting upstream to notcurses. Worked around by
  adding **Ctrl-K** as a third, always-reliable way to open M-x
  (`StatusLine.h`) — use that instead on this terminal.

- **Ctrl-P (raw byte `0x10`) never reaches the application at all**, on any
  terminal tested so far — not even as an unhandled/ignored keystroke.
  Confirmed the same way as above (stderr trace at the earliest possible
  point): no `ncinput` event is ever produced for it. Unexplained; not
  investigated further since Ctrl-K worked fine as an alternative. Avoid
  binding Ctrl-P to anything until this is understood.

- **Ctrl-Q (quit) can take many seconds to actually exit** the process
  after being pressed, rather than exiting promptly. Confirmed pre-existing
  (reproduces on old commits too, via `git stash`), not caused by anything
  recent. Not yet investigated — likely something in the audio thread
  shutdown/join path in `Player.cpp`/`UI::start()`.

- **`SongState::getRelativePosition()` doesn't wrap back to pattern 0** once
  playback advances past the last pattern in the song's pattern list — it
  only handles moving *forward* between multiple existing patterns in
  sequence, so for a short or single-pattern song (e.g. a freshly created
  new-song, 64 rows / 1 pattern), once playback runs past row 64 it starts
  reporting an out-of-range pattern index (`Song::getPattern()` bounds-checks
  and returns a static empty pattern, so this doesn't crash, but the
  displayed pattern/row numbers become nonsensical and playback presumably
  renders silence instead of looping).
