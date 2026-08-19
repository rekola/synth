# Launchpad end-to-end test harness

`ctest` only covers the pure-function layers (`LaunchpadLayout`,
`LaunchpadProtocol`) - anything that touches real ALSA I/O (device
detection, SysEx handshakes, pad/button decoding, hotplug, multi-device
state) is deliberately left out of the CMake build, the same way
`LaunchpadIO` itself has no unit tests. This directory is the harness
used to verify that layer by hand: a small C program opens an ALSA
sequencer client named to look like a real Launchpad and scripts a
sequence of press/release/CC events, while a Python script spawns
`synth` in a pty (via `pyte`) and screen-scrapes the result.

## Setup

```sh
gcc -o fake_launchpad fake_launchpad.c -lasound
gcc -o fake_launchpad_chord fake_launchpad_chord.c -lasound
gcc -o fake_launchpad_perc fake_launchpad_perc.c -lasound
gcc -o fake_launchpad_button fake_launchpad_button.c -lasound
gcc -o fake_launchpad_hotplug fake_launchpad_hotplug.c -lasound
gcc -o fake_launchpad_device fake_launchpad_device.c -lasound
gcc -o fake_launchpad_sendmode fake_launchpad_sendmode.c -lasound
gcc -o fake_launchpad_sendmode_autocreate fake_launchpad_sendmode_autocreate.c -lasound
```

(the compiled binaries are gitignored - only the `.c` sources are
checked in). Requires a built `../../build/synth` and Python's
`pyte` package (`pip install pyte`).

## Running a script

```sh
python3 verify_launchpad_chord.py
```

Each script prints `[PASS]`/`[FAIL]` per check plus the simulated
device's log, and exits non-zero if anything failed. They're independent
of each other and of ctest - run whichever ones are relevant to what
you're changing.

## What's here

- **`harness.py`** - shared driver every script below imports: forks
  `synth` under a pty with a given song and answers the terminal-
  capability queries notcurses probes for on startup (cursor position,
  pixel geometry, Kitty keyboard protocol, etc.) so it doesn't hang
  waiting for a reply a plain pty never sends. Not Launchpad-specific -
  reusable for testing any keybinding/UI behavior.
- **`verify_keybindings.py`** - general Emacs-keybinding smoke test
  (Ctrl-B/W/Y/G/Space/Ctrl-N/Ctrl-Q), independent of Launchpad.
- **`fake_launchpad.c` / `verify_launchpad_e2e.py`** - baseline single
  pad press/aftertouch/release; the step-entry-vs-playing note-off
  semantics regression test.
- **`fake_launchpad_chord.c` / `verify_launchpad_chord.py`** - 3
  near-simultaneous presses released in non-LIFO order; catches note
  columns colliding into one, or a premature auto-advance mid-chord.
- **`fake_launchpad_perc.c` / `verify_percussion_layout.py`** - presses
  a pad after navigating onto a percussion track; confirms the layout
  actually switches (GM percussion mapping + LED coloring) instead of
  silently dropping input.
- **`fake_launchpad_button.c` / `verify_launchpad_buttons.py`** - sends
  an extra-button CC press/release; confirms decoding, command dispatch
  (cursor actually moves), and button LED feedback.
- **`fake_launchpad_hotplug.c` / `verify_launchpad_hotplug.py`** -
  connects *after* `synth` has already started, exercising the
  ALSA announce-port hotplug path instead of the startup-time scan.
- **`fake_launchpad_device.c`** - the general-purpose simulator:
  argv is `<client-name-suffix> <number-of-octave-up-presses>`, so two
  instances can run concurrently and be told apart. Used by:
  - **`verify_launchpad_multidevice.py`** - two devices connect at once,
    one shifts its own octave first; the resulting notes must be the
    same pitch class exactly one octave apart, proving
    `LaunchpadManager`'s per-device state is genuinely independent (and
    not just that it compiles).
  - **`verify_launchpad_disconnect_prune.py`** - one device builds up
    state and fully disconnects; a second connects afterward and must
    start clean with no crash - covers `LaunchpadManager::refresh`'s
    erase-while-iterating device-pruning loop.
- **`fake_launchpad_sendmode.c` / `verify_launchpad_sendmode.py`** - toggles
  into Send A grid mode (CC69) and presses a grid pad; confirms the LED
  bargraph both starts at the track's existing Send A level and reflects
  the new one after the press - the non-NOTES branch of
  `PatternEditor::handleLaunchpadPadEvent` (Send A/B/Main/Pan) had no
  coverage before this script.
- **`fake_launchpad_sendmode_autocreate.c` / `verify_launchpad_sendmode_autocreate.py`** -
  loads `songs/songtest1.xml` (2 tracks), toggles Send A mode, and presses
  column 5 (no track there yet); confirms
  `PatternEditor::handleLaunchpadPadEvent` auto-creates tracks up to that
  column instead of silently doing nothing - both this script's own
  Send/Pan-mode branch and the symmetric NOTES-mode branch (a device's
  assigned/fallback track index growing the song the same way) were added
  together, though only the Send/Pan-mode one has e2e coverage here (the
  NOTES-mode trigger - a song with zero tracks - hits an unrelated
  pre-existing crash elsewhere in the editor before Launchpad code even
  runs; see `docs/known_bugs.md`).
- **`verify_fokker_colors.py`** - loads `songs/song.xml` (31-EDO) and
  asserts the literal RGB SysEx bytes sent for a handful of hand-verified
  tonic/diatonic/sharp/flat/diesis pads match the `FOKKER_*` color
  constants in `LaunchpadManager.cpp`, scaled down to
  `LAUNCHPAD_IDLE_BRIGHTNESS` since nothing is playing at that snapshot.
- **`launchpad_brightness_test.xml` / `verify_launchpad_note_brightness.py`** -
  a dedicated fixture (one sustained 31-EDO oscillator note, no
  envelope/decay) proves the grid is idle-dimmed with nothing playing and
  brightens once the note actually starts sounding via normal pattern
  playback (not just a live pad press) - covers the active-voice LED
  brightness overlay end to end.
- **`drum_machine_stepgrid_test.xml` / `fake_launchpad_stepseq.c` /
  `verify_launchpad_stepseq.py`** (plans/drum-machine.md, Phase 5) - loads
  a song whose only track is a `DrumMachineTrack`, confirms the Launchpad
  grid switches to the step-grid surface automatically (no mode toggle
  needed - the step-lit/unlit colors, not the ordinary note-grid ones)
  purely from track-type assignment, then presses pad (0,0) and checks
  for the lane/step's color changing to lit. That second check currently
  fails in at least one sandboxed environment for reasons unrelated to
  this feature - see docs/known_bugs.md's entry on
  `verify_launchpad_e2e.py`, which fails the identical class of
  press-changes-something check even on an unmodified checkout.

## Known environmental quirks (not bugs in the app)

See `../../docs/known_bugs.md` for the couple of pty/terminal quirks
(`Esc` and `Ctrl-P` not reliably arriving as events in a scripted pty)
these scripts already work around.
