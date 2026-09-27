# Session playback: launched clips inside the transport

Goal: Session view and Arrangement view share one transport, the way the
live-sequencer convention has it. A launched clip takes its track over
from the arrangement instead of running on a separate clock, and the
Session view pattern editor follows each track's own playhead. The
Session view, clip grid and clip editing this builds on are already in
place.

Status: Phases 1-4 done; Phase 5 next.
Every phase lands as its own commit(s), with `ctest` and the e2e scripts
green.

## Open questions

- **Pausing isn't settled for good.** Phase 5 makes the transport toggle
  a plain pause (below) - an interim choice, not the final design. Still
  unresolved: where an arpeggiator resumes after a pause (it keeps its
  own step position on a clock of its own - maybe it should run from
  clips instead, positioned by their rows like everything else), and
  what becomes of sustained voices (a pause leaves an instrument's
  voices as they are - a held note rings on through it, and whatever
  starts on resume layers over it).

## Phase 1: a Controller-owned SessionPlayer

Session playback - which clip is triggered on each track and from which
step, what's queued, the bar-quantized launch/stop rule, the recording
queues and the audition clock - lives in `LaunchpadManager`, so the
terminal's own launches (the clip grid, the master column) go through
Launchpad code, and Phase 2 would move playback out from under it
anyway. Move that state and logic to a Controller-owned `SessionPlayer`
first, with no behaviour change at all:

- **Moves:** `triggered_pattern_by_track_`/`queued_pattern_by_track_`/
  `queued_recording_by_track_`, `session_origin_step_`, the audition
  clock, and the methods around them (`triggerSessionClip()`,
  `launchScene()`, `stopSessionTrack()`, `triggerClipStep()`,
  `sessionPlayheads()`, `restartAuditionClockFromSilence()`).
- **Stays:** everything about a device - grid modes, LED colors, pad
  decoding, the step grid and lane picker with their own preview clock,
  which needs no transport. `LaunchpadManager` becomes one caller of
  `SessionPlayer` among others, alongside `ClipGrid` and `TerminalUI`.
- **Why first:** it's a big move with no behaviour change, so the e2e
  suite is the check that nothing moved wrong - keeping it out of the
  same commit as Phase 2's real change of where launched notes come
  from. It also gives `SessionPlayer` unit tests, which
  `LaunchpadManager` can't have today (it isn't in `synth_engine`).
- Tests: the Launchpad e2e scripts, unchanged and green; new unit tests
  for launch/queue/stop bookkeeping and the bar-quantized resolution,
  now that they're reachable without a device.

---

## Phase 2: launched clips play inside the transport, per track

Done: one transport for both views (Space included); a launch or stop
takes its track over from the arrangement at the next bar, with the
first launch into silence waiting too (rewind to start from the top);
`back-to-arrangement`/`track-back-to-arrangement` hand tracks back; the
clip grid header marks a taken-over track (◆); launched clips play
through `SongState` (`queueSessionChange()`, `SessionTrackInfo`) on a
session clock that seeks and pattern breaks don't move, with their own
commands; `SessionPlayer` predicts each change until a snapshot catches
up and keeps the Session View takes, now running on the transport. A
Launchpad fader move made while a Session View take records on its track
goes into the take's clip, at the row a note pressed then would land on
(`recordFaderAutomationIfArmed()`); otherwise it goes to the arrangement
background, as before.

---

## Phase 3: per-track playheads in the Session view pattern editor

Done: each track has its own position in `ScenePatternSource` - a playing
track's is its playhead (it can't be moved; Up/Down say so), a stopped
track's is wherever playback or the cursor left it, per buffer. Every
column is shown relative to the cursor track's position
(`trackAddress()`), so columns can show different clips and a playing
column shows its playhead on the highlighted row; moving the cursor moves
every stopped track along, so the highlighted row moves across still
columns and the view scrolls only near its edges; every other track
shows its position on a line of its own, which follows its playhead
within the margins as it plays - the cursor row the cursor track's - and
otherwise stays put, a stopped one moving only with the cursor moved by
hand; regions resolve each track's
rows the same way (`PositionedSceneGrid`). Each column's heading names
its clip, and each clip grid column marks its own track's clip - the
clip grid's own cursor never moves a track; only the pattern editor or
a launched clip starting does.

---

## Phase 4: one flat arrangement timeline, shown as bar.beat.sixteenth

Done: the transport reads `1.3.3` - bar, beat, sixteenth, all 1-based
(`Song::formatPosition()`) - in the info bar, replacing the hex row and
`pattern:N`; the elapsed time went with them. `Section` is gone: `Song`
holds one `Arrangement`, a timeline keyed by absolute row (instance
events, inline patterns, `SampleTrack` background beds - the bed now
starts at row 0), `<arrangement>` in the file with no legacy reader.
Its length is where its content ends (`Song::getArrangementLength()`; a
clip counts one pass, a stop its own row); a looping clip plays on until
its track's next event, and placing one clears only its first pass.
Annotations became song-level locators (not bar-quantized; `ArrangementGrid`
marks each bar that has one, not the row), and `ZBxx` jumps to locator `xx`, `00` the next one;
an offline render ends where a break jumps back. `ArrangementGrid` is one
run of bars; `ArrangementPatternSource` is one block of absolute rows
rather than a block per bar, since a block bounds a selection and a
region must span bars (the row-number gutter widens past `ff`). Songs
were converted by `tools/annotations_to_locators.py` and
`tools/sections_to_arrangement.py`: a clip still in effect where its
section ended gets a stop there, rows past a section's end (never
played) are dropped, and section names become locators; every tracked
song renders sample-identical over its length except where the old
reader or scheduler was wrong (a leftover empty `<sections>` read
instead of the real one; a clip's track not released at a section end).

---

## Phase 5: the transport toggle only pauses

The transport toggle (Space, and every other play/stop) pauses and
resumes the transport and nothing else. The live-sequencer convention
stops every session clip with the transport and doesn't bring them back
on play; pausing launched clips instead fills a long-requested gap. See
the open question above: this is interim.

- **Pause:** nothing is stopped or forgotten: the arrangement position,
  each launched clip at its row (the session clock only advances while
  the transport runs), anything queued, which tracks are taken over, and
  a note take in progress all stay. Voices do what the arrangement's
  already do on a pause: a sample track's stop, an instrument's are left
  as they are (see the open question). A sample take ends, as before -
  audio capture has no pause.
- **Resume:** everything continues where it was.
- **Stopping clips** stays its own action - the stop pads, the master
  column's stop row, `back-to-arrangement` - and so does rewinding.
- **While paused,** a launched clip shows as launched but not moving:
  static green on its Launchpad pad and a static mark in the clip grid,
  rather than the playing pulse and ▸.
- **Launching while paused** starts the transport, as now, which resumes
  every other paused clip too.
- Tests: pause and play resumes a launched clip on the row it paused
  at, with no voices sounding in between; a queued launch survives a
  pause and lands on the first bar after play; a take in progress
  survives a pause; a paused clip's highlight is the paused one.
