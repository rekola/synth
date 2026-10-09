# Retroactive MIDI recording (Capture MIDI)

Notes played while nothing is recording are kept in a short per-track log.
One gesture turns the most recent phrase into a new clip, as if recording had
been running all along (Ableton Live's "Capture MIDI"). It works with the
transport stopped or running, and does not need the track armed first.

## What exists

- Live performance input has two entry points, neither of which records unless a
  take is on: `MidiNoteInput::handle()` (MIDI, terminal and headless) and
  `LaunchpadManager`'s note-pad handler. Each already sends
  `PLAY_NOTE`/`STOP_NOTE`/`NOTE_PRESSURE` and decides itself whether to write.
  The computer keyboard is for editing existing recordings, not performing,
  so it is not captured.
- Aftertouch is stored as a per-row cell in the note's column
  (`Note::isAftertouch()`: value -1, velocity > 0). `Controller::notePressure()`
  turns raw readings into those cells: each row gets the time-weighted average of
  the readings in it (a reading holds until the next), rows with no reading
  get the previous value held through them, recorded rows have delay 0, and
  `applyNotePressure()` never overwrites a real note.
- Take timing already has a model: `ClipPlayer::rawStep()` (live-clock row plus
  the sub-row `delay`, 0-255) and `quantizedStep()`, chosen by
  `Song::getRecordQuantize()`. `Controller::ensureClipRecordingClip()` turns an
  absolute step into a row relative to the take's bar-aligned origin.
- The Launchpad CC98 long hold (`kMixerHoldPreviewThreshold`-style hold,
  `LaunchpadManager::handleRecordButton()`, `LaunchpadManager.cpp:1073`) is
  already routed to a stub: "Capture MIDI: not implemented yet". Docs
  (`docs/launchpad.md`, CLAUDE.md) already name the gesture.
- Clips are created through `Song::addClip()` / `ensureClipAt()` inside a
  `Song::Edit`, which also gives undo as one step.

## Design

### 1. `LiveNoteLog` (new, `src/playback/LiveNoteLog.{h,cpp}`)

UI-thread only, so no locking. A bounded ring per track id of
`{kind on/off/pressure, MIDI note, note value, velocity or pressure, position}`.

- Note value is stored after tuning mapping (`nearestNoteValue()` result), so
  the log is cleared when the song tuning changes, a song closes, or the track
  is removed.
- `position` is a double in rows plus a mode flag:
  - transport playing: live-clock step + `getCurrentDelay()/255` (the same
    source `rawStep()` uses);
  - stopped: wall-clock seconds from `steady_clock` x `bpm * 4 / 60` rows/s,
    using the song tempo at log time.
- Bounded by count (2048 events per track) and age (10 minutes). Oldest drop first.
- Notes held across the capture moment are closed at the capture position.

Fed from one new call, `Controller::logLiveNote(track_id, kind, value,
velocity)`, placed next to every live `PLAY_NOTE`/`STOP_NOTE`/`NOTE_PRESSURE`
push: MIDI (`MidiNoteInput`) and Launchpad note pads (including their
aftertouch). Keyboard note entry, step-grid auditions and clip playback are not
live performance and are not logged. A pressure reading is logged only for a
note that is held; channel-wide pressure has no note column and, as in live
takes today, is not recorded. It logs regardless of
whether the note was also written by an active take; capture is cheap and
independent of arming.

### 2. `captureToPattern()` (new, pure, `src/playback/MidiCapture.{h,cpp}`)

No `Controller` dependency, so it is unit-testable. Input: the track's events,
`BarGrid`/rows-per-bar, `quantize`, current mode. Output: a `Pattern` plus clip
length in rows.

1. **Phrase selection.** Take events since the track's last capture. Walk back
   from the newest; a gap with nothing held and no event for longer than
   `max(2 bars, 4 s)` ends the phrase. Events of a different time mode (a
   transport start/stop in the middle) end it as well, so a phrase never mixes
   live-clock and wall-clock positions.
2. **Alignment.**
   - Playing: positions are already on the song grid. Clip row 0 is the start of
     the bar holding the first note-on; length is the whole bars covering the
     last event, minimum one bar. This keeps the clip in phase with the running
     transport when it launches.
   - Stopped: no grid to be in phase with, so the first note-on becomes row 0
     (its sub-row offset kept as `delay` unless quantising). Length rounds up to
     whole bars of the song signature.
3. **Quantise.** `Song::getRecordQuantize()` on: nearest row, delay 0; off: the
   row it falls in plus delay. The same rule as a Launchpad take.
4. **Columns.** Lowest column free at each note-on given overlapping held notes,
   like `MidiNoteInput::freeColumn()`, computed at conversion time.
5. **Note-offs.** A cell cannot hold a note and its own off, so an off that lands
   on its note's row moves to the next row (min one row). An off past the clip end
   clamps to the last row. Offs never overwrite a note-on in that column.
6. **Aftertouch.** For each held note, its pressure readings become aftertouch
   cells in that note's column, with `notePressure()`'s rules: the row's value is
   the time-weighted average over the row (a reading holds to the next, or to the
   note's release), rows between readings hold the previous value, delay 0, value
   clamped to 1-127. Cells run from the row after the note-on to the row before
   its note-off, never onto the note-on or note-off cell. The averaging is
   extracted from `Controller::notePressure()` into a small shared helper
   (state in, row values out) so live recording and capture cannot disagree.
   Quantise does not move readings: they are averaged per row either way.
   Not covered: effect commands.

### 3. `Controller::captureMidi(track_id)` (glue)

- Builds the pattern, then inside one `Song::Edit("capture midi", CONTENT)`
  creates the clip in the first empty slot (`!clip.isEmpty()` scan, filler
  slots reused) or appends one; loops it; names it "Capture". One undo step.
- Marks the log consumed up to the capture point (a second press with nothing
  new says "Nothing to capture").
- Transport running: queue the clip to launch at the next bar via
  `ClipPlayer::triggerClip()` (the Ableton behaviour; the clip was bar-aligned,
  so it comes in phase). Stopped: the clip is only created.
- Which tracks: every armed track with something logged, else the cursor track.
  Same fallback as `ClipPlayer::toggleOverdub()`. Sample tracks and tracks with
  nothing logged are skipped with a status message.

### 4. Triggers

- Launchpad: CC98 long hold calls `captureMidi` (replacing the stub). It works from
  any `GridMode`, as CC98 does.
- Command `capture-midi` in `UI::initializeCommands()` (shared, so M-x and a
  future GUI get it); a terminal keybinding in `TerminalUI` and an entry in the
  Record menu.
- Headless gets it through the Launchpad only (it has no command prompt).

## Testing

- `tests/MidiCaptureTests.cpp`: aftertouch (row average, held rows, delay 0,
  never onto a note cell, release ends it, a helper test that live
  `notePressure()` and capture give identical rows for the same readings);
  playing-mode bar alignment and length; chord
  columns; short note off pushed to next row; off past the end; quantise on/off;
  stopped mode; phrase gap split; mode switch split; ring overflow and age
  expiry; clearing on tuning change.
- Controller-level test: captured clip lands in the first empty slot, is one
  undo step, and a second capture without new input does nothing.
- `tools/e2e/verify_launchpad_capture_midi.py`: play pads (with pad pressure)
  without arming (stopped, then running), hold CC98 past the threshold, assert a new clip shows in the
  `ClipGrid` text and, while running, queued to launch. Uses `Screen.wait()`.
- Full `ctest` plus a manual listen with a real MIDI keyboard.

## Docs

Update CLAUDE.md's Session Record bullet (the stub is gone), `docs/launchpad.md`,
`docs/terminal.md`, `docs/glossary.md` (capture, phrase), and add a short
`docs/capture-midi.md` with the phrase and alignment rules.

## Known limits

- MIDI is read on the UI thread, so event times carry a frame of jitter (like
  existing takes). Better: stamp events with the ALSA sequencer time in
  `AlsaAudio::recordMIDI()`. Follow-up, not v1.
- A tempo change inside a stopped-mode phrase uses the tempo at each event's
  log time, so a very long phrase across a tempo edit may drift.
- `getLiveClock()` across stop/rewind must be checked while building (plan assumes
  it is monotonic while playing; if not, a transport state change ends the phrase,
  which step 1 already allows for).

## Open questions (defaults chosen, change if wrong)

1. Log pads as well as MIDI, or MIDI only like Ableton? Default: both, since
   pads are equally live input here. The computer keyboard is excluded.
2. Launch the captured clip when the transport runs? Default: yes.
3. Phrase gap `max(2 bars, 4 s)` and 10 minute retention. Defaults, easy to tune.

## Order of work

1. `LiveNoteLog` + `captureToPattern()` + unit tests.
2. `Controller::logLiveNote()` wiring at the three input sites.
3. `Controller::captureMidi()` + command + Launchpad hook + test.
4. e2e script, docs, CLAUDE.md.
