# Session playback: launched clips inside the transport

Goal: Session view and Arrangement view share one transport, the way the
live-sequencer convention has it. A launched clip takes its track over
from the arrangement instead of running on a separate clock, and the
Session view pattern editor follows each track's own playhead. The
Session view, clip grid and clip editing this builds on are already in
place.

Status: not started. Every phase lands as its own commit(s), with `ctest`
and the e2e scripts green.

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
  replaces the arrangement's instance resolution for that track.
  `SessionPlayer` (Phase 1) keeps owning the launch bookkeeping - which
  clip, from which step, what's queued - and feeds it to `SongState`
  instead of firing notes itself; its audition clock goes away with the
  transport taking over. Step-grid preview keeps its own clock.
- **Clip commands play too.** A launched clip's effect commands
  (`0Lxx`, the `Y`-namespace glides, ...) reach the audio thread today
  as nothing at all: `LaunchpadManager::fireClipStep()` sends only
  `PLAY_NOTE`/`STOP_NOTE`. Once a launched clip plays through
  `SongState`, its commands come for free, read by the same
  `applyRowCommands()` the arrangement uses for a placed clip, with
  `ZBxx` ignored there as it already is.
- **Recording automation:** Launchpad fader moves recorded while a clip
  plays (`recordFaderAutomationIfArmed()`) go to the section background.
  Decide whether a Session-view take should write them into the clip
  instead, now that a clip's own commands play.
- **Knock-on:** Phase 3's per-track playheads read the override's
  position from the snapshot.
- **Space** in Session view becomes the transport again, like Arrangement
  view's.
- Tests: a render test with one track taken over by a clip and another
  following the arrangement, both audible, the arrangement's position
  unaffected; back-to-arrangement at a bar boundary; a stopped override
  staying silent while the arrangement plays; a launched clip's own
  volume command changing its level.

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
