# Session playback: launched clips inside the transport

Goal: Session view and Arrangement view share one transport, the way the
live-sequencer convention has it. A launched clip takes its track over
from the arrangement instead of running on a separate clock, and the
Session view pattern editor follows each track's own playhead. Split
out of `plans/clip-editor.md`, whose Phases 0-8 built the Session view,
clip grid and clip editing this builds on.

Status: not started. Every phase lands as its own commit(s), with `ctest`
and the e2e scripts green.

## Phase 1: launched clips play inside the transport, per track

Today Session-view launches and the transport are two separate playback
modes: launched clips run on `LaunchpadManager`'s own audition clock and
only while the transport is stopped, so starting the transport over them
clashes (Space in Session view does nothing for that reason). The
live-sequencer convention is one transport for both views, with each
track following either its arrangement or its Session view clip:

- **One transport.** Space starts and stops it in both views. Every
  track plays its arrangement unless it's been taken over.
- **Per-track override.** Launching a clip on a track takes that track
  over: it plays the clip, looping or one-shot, while the other tracks
  follow the arrangement. Stopping a track's clip takes it over too - it
  stays silent while the arrangement plays.
- **Back to arrangement.** A global command and a per-track one drop the
  override, so the track follows the arrangement again at the
  transport's position (bar-quantized, like a launch). The clip grid
  shows which tracks are taken over, and the Launchpad gets the same.
- **Launching while stopped** starts the transport, as a launch does
  today from silence.
- **Engine:** launched-clip playback moves from the UI-thread audition
  clock (`LaunchpadManager::fireClipStep()`'s `PLAY_NOTE`s) into
  `SongState`, which already resolves each track's content per row: a
  per-track override (the launched clip and its launch step, or a stop)
  replaces the arrangement's instance resolution for that track. This is
  the `SessionPlayer` move the clip-editor plan's Phase 1 deferred.
  Bar-quantized launch/stop and the recording queues move with it;
  audition of step-grid edits, which needs no transport, keeps its own
  clock.
- **Knock-on:** Phase 2's per-track playheads read the override's
  position from the snapshot, and the clip-editor plan's clip effect
  commands (its Phase 10) come for free, played by the same scheduler as
  the arrangement's.
- **Space** in Session view becomes the transport again, like Arrangement
  view's.
- Tests: a render test with one track taken over by a clip and another
  following the arrangement, both audible, the arrangement's position
  unaffected; back-to-arrangement at a bar boundary; a stopped override
  staying silent while the arrangement plays.

---

## Phase 2: per-track playheads in the Session view pattern editor

In Arrangement view the transport is the cursor row: the pattern
editor's highlighted row is where playback is, and moving it moves the
transport. Session view should work the same way per track: each track
has its own position - a clip (scene row) and a row in it - and while
that track plays a clip, its position is its playhead. There's no
separate session cursor: the per-buffer scene cursor (the clip-editor
plan's Phase 3) is replaced by these per-track positions.

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
  position there (row 0) - the clip-editor plan's one shared cursor
  (its Phase 4), per track.
- **Regions:** a multi-track mark/kill/yank spans the same screen rows
  in each column, each track's rows resolved through its own position -
  so one region can cover different clips in different tracks. Editing a
  playing track's clip while it plays is allowed (the notes change under
  the playhead), the same as editing under the transport.
- **Space** is the transport, in Session view too, once Phase 1 lands.
- **Source of positions:** a playing track's clip and row come from the
  playback snapshot (Phase 1's per-track overrides; until then
  `LaunchpadManager::sessionPlayheads()`); stopped tracks' positions
  live beside them, per buffer, in `ScenePatternSource`.
- Tests: two tracks launched on different scenes show their own clips in
  one pattern editor; a playing track's column can't be moved and follows
  its playhead; a stopped track's can, and keeps its position after
  stopping; launching jumps the position to the clip.
