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
gcc -o fake_launchpad_draw_clear fake_launchpad_draw_clear.c -lasound
gcc -o fake_launchpad_stepseq fake_launchpad_stepseq.c -lasound
gcc -o fake_launchpad_session fake_launchpad_session.c -lasound
gcc -o fake_launchpad_notecustom fake_launchpad_notecustom.c -lasound
gcc -o fake_launchpad_stopclip fake_launchpad_stopclip.c -lasound
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
- **`launchpad_session_test.xml` / `fake_launchpad_session.c` /
  `verify_launchpad_session.py`** - `GridMode::SESSION`, the redesigned
  session/launch view (rows are a track's own pooled patterns, columns are
  tracks; CC95/96/97 are the only way in/out, fully decoupled from
  terminal UI focus). `DeviceState::grid_mode` now defaults to SESSION, so
  nothing needs pressing to *enter* it - confirmed from the CC95/CC97 LED
  colors already present in the very first LED refresh, before any input
  at all. Arms Record Arm (CC19), then presses pad (0,0) (x=0 the fixture's
  only track, y=0 -> pool index 7 - see the fixture's own comment) and
  confirms `LaunchpadManager::handleSessionPadEvent`'s assign branch
  actually copied that pool entry's own pattern (E-4) into the current
  scene, replacing its placeholder note (C-4) - rather than silently
  falling through to ordinary NOTES-mode note entry.
- **`fake_launchpad_notecustom.c` / `verify_launchpad_notecustom.py`** -
  two small LED/mode-switch regressions: Note (CC96) previously had no
  active-state LED at all (fixed, now lights up the same way Session/
  Custom do once NOTES is actually selected), and Custom/DRAW (CC97)
  previously only switched `GridMode` on release rather than on press
  (fixed - `LaunchpadManager::handleDrawToggleButton()`). The script
  presses CC96 and confirms its LED lights, then presses CC97 and
  actively drains SysEx *before* ever sending its release, confirming
  DRAW mode's own LED already lit up while the button was still held
  down.
- **`launchpad_session_test.xml` (pool index 7's own `length="8"`) /
  `fake_launchpad_stopclip.c` / `verify_launchpad_stopclip.py`** - Stop
  Clip (CC49)'s redesign into a held modifier (Session view shows several
  tracks at once as columns with no visible "current" one for a plain
  press to target, so holding CC49 and pressing any pad in a column stops
  that column's own track instead - `LaunchpadManager::
  handleStopClipButton()`/`handleSessionPadEvent()`). Triggers pool index
  7 via pad (0,0), confirms its own LED brightens, then holds CC49 and
  presses that pad again to queue a stop, confirming the LED reverts once
  it takes effect. **Currently fails 2 of 4 checks in this sandboxed
  environment** for a documented, pre-existing, unrelated reason (not a
  real regression - reproduces with plain pad presses alone, no CC49
  involved) - see `docs/known_bugs.md`.
- **`launchpad_sampletrack_session_test.xml` (+ sidecar `.wav`) /
  `verify_launchpad_sampletrack_stopclip.py`** - the SampleTrack twin of
  the script above: structurally the same fixture (one track, pool index
  7 populated - a real sample clip this time, `length="8"` for the same
  reason), reusing `fake_launchpad_stopclip.c` unchanged, to prove
  `LaunchpadManager::fireOrTriggerClipStep()`'s SAMPLE branch
  (`PlaybackControlEvent::PLAY_SAMPLE_CLIP`) is wired all the way through
  the real ALSA + audio-thread path, not just reachable in-process the way
  `SampleTrackTests.cpp`'s own `triggerClip()` calls are. Same known,
  pre-existing environment limitation as its sibling above - see
  `docs/known_bugs.md`.
- **`cross_tuning_paste_test.xml` (+ companion `..._song_b.xml`) /
  `verify_patterneditor_cross_tuning_paste.py`** - a `Note::getValue()`
  means a different kind of value under a different tuning (GM percussion
  key vs. a pitched scale degree), so `PatternEditor`'s own clipboard
  refuses a paste across that boundary rather than silently reinterpreting
  it. `PatternEditor`'s clipboard always targets the cursor's current
  column, so a same-song cross-tuning paste is the real, everyday risk
  there (using `ArrangementGrid`'s Enter-commit purely as a reliable
  teleport to an exact track/row, not to exercise any clipboard of its
  own - it has none).
- **`drum_machine_stepgrid_test.xml` / `fake_launchpad_stepseq.c` /
  `verify_launchpad_stepseq.py`** - loads
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
