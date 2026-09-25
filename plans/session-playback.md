# Session playback: launched clips inside the transport

Goal: Session view and Arrangement view share one transport, the way the
live-sequencer convention has it. A launched clip takes its track over
from the arrangement instead of running on a separate clock, and the
Session view pattern editor follows each track's own playhead. The
Session view, clip grid and clip editing this builds on are already in
place.

Status: Phase 1 done; Phase 2 done but for the two items under its "Still
open". Phases 3-4 not started. Every phase lands as its own
commit(s), with `ctest` and the e2e scripts green.

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
up and keeps the Session View takes, now running on the transport.

Still open:

- **The Launchpad's taken-over indicator.** The clip grid shows which
  tracks are taken over; the Launchpad doesn't yet. Pick where it shows -
  a button's LED (a "back to arrangement" button, lit while any track is
  taken over, would also give the hardware the command itself), or a
  per-column mark in Session view - and whether a press there returns
  every track or just the column's.
- **Recording automation.** Launchpad fader moves recorded while a clip
  plays (`recordFaderAutomationIfArmed()`) still go to the section
  background. Now that a launched clip's own commands play, and a
  taken-over track ignores its arrangement automation, decide whether a
  Session View take (or an overdub) should write them into the clip
  instead - otherwise a fader move recorded during a take is inaudible
  until the track returns to the arrangement.

---

## Phase 3: per-track playheads in the Session view pattern editor

In Arrangement view the transport is the cursor row: the pattern
editor's highlighted row is where playback is, and moving it moves the
transport. Session view should work the same way per track: each track
has its own position - a clip (scene row) and a row in it - and while
that track plays a clip, its position is its playhead. There's no
separate session cursor: `ScenePatternSource`'s per-buffer scene cursor
is replaced by these per-track positions.

- **Playing track:** its column follows its playhead exactly the way
  Arrangement view follows the transport - the playhead sits on the
  pattern editor's highlighted row and the column's content scrolls under
  it, crossing into the next clip or looping as the clip does. You can't
  move it: Up/Down on that track do nothing (or the status line says it's
  playing), as moving the transport does nothing while recording.
- **Stopped track:** its position is yours to move - Up/Down, Page, jumps
  to a clip - and stays where playback left it when the clip stops.
  Launching a clip jumps the track's position to that clip's row 0.
- **Different scenes per track:** so the Session view pattern editor can
  show a different clip - a different scene row - in every column: some
  tracks playing different scenes, some stopped wherever they were left.
  Each column's header shows which clip (scene row) it's in.
- **Moving between tracks:** Left/Right keep the highlighted screen row;
  each column shows its own track's position there. The row-number
  gutter shows the cursor track's rows.
- **The clip grid:** its scene row marks the cursor track's position
  (its clip); a playing track's slot already shows its state. Moving the
  clip grid's cursor onto a clip of a stopped track moves that track's
  position there (row 0) - the one shared cursor of the two widgets
  (`TerminalUI::syncSessionView()`), per track.
- **Regions:** a multi-track mark/kill/yank spans the same screen rows
  in each column, each track's rows resolved through its own position -
  so one region can cover different clips in different tracks. Editing a
  playing track's clip while it plays is allowed (the notes change under
  the playhead), the same as editing under the transport.
- **Space** is the transport, in Session view too, once Phase 2 lands.
- **Source of positions:** a playing track's clip and row come from the
  playback snapshot (Phase 2's per-track overrides; until then
  `LaunchpadManager::sessionPlayheads()`); stopped tracks' positions
  live beside them, per buffer, in `ScenePatternSource`.
- Tests: two tracks launched on different scenes show their own clips in
  one pattern editor; a playing track's column can't be moved and follows
  its playhead; a stopped track's can, and keeps its position after
  stopping; launching jumps the position to the clip.
