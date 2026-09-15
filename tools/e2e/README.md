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
`harness.py`'s own `spawn()` sets `SYNTH_LAUNCHPAD_NO_HARDWARE=1` on every
`synth` it spawns, so it only ever connects to the fake simulator a
script is actually driving, never any real Launchpad hardware also
plugged into the same machine (`LaunchpadIO.h`'s own `ignore_hardware_`
comment) - without this, a machine with a real Launchpad attached would
have every e2e-spawned `synth` auto-connect to both at once.

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
gcc -o fake_launchpad_mute_picker fake_launchpad_mute_picker.c -lasound
gcc -o fake_launchpad_record_arm_picker fake_launchpad_record_arm_picker.c -lasound
gcc -o fake_launchpad_record_arm_holes fake_launchpad_record_arm_holes.c -lasound
gcc -o fake_launchpad_record_arm_wrong_track fake_launchpad_record_arm_wrong_track.c -lasound
gcc -o fake_launchpad_record_arm_percussion fake_launchpad_record_arm_percussion.c -lasound
gcc -o fake_launchpad_aftertouch_clip fake_launchpad_aftertouch_clip.c -lasound
gcc -o fake_launchpad_mixer_hold fake_launchpad_mixer_hold.c -lasound
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
  (Ctrl-B/W/Y/G/Space/C-x o/C-x b/C-x C-c), independent of Launchpad.
- **`fake_launchpad.c` / `verify_launchpad_e2e.py`** - baseline single
  pad press/aftertouch/release, switching into NOTES mode and arming
  Record Arm first (a plain press only ever auditions - it never writes
  into the pattern without Record Arm on, and arming it always starts
  playback, so there is no "step entry while stopped" state to test any
  more): the press enters a note, aftertouch modulates its velocity on a
  later row as the transport advances, and release writes a real OFF.
- **`fake_launchpad_chord.c` / `verify_launchpad_chord.py`** - 3
  near-simultaneous presses (after switching into NOTES mode and arming
  Record Arm) released in non-LIFO order; catches note columns colliding
  into one, or a release scrambling which column gets which OFF.
- **`fake_launchpad_perc.c` / `verify_percussion_layout.py`** - presses
  a pad after navigating onto a percussion track; confirms the layout
  actually switches (GM percussion mapping + LED coloring) instead of
  silently dropping input.
- **`fake_launchpad_button.c` / `verify_launchpad_buttons.py`** - sends
  an extra-button CC press/release; confirms decoding, command dispatch
  (cursor actually moves), and button LED feedback.
- **`fake_launchpad_hotplug.c` / `verify_launchpad_hotplug.py`** -
  connects *after* `synth` has already started, exercising the
  ALSA announce-port hotplug path instead of the startup-time scan; also
  switches into NOTES mode and arms Record Arm before pressing, same
  reasoning as `fake_launchpad.c` above.
- **`fake_launchpad_device.c`** - the general-purpose simulator: argv is
  `<client-name-suffix> <arm|plain> [note]`. `arm` switches into this
  instance's own NOTES mode and arms Record Arm (a single song-wide flag,
  not per-device - only one connected instance should ever press it)
  before pressing `note` (default 11, pad (0,0)); `plain` only switches
  into its own NOTES mode and presses, relying on whichever `arm`
  instance already armed Record Arm. Octave shifting has no button of its
  own any more (`LaunchpadProtocol::commandForButton()`'s own comment -
  deliberately deferred, not removed), so this no longer simulates it.
  Two instances can run concurrently and be told apart. Used by:
  - **`verify_launchpad_multidevice.py`** - two devices connect at once
    and each independently switches into its own NOTES mode (`GridMode`
    is per-device); device A arms Record Arm and presses one note, device
    B presses a different one relying on A's already-armed state. Both
    distinct notes must land correctly, uncorrupted - proving
    `LaunchpadManager`'s per-device state (`active_notes`, keyed by
    device id) is genuinely independent (and not just that it compiles).
  - **`verify_launchpad_disconnect_prune.py`** - one device arms Record
    Arm, presses, and fully disconnects without ever disarming (Record
    Arm is Song state, not per-device connection state, so it stays
    armed); a second device connects afterward, relies on that still-
    armed state, and must be able to press and write cleanly with no
    crash - covers `LaunchpadManager::refresh`'s erase-while-iterating
    device-pruning loop.
- **`fake_launchpad_sendmode.c` / `verify_launchpad_sendmode.py`** - presses
  CC95 a second time to enter Session's own mixer submode (required before
  CC69 means anything at all - see `CLAUDE.md`'s own `GridMode` bullet),
  toggles into Send A grid mode (CC69), and presses a grid pad; confirms
  the LED bargraph both starts at the track's existing Send A level and
  reflects the new one after the press - the non-NOTES branch of
  `PatternEditor::handleLaunchpadPadEvent` (Send A/B/Main/Pan) had no
  coverage before this script.
- **`fake_launchpad_sendmode_autocreate.c` / `verify_launchpad_sendmode_autocreate.py`** -
  loads `songs/songtest1.xml` (2 tracks), enters mixer submode, toggles
  Send A mode, and presses column 5 (no track there yet); confirms
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
  at all. Arms Record Arm via a quick CC98 tap (the legacy global
  "toggle-record-arm" command - CC19 itself no longer reaches it while
  looking at Session view, see `verify_launchpad_record_arm_picker.py`
  below for that gesture instead), then presses pad (0,0) (x=0 the fixture's
  only track, y=0 -> pool index 7 - see the fixture's own comment) and
  confirms `LaunchpadManager::handleSessionPadEvent`'s assign branch
  actually copied that pool entry's own pattern (E-4) into the current
  section, replacing its placeholder note (C-4) - rather than silently
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
- **`fake_launchpad_draw_clear.c` / `verify_launchpad_draw_clear.py`** -
  DRAW mode's own "hue decided on release" pad-coloring design (a press
  never changes a pad's hue immediately, only its live brightness; a
  short release cycles to the next hue, a long hold leaves the hue alone
  and adjusts brightness only), plus CC98's own tap-vs-hold split as a
  whole: a long hold is what reaches DRAW at all now (entering it, or
  blanking the canvas if already there), while a quick tap instead fires
  "toggle-record-arm" (`LaunchpadManager::handleDrawToggleButton()`) -
  this script only exercises the long-hold half, since the tap half is
  the exact gesture `verify_launchpad_session.py` already drives.
- **`launchpad_session_test.xml` (pool index 7's own `length="8"`) /
  `fake_launchpad_stopclip.c` / `verify_launchpad_stopclip.py`** - Stop
  Clip (CC49) opening the track-picker overlay: a plain press-only toggle,
  Session-view-only (a no-op from any other `GridMode`), that lights the
  grid's bottom row with one pad per selectable track on top of Session
  view's own rendering - left completely untouched otherwise, no dimming,
  every other row still reaching Session view's own pad handling normally
  (Session view's own column-per-track layout has no visible "current"
  track for a plain press to target, so picking a column in the picker
  row is what actually stops that track - `LaunchpadManager::
  handleRawButton()`/`handleTrackPickerPadEvent()`). Triggers pool index 7
  via pad (0,0), confirms its own LED brightens, then presses CC49 and
  picks that same track's column in the picker row - pad (0,0) again, now
  read as "column 0" rather than "clip index 7" - to queue a stop,
  confirming the pad's LED dims red once it takes effect while CC49's own
  indicator stays lit (the overlay no longer auto-closes on a pick), then
  a second CC49 press closes it, reverting both. **Currently fails most
  checks in this sandboxed environment** for a documented, pre-existing,
  unrelated reason (not a real regression - reproduces with plain pad
  presses alone, no CC49 involved) - see `docs/known_bugs.md`.
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
- **`fake_launchpad_mute_picker.c` / `verify_launchpad_mute_picker.py`** -
  the track-picker overlay's Mute purpose (CC39): presses CC95 a second
  time to enter Session's own mixer submode first (required before CC39
  means anything at all rather than launching a scene - see `CLAUDE.md`'s
  own `GridMode` bullet; also confirms Session's own LED turns orange),
  opens it, confirms both CC39's own LED and the picker row's pad (0,0)
  (the fixture's only track, unmuted by default) show bright yellow,
  confirms a *different* row (pad
  (0,7)) is untouched - not dimmed - proving the overlay leaves Session
  view's own rendering alone outside the picker row, picks column 0 to
  mute it, confirms the picker row dims to dark yellow (bright/dim is
  polarity-inverted from Stop Clip/Solo - see `CLAUDE.md`'s own
  Track-picker overlay bullet) while CC39's own LED stays lit (no
  auto-close on a pick), then a second CC39 press closes it and both
  revert. Deliberately never triggers playback, unlike
  `verify_launchpad_stopclip.py` above - Mute needs no clip playing at
  all, which sidesteps the same sandboxed-environment flakiness
  documented for that script (`docs/known_bugs.md`), giving a much more
  reliable signal for the overlay's general open/pick/retarget/close
  mechanics.
- **`fake_launchpad_record_arm_picker.c` / `verify_launchpad_record_arm_picker.py`** -
  the track-picker overlay's RECORD_ARM purpose (CC19): one of the eight
  members of the real Launchpad X's own right-column "Track control" group
  (`CLAUDE.md`'s own `GridMode`/Extra-button-layout bullets), so CC95 a
  second time is needed first, same as Mute/Solo/Stop Clip, to enter
  Session's own mixer submode before CC19 opens the overlay rather than
  launching scene row 0. Confirms both CC19's own LED and the picker row's
  pad (0,0) (the fixture's only track, unarmed by default) show dim red
  before opening and once opened (an idle/just-opened track reads the same,
  unlike Mute's inverted-polarity bright-when-off), confirms pad (0,7) is
  untouched, picks column 0 to arm it, confirms the picker row lights
  bright red (reusing Stop Clip's own hue - the two purposes never show at
  once) while CC19's own LED stays lit (no auto-close on a pick), then a
  second CC19 press closes it and both revert. Deliberately never triggers
  playback - arming is pure bookkeeping with nothing to hear. Only the
  arm/disarm picker mechanism itself, not the actual recording gesture -
  see `fake_launchpad_record_arm_holes.c`/`verify_launchpad_record_arm_
  holes.py` below for a real NOTE-mode note landing in the armed take.
- **`launchpad_record_arm_holes_test.xml` / `fake_launchpad_record_arm_
  holes.c` / `verify_launchpad_record_arm_holes.py`** - the per-track
  Record Arm mechanism's actual *recording* gesture, not just the
  arm/disarm picker above: arms the fixture's only track (which starts
  with no clips at all), presses Session-view clip index 2 (not 0),
  switches to NOTE mode, plays one note, then disarms - exercising "holes
  are allowed" directly (`Song::ensureClipAt()`), since a correct
  implementation has to backfill indices 0/1 with empty fillers rather
  than collapsing the take to whichever slot happens to be first unused.
  Verifies the result through the terminal `SessionView` widget itself
  (M-x `session-view`, driven the same way a real Alt-x would arrive -
  `EscapeSequenceCoalescer` folds a bare ESC followed later by 'x' into
  one event): confirms 7 of the 8 displayed rows still show the plain
  empty-slot icon, exactly one shows a real, named clip (not "(unnamed)"
  - `Song::ensureClipAt()`'s filler got a real id/name once it actually
  received content), and no row still shows the "●" record indicator once
  the take is disarmed and finalized.
- **`launchpad_record_arm_wrong_track_test.xml` / `fake_launchpad_record_
  arm_wrong_track.c` / `verify_launchpad_record_arm_wrong_track.py`** -
  regression test for a real bug: `LaunchpadManager::handlePadEvent()`'s
  own "recording supersedes the assigned track" override only fired when
  the recording track's own internal id sorted numerically *lower* than
  the already-assigned/cursor track's, instead of unconditionally (the
  fixture's own track "1", armed/targeted for recording, is created
  after - and so has a higher internal id than - track "0", left as the
  assigned/cursor track the whole script). Arms and targets track 1's
  clip index 0 via the picker, then plays a NOTE-mode note while track 0
  is still assigned - confirms track 1's own SessionView column shows a
  real, populated clip and track 0's shows none. The fixture deliberately
  makes track 0 a lane-less `PercussionTrack` and track 1 a plain pitched
  track (different percussion-ness) - an earlier draft used two plain
  pitched tracks and passed even with the bug still present, since the
  multi-track record fan-out mechanism ended up writing the note into the
  recording track anyway as a side effect, masking the actual bug
  entirely; only a percussion-vs-pitched mismatch (fan-out's own
  compatibility filter) makes the fixture actually prove the fix matters.
- **`launchpad_record_arm_percussion_test.xml` / `fake_launchpad_record_
  arm_percussion.c` / `verify_launchpad_record_arm_percussion.py`** -
  regression test for a real bug: recording a Session View take into a
  step-sequenced `PercussionTrack` showed the step-grid editor in NOTES
  mode instead of letting the performer actually play it live -
  `handlePadEvent()`'s own step-grid short-circuit ran before (and so was
  never superseded by) the recording-supersedes-assigned-track override,
  so a pad press toggled a step in the *background* pattern instead of
  ever reaching the armed take. Arms and targets the fixture's only track
  (a two-lane step-sequenced `PercussionTrack`), plays a NOTES-mode pad
  press, then disarms - verifies the actual functional outcome through
  the terminal `SessionView` widget (did the note land in the armed clip
  at all), deliberately not exact LED byte sequences for the step grid vs.
  free-drumming layout: this sandboxed environment's own pad-press-to-LED
  round trip for the step grid specifically is unreliable even on an
  unmodified checkout (confirmed via `git stash` A/B - see
  `verify_launchpad_stepseq.py`'s own docstring and `docs/known_bugs.md`).
- **`fake_launchpad_aftertouch_clip.c` / `verify_launchpad_aftertouch_clip.py`** -
  the "Clip-based note recording" path (`Controller::
  ensureNoteRecordingClip()`), not step entry: switches into NOTES grid
  mode (CC96 - `GridMode` defaults to SESSION, where a plain note-on would
  launch a Session View clip slot instead), arms Record Arm (CC19), holds
  a note across several rows of real playback, and sends two aftertouch
  messages partway through the hold - confirms both become visible as a
  real (non-`--`) value in the pattern editor's own velocity column on the
  row the transport had reached when each one arrived, not just on the
  note-on's own row. Polls every currently-visible row rather than
  assuming a fixed one, since the exact row a message lands on depends on
  real wall-clock/audio-thread timing.
- **`fake_launchpad_mixer_hold.c` / `verify_launchpad_mixer_hold.py`** -
  the mixer radio group's own momentary hold-to-preview gesture
  (`LaunchpadManager::armMixerHoldPreview()`/`handleMixerFunctionRelease()`):
  enters Session's own mixer submode, quick-taps Send A (CC69) so it
  becomes the sticky selection, long-holds Mute (CC39, past the 600ms
  threshold) and releases - confirms Send A's own LED is bright again
  afterward (reverted, not left on Mute), then quick-taps Mute again as a
  control - confirms it stays bright this time (sticky, no hold
  involved). Same known, pre-existing environment limitation as
  `verify_launchpad_stopclip.py` above (it also presses CC95 a second
  time to enter mixer submode) - see `docs/known_bugs.md`.
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
  a song whose only track is a step-sequenced `PercussionTrack`, confirms
  the Launchpad grid switches to the step-grid surface automatically (no
  mode toggle needed - the step-lit/unlit colors, not the ordinary
  note-grid ones) purely from track assignment, then presses pad (0,0) and checks
  for the lane/step's color changing to lit. That second check currently
  fails in at least one sandboxed environment for reasons unrelated to
  this feature - see docs/known_bugs.md's entry on
  `verify_launchpad_e2e.py`, which fails the identical class of
  press-changes-something check even on an unmodified checkout.

## Known environmental quirks (not bugs in the app)

See `../../docs/known_bugs.md` for the couple of pty/terminal quirks
(`Esc` and `Ctrl-P` not reliably arriving as events in a scripted pty)
these scripts already work around.
