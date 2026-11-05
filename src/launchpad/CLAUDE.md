# Launchpad

Hardware I/O and pad/button semantics for Novation Launchpad X / Mini MK3 /
Pro MK3 (ALSA sequencer clients, auto-detected by device name,
`LaunchpadProtocol::modelFromDeviceName()`). The app works fully without one.
The clip/Live View model these gestures drive is in
`.claude/rules/song-model.md`; the terminal widgets in `src/ui/CLAUDE.md`;
user-facing descriptions of every button in `docs/launchpad.md`.

## Ownership

- `LaunchpadIO` owns raw MIDI I/O (connect, hotplug, SysEx) on a
  platform-neutral `MidiPortBackend`; tested against a fake backend.
- `LaunchpadManager` owns all per-device state and logic (note entry,
  grid-mode dispatch, Live View, step grid). Song/Controller-aware, UI-agnostic:
  it works the same with `HeadlessUI`.
- Pad/button handling that reaches the app lives in `UI` and is shared by
  `InteractiveUI` and `HeadlessUI`.
- `LaunchpadIO`'s destructor blanks every LED on every device
  (`clearAllLeds()`) before closing. It never leaves Programmer Mode; going
  dark is the "we're done" signal.
- Commands only a pad dispatches are named `pad-*`, never `launchpad-*`.

## Grid modes

`GridMode` (`NOTES`/`SEND_MAIN`/`PAN`/`SEND_A`/`SEND_B`/`DRAW`/`LIVE`/
`CUSTOM`/`TEMPO`/`SWING`) is per device, mutually exclusive, never tied to
terminal focus: one device can sit in Live View while another does note
entry. Defaults to `LIVE`.

- **Live/Note/Custom (CC95/96/97) + DRAW** are one exclusive four-member
  group: each press selects that mode unconditionally (even the active one),
  so the only way out of a mode is selecting another. DRAW is entered by
  shift + Custom only; it is a per-pad colouring toy independent of the song
  (shift + Custom again blanks the canvas). The device labels 95 "Session"
  and 98 "Session Record"; keep those names in user-facing text, the code
  stays neutral.
- **`CUSTOM`** is the performance grid (`playback/PerformanceEffect.h`,
  `state/PerformanceAudio.h`): columns are Live View's tracks, rows effects
  (default order: Filter, Stutter, Shuffle, Erode, Shatter, Gate, Crush, Duck;
  Orbit and the Send A/B throws exist off the grid), held to apply, pressure
  deepening them. The button at a row's end holds the effect on the master.
  A performance is held state, never the song: `SongState::setPerformance()`
  applies it to the voices only, so `sends_`/`position_` (what `TrackInfo`
  reports and the song mirrors) keep the song's values. The audio effects
  run on a track's whole rendered block before its level is reported.
- **Mixer submode** (`DeviceState::mixer_mode`, off by default) is toggled by
  a repeat press of CC95 while already on the plain Live grid with nothing
  active. The right-column buttons 19/89/79/69/59/49/39/29 (Record Arm /
  Volume / Pan / Send A / Send B / Stop Clip / Mute / Solo; Pro MK3 also
  30/20 for Mute/Solo) share one dispatch keyed on it:
  - **off**: each launches the whole scene at `row = (cc - 19) / 10`
    (`triggerSceneRow()`: every visible track's clip at that row, through the
    same audition/assign split as a pad press). All eight show a uniform dim
    white, never a mixer hue.
  - **on**: a radio group of seven: Volume/Pan/SendA/SendB enter that fader
    `GridMode`; Stop Clip/Mute/Solo/Record Arm open or retarget the
    track-picker overlay (below). Pressing a different member switches at
    once; pressing the active one closes back to the plain Live grid. A hold
    of >= 600 ms (`kMixerHoldPreviewThreshold`) on a press that *switched*
    reverts on release (momentary preview); a tap sticks. Reachable only from
    `LIVE`, which keeps the fader column mapping (first 8 root tracks) from
    ever showing beside the picker's mapping (`live_.track_ids`, Live View's
    filtered list) — the two can differ.
  CC95's LED: dim green outside the Live family, bright green in scene-launch
  submode, bright orange in mixer submode. Once submode is on, each of the
  eight shows its own hue, bright only when active.
- Mute/Solo/Record Arm from a Launchpad never act on the followed track
  directly; `toggle-mute`/`toggle-solo` (`PatternEditor` commands) and the
  global `toggle-record-arm` are keyboard/M-x only, the latter reachable on the
  pad as shift + CC98. In `NOTES` (no step grid showing) CC19 starts/stops
  note capture instead (red LED, bright while capturing; also ends a sample
  take).

## Buttons and shift

- CC91/92/93/94 are `move-row-up`/`-down`/`pad-prev-track`/`pad-next-track`
  (`LaunchpadProtocol::commandForButton()`). CC91 held is **shift**; its own
  meaning fires on release of a plain tap, so a press that combines with a
  pad never has to be undone. 92/93/94 fire on press.
- Shift relabels the right column in every `GridMode`, following the Pro
  MK3's own shift layer where it has one: Record Arm = Undo, Mute = Redo,
  Solo = metronome (`toggle-metronome`, a click per beat accented on the bar,
  `Player::scheduleMetronome()`; amber LED), Volume = Duplicate, Pan = Delete,
  Send A = Quantise (held with a pad: `quantizeClip()` snaps that clip's notes
  to rows; tapped alone: `toggle-record-quantize`, resolved on release;
  red/green LED), Send B = Tempo view, Stop Clip = Swing view, Custom = DRAW.
  LEDs show only these functions while shift is held (Undo/Redo white only
  while something can be undone/redone).
- **Shift + pad** selects a clip slot (`Controller::selectClipSlot()`): moves
  the shared track cursor and the clip grid's cursor there, empty slots
  included, without launching or opening. Shift + Note (CC96) then opens or
  closes the selected clip for step editing. Both halves light white while
  held.
- **Duplicate** (held): a pad on a populated Live View slot copies the clip
  into the slot below, overwriting (`duplicateClip()`, an independent copy
  under a fresh id; an overwritten clip's placements go with it). Several per
  hold. There is deliberately no command for this: the keyboard duplicates
  by copying in the clip grid and yanking.
- **Delete** (held): a pad deletes what its slot holds, one layer per press
  (`Controller::deleteClipSlot()`): a populated slot loses its clip (leaving
  an empty hole so scene rows stay aligned), an empty one its stop button.
  A clip playing or queued while the transport runs is never pulled from
  under the playhead: `ClipPlayer::deleteClip()` stops the track at the next
  bar and removes it once that took effect. The clip grid's `kill-region`
  goes through the same path (after copying to its clipboard; the pad never
  copies). No undo or confirmation.
- **Session Record (CC98)** needs press and release. A tap is
  `ClipPlayer::toggleOverdub()`: from the next bar each armed track's playing
  clip (the followed track's if none is armed) is overdubbed in place; while
  any take is in flight a tap stops them at the next bar. A pad press never
  overdubs by accident: on an armed track a populated slot only launches, an
  empty one starts a fresh take. Takes record raw sub-row timing into each
  note's delay unless `Song::getRecordQuantize()` snaps them. A long hold is
  Capture MIDI, a stub. Shift + CC98 is the arrangement's own Record Arm
  (`toggle-record-arm`). LED bright red while anything records, dim red
  otherwise.
- **Tempo and Swing views** (`TEMPO`/`SWING`): the value is drawn as digits
  on the pads (`LaunchpadLayout::renderNumber()`: tens white and centred,
  units beside it, a narrow 2-column hundreds digit before it in the view's
  colour — blue Tempo, orange Swing — which is what lets them touch with no
  margin). CC91/92 are up/down (CC91 is not shift there): step on press,
  repeat after 400 ms every 100 ms (`tickNumberView()`). Plain Send B / Stop
  Clip switch between the views; repeating the entry gesture or CC95/96/97
  leaves; everything else does nothing. Edits go through
  `Controller::setTempo()` (20-300) and `setSwing()` (50-75 %; the second
  note of each eighth-note pair plays late, `swing.h`), also the
  `tempo-`/`swing-increase`/`-decrease` commands. Library rhythms carry their
  own swing, adopted on Add to Song.

## Note mode and the step view

- `NOTES` follows the assigned track. A `PercussionTrack` gets a fixed 4x4 GM
  drum rack bottom-left (notes 36-51, left to right then bottom to top,
  `docs/drums-and-sequencer.md`); there is no per-track drum list, the rack is
  the one kit. A pitched track gets the isomorphic keyboard
  (`LaunchpadLayout::noteForPad()`): the song's key at the anchor pad at this
  device's octave, pads coloured by consonance class from the tonic. The
  song's scale does not change the playing keyboard; it picks the degrees the
  step view steps through.
- With a clip open for editing (`Controller::getFocusedClipTrackId()`, and no
  Live View take recording) the grid splits Push-style
  (`DeviceState::show_step_grid`): rows 0-3 stay the playing surface, rows 4-7
  are 32 steps (`kStepWindow`) of the *selected* sound — the last surface pad
  pressed (white; the first pad until one is pressed, reset on every open). A
  surface press selects and then falls through to ordinary note entry; a step
  press toggles that sound there, writing only to the focused clip's own
  live-linked `Pattern` (never the track's background pattern, which has no
  pagination) and auditions it. A pitched step also writes a note-off one row
  later. One sound at a time; no chords.
- `pad-prev/next-track` scroll the step window by 4 (`kStepGridScrollStep`);
  `move-row-up/-down` move a pitched track's selected sound one scale degree
  (`neighbourScaleNote()`); a drum rack has nothing to step.
- The step view is opened only from a Launchpad (shift + pad in Live View,
  or shift + Note on the selected slot), via
  `Controller::toggleDrumClipFocus()`; the pad half resolves on its own
  release. Opening switches *that* device to `NOTES` (others keep their mode)
  and resets its page, octave and `preview_clock_` (separate from
  `ClipPlayer`'s). A lone CC95 press, or the same gesture on the same pad from
  the plain Live grid, closes it. Navigating the cursor onto a track or
  recording into it never opens it. While it shows, CC96 and idle Session
  Record go dark; CC91/92 are dark on a percussion track; CC93/94 go dark once
  the clip fits the connected devices.

## Live View on the grid

Rows are the followed track's clip list, columns the one shared cursor track
every device follows (no per-device detachment). Record Arm gates
trigger-live (off) vs assign-into-the-arrangement (on). Playback semantics
(bar quantisation, takeover, `ClipPlayer`) are in `song-model.md`.

- LEDs: each pad's identity hue (`DeviceState::clip_colors`) gets a
  transport overlay (`ClipHighlight`): playing pulses and queued flashes a
  fixed green via the SysEx `FLASH`/`PULSE` lighting types, which only take a
  128-entry palette, not RGB — hence a fixed green rather than the pad's hue;
  the device animates on its own clock, so `refreshLeds()`'s send-on-change
  dedup never re-sends them. An armed track's column switches to a red
  overlay instead (`ARMED_EMPTY`/`RECORD_QUEUED`/`RECORDING`/
  `RECORD_STOPPING`, never alongside the green states): empty slot static dim
  red, press-to-record flashes, an in-flight take pulses, re-pressing the
  recording pad (queuing its stop) flashes. Red is Stop Clip's hue reused,
  not a fifth colour.
- A `SampleTrack` is the exception to the *queued* half: real audio capture
  is a single, immediate, global target (`Controller::armThresholdRecording()`
  via `ClipPlayer::triggerClip()`'s SampleTrack branch), never bar-quantised
  or fanned out to other armed tracks — there is one input stream. Pressing
  the same pad cancels a still-idle arm (`stopSampleTrackRecording()`). Never
  route a SampleTrack take through `Controller::trimClipRecordingClip()`: it
  reads the take's empty note pattern as "nothing recorded" and resets the
  audio length to one bar.
- An empty pad queues a stop unless its stop button was removed
  (`Clip::hasStopButton()`, no ⏹ in the clip grid): then launching it, alone
  or in its scene, leaves the track alone.

## Track-picker overlay

Opened by Stop Clip / Mute / Solo / Record Arm in mixer submode, Live View
only: a no-op from any other `grid_mode`, and every reassignment off `LIVE`
closes it. It lights just the bottom row, one pad per selectable track, and
leaves the rest of Live View untouched (no dimming; only that row is routed
here, `isTrackPickerRow()`). The restriction to Live View exists because
opening it over Send/Pan once showed the picker's column order beside the
faders' different order, which read as the grid rotating.

- The row shares one hue naming the purpose (red Stop Clip, blue Solo, yellow
  Mute, red again Record Arm; the opener's LED matches), with brightness as
  the column's current state: bright = a clip is playing / already soloed /
  *not* muted (muted reads dark, like a fader at the bottom) / already armed.
- Picking a track performs the purpose (a quantised stop;
  `Controller::toggleTrackMuted()`/`toggleTrackSolo()`/`toggleTrackArmed()`,
  pure per-track bookkeeping for every track type) and leaves the overlay
  open, since every purpose is a toggle. The same opener closes it; a
  different opener retargets it.

## e2e coverage

`tools/e2e/verify_launchpad_*.py` drive simulated Launchpads (see that
directory's `README.md`): live trigger/assign and record arm, note/custom
mode switching, DRAW entry and clear, each picker purpose, scene rows, mixer
hold-to-preview, fader automation landing in the take's clip, SampleTrack
record arm, shift + step-grid opening (percussion and pitched), step view
with two devices, tempo/swing views, shift LED feedback. Solo's picker
purpose has no script of its own yet. Real hardware and simulators never
mix: a simulator's ALSA client name ends in `(e2e)` and is skipped by an
ordinary `synth`; the harness spawns `synth` with
`SYNTH_LAUNCHPAD_NO_HARDWARE=1`. Scripts wait with `Screen.wait()`, never
`time.sleep()` — an unread pty blocks the UI thread.
