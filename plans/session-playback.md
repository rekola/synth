# Session playback: launched clips inside the transport

Goal: Session view and Arrangement view share one transport, the way the
live-sequencer convention has it. A launched clip takes its track over
from the arrangement instead of running on a separate clock, and the
Session view pattern editor follows each track's own playhead. The
Session view, clip grid and clip editing this builds on are already in
place.

Status: Phase 1 done; Phase 2 done but for the two items under its "Still
open"; Phase 3 done. Phase 4 not started. Every phase lands as its own
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

Done: each track has its own position in `ScenePatternSource` - a playing
track's is its playhead (it can't be moved; Up/Down say so), a stopped
track's is wherever playback or the cursor left it, per buffer. Every
column is shown relative to the cursor track's position
(`trackAddress()`), so columns can show different clips and a playing
column shows its playhead on the highlighted row; moving the cursor moves
every stopped track along, so the highlighted row moves across still
columns and the view scrolls only near its edges, and while the cursor
track plays that row holds still and its column scrolls under it; any
other playing track shows its playhead on a line of its own, which
stays put as the cursor moves; regions resolve each track's
rows the same way (`PositionedSceneGrid`). Each column's heading names
its clip, and each clip grid column marks its own track's clip - the
clip grid's own cursor never moves a track; only the pattern editor or
a launched clip starting does.
