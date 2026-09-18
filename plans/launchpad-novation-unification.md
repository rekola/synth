# Launchpad Mixer Mode: closing the gap with Novation's own Session mode

## Shipped

- Session pad flash/pulse for playing/queued clips, matching Novation's
  own published guide (`LaunchpadManager::SessionPadHighlight`).
- Fader velocity-sensitive glide + micro-values (`FaderState`,
  `resolveSendFaderTarget()`/`resolveAzimuthFaderTarget()`) for all four
  of Volume/Pan/Send A/Send B alike - see below for how Pan's own glide
  ended up server-side too, same as the other three.
- Momentary hold-to-preview across the seven mixer-submode radio-group
  buttons (`armMixerHoldPreview()`/`handleMixerFunctionRelease()`).
- Multi-column `Command` storage (`Pattern`/`Section`:
  `setCommand`/`getCommand`/`pushCommand`/`getCommandsAt`, column-0
  shorthand kept for every pre-existing single-command call site), with
  XML persistence and the `SongState.h` masking fix - a background
  `Section` command now always fires regardless of whether a `Clip`
  instance is what's actually supplying that row's notes.
- New pattern effect commands: `0Lxx`/`0Fxx`/`0Mxx` (Volume/Send A/Send B
  absolute set) and `0Pxx` (absolute azimuth set), alongside a
  re-lettering of the pre-existing azimuth-slide commands to `0Hxx`/`0Kxx`
  - see `docs/commands.md`'s own "Source" column for Renoise precedent.
- The live-recording capture path itself: a Launchpad mixer fader press
  writes a `0Lxx`/`0Fxx`/`0Mxx` command into the current playback
  row/section's background `Pattern`, gated on Record Arm + genuinely
  playing (`LaunchpadManager::recordFaderAutomationIfArmed()`). Recorded
  exactly once per press, not per glide tick - the press's own glide
  toward its target is Launchpad's real-time/audio-feel interpolation
  only, deliberately not baked into the recording (see the deferred
  interpolation phase below for why).
- Command's own device-index digit (first character): documented
  (`docs/commands.md`) as how far up the track's own ancestor chain a
  command targets (`-`/`0` = the track itself, `1` = parent, `2` =
  grandparent, ... - not implemented beyond `0` yet), and validated
  accordingly (`Command::updateData()` only accepts `Z`, a digit, or `-`
  at column 0 now, not an arbitrary letter).
- `PatternEditor` always shows/edits the background `Section`'s own
  command column now, even on a row where a `Clip` instance supplies the
  notes - previously read/wrote through whichever `Pattern` the note
  columns came from, which showed a `Clip`'s own (unused, always empty)
  command data instead of what's actually going to play.
- Send Main/A/B's own *live* glide moved server-side - Launchpad no
  longer interpolates them locally at all; a Send press instead resolves
  its target once and sends it, with the same velocity-derived duration as
  before, in a single event - `Controller::glideTrackSendA()`/`B()`/
  `Main()`, new `PlaybackControlEvent::GLIDE_TRACK_SEND_*` types). The
  engine glides it (`dsp::ValueRamp`, `LeafTrackState::glideSendA()`/
  `advanceSendRamps()`, advanced once per render chunk, reaching
  already-sounding voices the same way an instant set always did). Since
  the model no longer gets ticked through every intermediate value the
  way the old client-side ramp incidentally kept it in sync, it's now kept
  honest a different way: the engine reports its own real, possibly-mid-
  glide value back every snapshot (`TrackInfo::getLiveSendMain()`/`A()`/
  `B()`, `Controller::syncLiveGlideStateIntoModel()`, called from
  `receivePlaybackSnapshot()`) - so `LaunchpadManager`'s own LED refresh
  needed no change at all, still just reading the model. Automation
  recording is unaffected - `recordFaderAutomationIfArmed()` still only
  ever needs the resolved target at press time, which is still known
  immediately regardless of how the value is later reached. Pan later got
  the exact same treatment (below), reusing this same model-sync
  mechanism (`TrackInfo::getLiveAzimuth()`, folded into the same
  `syncLiveGlideStateIntoModel()`). Covered by
  `ValueRampTests.cpp` (the ramp in isolation), `SendLiveUpdateTests.cpp`
  (the glide through a real `LeafTrackState`/`SongState`, and an instant
  set correctly cancelling one in flight), `PlayerMultiBufferTests.cpp`
  (the real event end to end, ms-to-frames conversion included), and
  `ControllerTests.cpp` (the model-sync itself, both the positive case
  and "no live data yet - leave the model alone"). Three bugs found and
  fixed once this was actually used on real hardware: the ramp initially
  interpolated linear gain directly, moving through Launchpad's own
  dB-mapped rows very unevenly (fast near the bottom, crawling near the
  top, since a uniform linear step is very much not a uniform dB one) -
  fixed by ramping in dB throughout (`LeafTrackState.h`'s own comment),
  converting to linear only where `sends_` actually needs it; `refreshLeds()`'s
  own "which row is the fader's own current one" derived
  it by rounding the live value to the *nearest* row, which silently
  flipped to the row *above* once a micro-value passed halfway toward it,
  bleeding that row's own brightness scale onto a pad nobody pressed -
  fixed by preferring the row actually last pressed
  (`FaderState::last_pressed_row`) whenever the naively-derived row is
  still consistent with it, falling back to the derived row otherwise (a
  value that moved some other way, e.g. automation); and the velocity-to-
  duration curve was linear against the full 1-127 MIDI range, which
  assumed a genuinely hard press reaches close to 127 - confirmed false by
  capturing real raw velocity bytes (`aseqdump`) from an actual Launchpad
  X: max-effort presses landed mostly in the 65-92 range, one hit of
  nearly twenty reaching 127, against a Launchpad Mini MK3's own non-
  velocity-sensitive pads reporting a flat 127 unconditionally - meaning
  a genuinely hard X press was gliding 10-17x slower than Mini MK3's
  guaranteed-fast one, not just "somewhat" slower. Fixed with a gamma
  curve (`kFaderVelocityGamma = 0.3f`, `faderGlideDurationSeconds()`,
  shared by every one of `resolveSendFaderTarget()`/
  `resolveAzimuthFaderTarget()`) compressing the top of the velocity range
  so a realistic hard press already reaches most of the way to full
  speed, while a genuinely soft press still glides meaningfully slower.
- Fader micro-values reduced from 4 to 2 (`FaderState::micro_step`,
  `resolveSendFaderTarget()`/`resolveAzimuthFaderTarget()`/
  `refreshLeds()`) - 8 rows * 2 micro-values = 16 distinct positions,
  matching the 4-bit target resolution `YMxy`/`YAxy`/`YBxy` (see
  `docs/commands.md`) can actually record.
- Pan's own live glide moved server-side too, same as Send Main/A/B
  above: an abrupt press already reached every already-sounding voice
  instantly (`LeafTrackState::adjustAzimuth()`), so it needed the same
  velocity-scaled glide treatment, not just Send's own "an instant gain
  step is audible" reasoning. `LeafTrackState::glideAzimuth()`/
  `advanceAzimuthRamp()`, a new `PlaybackControlEvent::
  GLIDE_TRACK_AZIMUTH`, and `Controller::glideTrackAzimuth()` mirror the
  Send machinery exactly, with one addition Send never needed: azimuth
  wraps at +-180 degrees, so gliding toward a target has to pick which
  way around the circle is actually shorter (e.g. 170 to -170 degrees
  should move 20 degrees through +-180, not 340 degrees back through 0).
  `glideAzimuth()` wraps the delta into (-180,180] before starting the
  ramp, and `resolveAzimuthFaderTarget()`/`LaunchpadManager::
  applyFaderPress()`'s old row-to-row micro-value math both got the same
  wrap so a micro-value tap near row 7/row 0 no longer computes an
  implausible ~315-degree step either (a latent bug in the old client-side
  code, not just a gap the move to server-side needed to fill).
  `LaunchpadManager::applyFaderPress()`/`tickFaderRamps()`/
  `hasActiveFaderRamp()` are gone entirely now that no fader family has a
  client-side glide left to tick. A fourth bug found once this was
  actually used: repeatedly pressing two Pan rows exactly 180 degrees
  apart (an ordinary back-and-forth gesture) walked `position_.azimuth`
  up by 180 degrees on *every* press - the exact-180-degrees tie in the
  wrap math always broke the same direction, so it never averaged out,
  only accumulated (reported as the live azimuth running into the
  thousands of degrees after enough presses). Fixed by folding
  `position_.azimuth` (and the ramp's own state) back into a bounded
  range whenever `glideAzimuth()` starts a fresh glide with the ramp at
  rest - purely a change of reference point, congruent mod 360, so
  nothing audible moves; skipped whenever a press retargets a
  still-in-flight glide, since the ramp's own `target_` would desync from
  a rebase mid-flight. `0Hxx`/`0Kxx`'s own unbounded accumulation
  (deliberately needed to reach "behind" a track) is untouched - only
  `glideAzimuth()`'s own resting baseline is rebased. Covered by
  `SendLiveUpdateTests.cpp`'s own
  `leaf_track_state_glide_azimuth_does_not_drift_under_repeated_opposite_presses`
  (confirmed to actually fail without the fix, not just pass vacuously).
- Pan's own grid rotated 90 degrees relative to Send A/B/Main: row y is
  the track, column x sets that track's azimuth (a horizontal fader),
  rather than Send's column x is the track, row y the level. Resolved
  once in `handlePadEvent()` (`track_index`/`position_index`) and again
  in `refreshLeds()`'s own rendering loop, both keyed on the same
  `grid_mode == GridMode::PAN` check.
- Pan's own row indicator shows each column in that track's own identity
  color (`DeviceState::track_colors`, same hue/lightness Session view's
  own `session_colors` uses) instead of one fixed hue - unlike Send A/B/
  Main's own bargraph fill, a single lit cell per column has no shape of
  its own to tell columns apart by, so it needs the color to do that
  instead.
- `YMxy`/`YAxy`/`YBxy`/`YLxx`/`YRxx` actually built now - full detail
  (encoding, letter choices, source checks) lives in `docs/commands.md`'s
  own Implemented table, the authoritative version. `0Fxx`/`0Mxx` are
  marked deprecated there but still fully functional - not retired from
  the code. `0Hxx`/`0Kxx` are gone outright (not merely deprecated) -
  this whole feature was greenfield, no legacy songs to keep them
  working for, so `isAzimuthSlide()`/`getAzimuthSlidePerTick()` only ever
  recognize `YLxx`/`YRxx` now. `Command::isVolumeGlide()`/
  `isSendAGlide()`/`isSendBGlide()`/`getGlideTargetDb()`/
  `getGlideDurationSeconds()`/`volumeGlide()`/`sendAGlide()`/
  `sendBGlide()` (`YMxy`/`YAxy`/`YBxy`, target in dB not linear gain -
  `LeafTrackState::glideSendMain()`/A()/B()'s own argument type, unlike
  `0Lxx`/`0Fxx`/`0Mxx`'s linear-gain `getSendSetLinear()`/`volumeSet()`/
  etc.). `SongState.h`'s own command-handling loop starts a real
  `LeafTrackState::glideSendMain()`/A()/B() ramp for `YMxy`/`YAxy`/`YBxy`
  (frames from `getGlideDurationSeconds()` via this buffer's own real
  sample rate, same conversion `Player.cpp`'s own `GLIDE_TRACK_SEND_*`
  handling uses) - the deferred "reproduce the actual recorded glide, not
  just jump to its final value" phase this file used to track as not
  started. `LaunchpadManager::recordFaderAutomationIfArmed()`'s own
  SEND_A/SEND_B/SEND_MAIN callers now build `Command::sendAGlide()`/
  `sendBGlide()`/`volumeGlide()` (target + duration together) instead of
  the old `sendASet()`/`sendBSet()`/`volumeSet()` (target only, duration
  silently discarded). Covered by `CommandTests.cpp` (parsing/round-trip/
  clamping, plus `updateData()` accepting `Y` at column 0),
  `SendSetCommandTests.cpp` (a placed `YAxy` command actually starting a
  real multi-block glide during playback, not an instant jump).
- `YZxy` - azimuth's own equivalent of `YMxy`/etc., and the Launchpad Pan
  branch now actually records it (`recordFaderAutomationIfArmed()`,
  previously a no-op for Pan - see `docs/commands.md`'s Implemented
  table). Deliberately not called `YPxy`/given `0Pxx`'s own encoding:
  `0Pxx`'s `xx` only reaches half the circle (a real, inherited Renoise
  limitation), which would throw away exactly the range a live Pan press
  can reach, so `YZxy`'s own `x` spans the *full* circle instead -
  `Command::isAzimuthGlide()`/`getAzimuthGlideTargetDegrees()`/
  `azimuthGlide()`, `SongState.h`'s own dispatch to
  `LeafTrackState::glideAzimuth()`. Covered by `CommandTests.cpp`
  (parsing/decoding/wrapping) and `SendSetCommandTests.cpp` (a placed
  `YZxy` command starting a real glide, not an instant jump).
- Every connected Launchpad's LEDs blank on exit (`LaunchpadIO::
  clearAllLeds()`, called from `~LaunchpadIO()`) - a final all-black
  LED-lighting SysEx covering the 8x8 grid plus every extra-button index
  (`LaunchpadProtocol::allExtraButtonLedIndices()`, factored out as the
  shared source of truth alongside `LaunchpadManager::refreshLeds()`'s own
  per-button enumeration), sent before closing the ALSA connection - not
  a Programmer Mode exit (this codebase never actually leaves it once
  entered), just going dark, a clearer "we're done" signal than whatever
  a device's own standalone light show would otherwise resume showing.
  Covered by `tools/e2e/verify_launchpad_clear_on_exit.py` - quits `synth`
  gracefully (C-x C-c) against a fixture with real Session content and
  confirms the very last LED message the fake device receives blanks
  every colorspec, after an earlier dump confirmed there was real
  (non-black) content lit to begin with. No real audio/ALSA capture
  involved, so unlike several scripts below this one isn't expected to
  hit the sandboxed-environment flakiness documented in
  `docs/known_bugs.md` - confirmed passing.

## Remaining

1. **Multi-track Record Arm - shipped, including e2e coverage of the
   recording gesture itself.** Postponed twice by explicit direction, now
   done. The design went through several real revisions before landing
   here - each one is worth keeping since the reasoning isn't obvious
   from the final shape alone. Every item under "Mechanism" below is
   shipped and tested, including the per-pad LED feedback on the ordinary
   Session grid (see its own bullet further down).
   `tools/e2e/verify_launchpad_record_arm_holes.py` (see that directory's
   own `README.md`) closes the last gap - arms a track, presses a
   Session-view clip index ahead of the (empty) clip list's own end,
   switches to NOTE mode and plays a note, then disarms, verified through
   the terminal `SessionView` widget itself: confirms the note landed at
   the exact pressed index (holes-allowed placement, not "the next unused
   slot") and that the disarmed take shows up as a real, named clip with
   no lingering record indicator. Scoped to a single armed track - the
   multi-track fan-out path (several tracks armed and recording from the
   same NOTE-mode press) still has no dedicated e2e script of its own,
   only the `Controller`-level tests already listed above.

   **Settled decisions:**
   - Real overdubbing: a track armed while its own clip is already
     playing merges newly-played notes into that clip rather than
     replacing it - not the smaller "generalize today's clear-and-restart
     behavior to several tracks" alternative.
   - **No master switch for Session view.** Session view gets no global
     armed/disarmed flag at all: a track is armed or it isn't, and "is
     anything armed" is just `!armed_track_ids_.empty()`, never a second
     flag that could disagree with the set. The real driver for this
     whole feature is future physical mixer/control-surface hardware with
     a per-track arm button on each channel strip - those buttons can't
     be reprogrammed the way a Launchpad's own layout can, so the
     `Controller`-level API has to be genuinely per-track and
     hardware-agnostic, not a picker-shaped restriction bolted onto one
     global toggle. `Controller::isNoteCaptureArmed()`/`capture_enabled_`
     (ordinary, non-Session-View note capture's own single song-wide flag)
     is unrelated to any of this and stays exactly what it always was;
     from a Launchpad it's reachable only via CC98's own quick-tap gesture
     now, never CC19 (see the hardware-wiring bullet below) - a real,
     deliberate divergence from the original plan here, which assumed
     CC19 would keep arming/disarming the current track for Session View
     the way it does everywhere else. It turned out Session view has no
     "current track" for that to mean anything for in the first place
     (every column is shown at once), and separately, CC19 sharing its
     physical right-column position with Volume/Pan/SendA/SendB/StopClip/
     Mute/Solo means it's naturally the eighth member of that same
     mixer-submode group, not a special case with its own per-track
     meaning at all.
   - Arming a track starts nothing by itself. Pressing one of its own
     pads is what starts something, and even that is quantized like every
     other Session-view action - it queues, taking effect at the next
     shared bar boundary, not immediately. The one exception is the exact
     same one `triggerSessionClip()`'s own audition path already has: if
     nothing anywhere is currently triggered or queued (including no
     other pending recording), the press runs immediately and becomes the
     new shared origin, rather than queuing against a grid that doesn't
     exist yet - confirmed against real Ableton behavior, which does the
     same thing (no quantization to wait for with nothing else running;
     that press becomes 1.1.1). No metronome/count-in is needed for this -
     none exists in this codebase today, and using the drum sequencer to
     lay down a reference groove first sidesteps the case where it would
     matter (a first take from *total* silence with nothing to sync
     against).
   - Once a track is armed, pressing *any* of its pads - empty or
     occupied - is now record-oriented; the ordinary audition
     queue/launch/swap/stop behavior only applies while unarmed. An empty
     pad queues a fresh take; an occupied one leaves that clip looping
     completely undisturbed and queues merging newly-captured content
     into it once the boundary arrives (the actual overdub). The clip's
     own length stays fixed while overdubbing - it's already looping, and
     other tracks are synced to the same shared bar grid, so it can't
     silently grow the way a fresh take's own clip does
     (`extendSessionRecordingClipIfNeeded()`'s bar-by-bar growth). Playing
     longer than the clip just means multiple passes through the same
     loop, each merging more notes into it (an ordinary looper-pedal-style
     overdub) - `ensureSessionRecordingClip()` returns
     `(absolute_step - origin_step) % clip.getLength()` whenever the
     origin was primed (the overdub signal) instead of the raw,
     ever-growing row a fresh take gets, and
     `extendSessionRecordingClipIfNeeded()` is a no-op for a primed take,
     nothing to grow.
   - A fresh take replaces whatever was already playing on that track -
     the same "only one clip plays per track" rule an ordinary swap/stop
     already enforces unarmed - unlike overdub, which leaves the already-
     playing clip running deliberately, since it's the very thing being
     recorded into. **Shipped** (`triggerClipStep()`'s own fresh-take
     resolution) - a real gap caught after the fact: the queued-resolution
     path originally left an old triggered clip looping silently alongside
     a brand new, unrelated take on the same track; the immediate-start
     path never had this problem (it only ever runs when nothing anywhere
     is triggered at all, so there's nothing on the track to stop).
   - Two different stop gestures, at two different scopes: pressing the
     pad currently being recorded into *again* queues a stop for just that
     take at the next boundary (the exact same "press the already-
     triggered pad again" gesture ordinary Session-view auditioning
     already has), leaving the track itself still armed and ready for
     another take; disarming the track itself (CC19 on it, or the picker)
     stops whatever it's currently recording *immediately*, not queued -
     matching the documented hardware LED behavior below ("if the track is
     unarmed while still recording, that stops immediately instead of
     queuing").
   - No threshold-arming (the existing pre-roll-ring-buffer/backdate-to-
     when-input-crossed-a-level mechanism) for this new path, note or
     audio alike - it stops making sense once recording start is
     quantized to a boundary instead of triggered by input; capture just
     starts writing (real or silent) content from that boundary on,
     exactly like note capture already tolerates a performer starting a
     beat late.
   - Unified across track types in principle (arming means the same thing
     regardless of what the track actually records), but SampleTrack's
     own audio capture isn't being rebuilt on the new quantized-start path
     in this pass - real-time/ALSA-thread concerns of its own, deserving
     separate treatment. Concretely: `toggleTrackArmed()` itself works for
     any track (it's just bookkeeping), but until SampleTrack support
     lands, arming one and pressing its pad should fall back to today's
     unchanged threshold-armed mechanism rather than silently doing
     nothing - the dispatch-by-type happens at resolution time, not at
     arm time.
   - Discovered, not designed: `audition_clock_` already runs
     continuously whenever Session view is being looked at with the
     transport stopped (`refresh()`'s own `audition_active` condition) -
     so none of this needs to explicitly start any clock as a side effect
     of arming or pressing a pad. Writing into the same
     `triggered_pattern_by_track_`/a new, parallel `queued_recording_by_
     track_` map is enough; the clock's own next `triggerClipStep()` tick
     picks it up exactly the way a plain queued clip launch already does.

   **Mechanism, concrete enough to implement directly:**
   - `Controller::armed_track_ids_` (`std::unordered_set<int>`) -
     `armTrack()`/`disarmTrack()`/`toggleTrackArmed()`/`isTrackArmed()`/
     `hasAnyTrackArmed()`. **Shipped.**
   - The single-slot `session_recording_`/`session_recording_track_id_`/
     `session_recording_clip_index_`/`session_recording_clip_ready_`/
     `session_recording_origin_step_` fields become a per-track map
     (`std::unordered_map<int, SessionRecordingTake>`, `SessionRecordingTake
     { int clip_index; bool clip_ready; int origin_step; }`) so several
     tracks can each have their own in-flight take concurrently -
     `armSessionTrackRecording(track_id, clip_index)` starts/retargets one;
     `primeSessionRecordingOrigin(track_id, origin_step)` overrides where
     `ensureSessionRecordingClip()`'s own first call would otherwise derive
     row 0 from (`previousBarRow()`) - reused, not replaced, for overdub:
     calling it before that first call is also the *signal* that this take
     is an overdub, so `ensureSessionRecordingClip()`'s "first call" branch
     skips resetting the target clip's own content exactly when an origin
     was already primed, no separate flag needed. `LaunchpadManager.cpp`'s
     own `session_recording_here = controller.isSessionRecording() &&
     controller.getSessionRecordingTrackId() == track_id` pattern (two call
     sites, live note-entry) becomes a direct
     `controller.isSessionRecording(track_id)`.
     `completed_session_recording_track_id_`/`_clip_index_` (a single
     "just finished" latch) becomes a small queue, since quantized stops
     make several takes finishing on the same step a real case, not a
     corner one. **Shipped**, including the wrap-not-grow overdub row
     math and the immediate-stop-on-disarm behavior, all covered by
     `ControllerTests.cpp` (`track_armed_state_is_independent_per_track`,
     `disarm_track_stops_an_in_flight_take_immediately`,
     `overdub_row_wraps_instead_of_growing_past_the_clip_length`,
     `trim_session_recording_clip_is_a_no_op_for_an_overdub`,
     `several_tracks_can_record_concurrently`, plus the pre-existing
     `ensure_session_recording_clip_*`/`trim_session_recording_clip_*`
     tests, updated to the per-track signatures).
   - The overdub origin itself: `LaunchpadManager` already has everything
     needed to compute it, no new resolution path required -
     `triggered_pattern_by_track_[track_id].launch_step` plus the boundary
     step plus `clip.getLength()` gives `(boundary_step - launch_step) %
     clip.getLength()`, the row the already-playing clip's own loop is on
     at that exact boundary; call `primeSessionRecordingOrigin()` with
     `boundary_step` minus that, so `ensureSessionRecordingClip()`'s own
     `absolute_step - origin_step` formula lands there unchanged. Real
     transport playback (not auditioning) would need the equivalent from
     `ArrangementOps.h`'s `resolveInstanceAt()` instead, but Session-view
     recording only ever happens while stopped/auditioning in the first
     place, so this doesn't come up yet. **Shipped.**
   - `LaunchpadManager::triggerSessionClip()` gains a new leading branch,
     `controller.isTrackArmed(track_id)`, ahead of the existing
     `isNoteCaptureArmed()` audition/assign split (kept exactly as it was
     underneath - now unreachable in practice through ordinary Session
     View use, since Record Arm there no longer touches that flag at all,
     but still correct for the rare case it's true from an unrelated
     ordinary note-capture arm made before switching into Session View).
     Pressing the pad currently being recorded into again queues a stop
     for just that take (`queued_recording_by_track_[track_id] =
     kQueuedRecordingStop`, checked first, since it can't be told apart
     from an ordinary occupied-pad press by clip index alone); otherwise
     not `has_pattern_here` arms a fresh take (lands at the true next
     unused slot regardless of which empty row was pressed,
     `queued_recording_by_track_[track_id] = -1`) and an occupied index
     arms an overdub of it (`= clip_index`) - both via the same "nothing
     anywhere triggered or queued (including `queued_recording_by_track_`)
     -> run immediately and become the new origin, otherwise queue against
     the existing one" rule the audition branch already has.
     `triggerClipStep()` gained a parallel resolution block next to its
     existing `queued_pattern_by_track_` one, calling
     `armSessionTrackRecording()`/`primeSessionRecordingOrigin()` (or
     `trimSessionRecordingClip()` for the stop sentinel) once the boundary
     arrives - falling back to origin step 0 rather than gating a queued
     stop on `session_origin_set_`, so a take can never get stuck
     recording forever just because nothing else ever established a
     shared grid. `stopSessionTrack()` gained the same leading branch (a
     Stop Clip press on a track currently recording queues the same
     take-stop). **Shipped** at the `LaunchpadManager`/`Controller`
     mechanism level; not yet covered by an e2e script (see
     `tools/e2e/verify_launchpad_session.py`'s own sibling scripts) - only
     exercised indirectly today, through the `Controller`-level tests
     listed above.
   - "toggle-record-arm"'s own Session-View-focused branch (Controller.cpp)
     changes from setting `session_recording_*`/calling `armNoteCapture()`
     to `controller.toggleTrackArmed(session_view_track_id_)` - except when
     the target is a SampleTrack, which keeps today's unchanged
     `armThresholdRecording()` path (see the SampleTrack carve-out above).
     **Shipped.**
   - Hardware wiring for the track-picker's own `RECORD_ARM` purpose: a
     long-hold entry point was considered and dropped - Session view has
     no single "current track" the way ordinary note entry does (every
     column is shown at once), so there's no meaningful per-track toggle
     for a *normal* CC19 press to keep doing there in the first place, and
     no need to preserve one alongside a hold gesture. Instead, a plain
     CC19 press opens the track-picker overlay directly whenever a device
     is anywhere in the Session family (`inSessionMixerFamily()`), in a
     fourth `TrackPickerPurpose::RECORD_ARM` alongside Stop Clip/Mute/
     Solo - reachable without first flipping into mixer submode, since
     it isn't one of that radio group's seven members. Picking a column
     there calls `Controller::toggleTrackArmed()` (pure per-track
     bookkeeping, uniform across track types); outside the Session family,
     a plain CC19 press keeps its unchanged legacy meaning
     (`sendCommand("toggle-record-arm")`). Reuses Stop Clip's own red hue
     for the picker row/opener-button LED (the two purposes never show at
     once). **Shipped** - `LaunchpadManager::handleRawButton()`'s own CC19
     branch, `handleTrackPickerPadEvent()`'s new case, the picker row's
     coloring, and CC19's own LED all updated; covered by
     `tools/e2e/verify_launchpad_record_arm_picker.py`
     (`fake_launchpad_record_arm_picker.c`), all 9 checks passing.
     `triggerSessionClip()`'s own armed branch also gained the SampleTrack
     carve-out this exposed as missing: a SampleTrack's pads fall straight
     through to the unarmed audition/assign behavior regardless of its own
     armed state, since its audio capture isn't on the new quantized-
     start-via-pad-press path yet (the SampleTrack carve-out described
     above). The per-pad LED feedback below (on the *ordinary* Session
     grid, not the picker row) is a separate piece, shipped later in this
     same pass - see its own bullet further down.
   - Regression this exposed: CC19 opening the picker in Session mode
     orphaned a genuinely separate pre-existing feature -
     `triggerSessionClip()`'s own `isNoteCaptureArmed()` branch (untouched
     underneath the new `isTrackArmed()` one) writes a pressed clip's
     launch into the arrangement as real song data
     (`placeClipInstance()`), rather than just auditioning it live - and
     nothing could set that flag while looking at Session view any more,
     since CC19 was its only entry point there and CC19's own meaning had
     just changed. Resolved by giving CC98 ("Capture MIDI") the same
     tap-vs-hold treatment CC19 was originally going to get, but swapped:
     since DRAW mode was already reachable by a plain press with no
     competing meaning, a *long* hold is what reaches DRAW now (entering
     it, or clearing its canvas if already there - the same split this
     gesture always had); a *quick tap* instead fires the legacy global
     `toggle-record-arm` command, reachable from any `GridMode` -
     restoring a Session-view entry point for the arrangement-recording
     feature. **Shipped** - `LaunchpadManager::handleDrawToggleButton()`
     rewritten (now takes `Controller &`); `verify_launchpad_session.py`
     (arming via CC98 instead of CC19) and `verify_launchpad_draw_clear.py`
     (CC98's own long-hold half) both updated and passing.

   **The per-pad LED feedback:** while a track is armed, every empty clip
   slot in its column lights dim red. Pressing one flashes it red (queued
   to record) with the Record button flashing in unison; once recording
   actually starts the pad pulses red and the Record button goes solid
   bright red. Pressing the Record button again while recording flashes
   the pad red to show it's about to stop. If the track is unarmed while
   still recording, that stops immediately instead of queuing. **Shipped**
   (`SessionPadHighlight`'s `ARMED_EMPTY`/`RECORD_QUEUED`/`RECORDING`/
   `RECORD_STOPPING` states) and confirmed word-for-word against
   Novation's own Launchpad X Session-mode documentation - the one piece
   not yet done is the Record button's own LED flashing in unison with a
   queued pad (it currently only shows static bright-when-open/dim-when-
   closed for the picker, not a per-track flash tied to a specific queued
   take).

   **Known divergence from real hardware, deliberately out of scope for
   now:** Novation's own documentation describes take-start as
   fundamentally retroactive, not the quantized-start-on-press this
   session built. A dedicated `[O]` ("Session Record") button, distinct
   from both Record Arm and Capture MIDI, enables overdub of whatever
   clip is currently playing on an armed track; holding Capture MIDI
   captures a rolling buffer of recently-played content and places it
   into a new (or, if one was already playing, the same) clip - a
   "grab what you already just played" gesture, not "start writing
   forward from here." Neither `[O]` nor a rolling pre-roll buffer exists
   in this codebase. The per-track arm/disarm mechanism and the per-pad
   LED feedback above are unaffected either way and match the
   documentation exactly; only the exact moment/mechanism a take actually
   begins differs. Revisiting this to match real hardware exactly would
   mean reworking `ensureSessionRecordingClip()`/
   `primeSessionRecordingOrigin()` around a real pre-roll buffer - a
   substantial change, not undertaken this pass.

   **NOTE mode's own multi-track record fan-out - shipped.** Once a clip
   is selected on an armed track (via the mechanism above), NOTE mode's
   own chromatic keyboard entry (`LaunchpadManager::handlePadEvent()`)
   needs to actually feed it - the missing piece that made the whole
   feature unusable at first (arming alone writes nothing; a note has to
   actually be played). While any track is recording
   (`Controller::isAnySessionRecording()`), it supersedes the assigned/
   cursor track entirely for both note resolution and the grid's own LED
   coloring (`refresh()`'s `pinned_track_id`) - the reference is the
   lowest track_id among `getSessionRecordingTrackIds()`, since that set
   has no other stable order. The note is resolved once from that
   reference track (never per fan-out target - tuning is song-wide, only
   percussion-vs-pitched actually branches `resolveNote()`, and a single
   grid can't be colored/mapped for two different schemes at once - see
   `resolveNote()`'s own reasoning) and duplicated into every other
   currently-recording track whose percussion-ness matches the
   reference's; a mismatched one (e.g. a percussion track alongside a
   pitched reference) is silently excluded rather than receiving a
   nonsensical note. `DeviceState::active_notes` became `vector<ActiveNote>`
   per pad (one entry per target track) rather than a second, parallel
   map, so aftertouch/chord-column bookkeeping/channel-pressure broadcast
   all get fan-out support uniformly, with no separate, feature-reduced
   path. Also fixed along the way: a Session View take's own note-write
   was incorrectly gated behind the unrelated ordinary
   `capture_enabled`/`isNoteCaptureArmed()` flag (a real "I can't seem to
   be able to record any notes" bug) - `session_recording_here` is now
   its own permission to write, independent of that flag, matching how
   arming was always meant to be independent of it.

   **Holes allowed - shipped.** Whichever pad is pressed on an armed
   track is always the recording target, exactly at that clip-list index
   - never retargeted to "the true next unused slot," since a scene
   legitimately doesn't need every instrument populated (some tracks are
   silent in a given scene, leaving a gap on purpose). `Song::
   getClips(track_id)` stays `std::vector<Clip>` (no `std::optional`); a
   gap is a genuinely empty `Clip` (`Clip::isEmpty()`, backed by a single
   `Pattern pattern_` member always present but possibly empty - see
   below), mirroring a hand-authored `<clip/>` in the song XML, rather
   than the vector simply not reaching that index. `Song::ensureClipAt(
   track_id, index)` places/reuses a clip at an exact index, padding any
   earlier missing positions with fresh empty fillers - a filler gets no
   id of its own (nothing needs to address an unused slot by identity
   until something actually gives it content, the same "assign one if it
   doesn't already have one" convention `addClip()` already follows).
   `Controller::ensureSessionRecordingClip()`'s fresh-take branch now
   calls `ensureClipAt()` with the exact pressed index instead of
   retargeting to `clips.size()`; every "is this slot populated" check in
   `LaunchpadManager.cpp` (`triggerSessionClip()`'s `has_pattern_here`,
   `refresh()`'s own per-pad LED computation) and `SessionView.cpp`'s row
   display became content-aware (`!clip.isEmpty()`) rather than
   bounds-only, so a filler reads as empty even though it's in-bounds -
   including `SessionView`'s own delete/rename/loop-toggle commands,
   which now refuse to act on a filler the same way they already refused
   on a genuinely out-of-bounds row (erasing a filler would shift every
   later clip's own index down, silently misaligning every other track's
   own scene rows against it). `LaunchpadManager::queued_recording_by_
   track_`'s value became a small `QueuedRecording{kind, clip_index}`
   struct (`STOP`/`FRESH_TAKE`/`OVERDUB`) instead of a single `int` read
   by sign - the exact pressed index is always known up front now, so
   there's no more need for a `-1` "index TBD, resolve later" sentinel;
   only which of the two real actions (fresh vs. overdub) was already
   decided at queue time still needs carrying forward.

   Along the way, `Clip` itself was simplified from a `Pattern`-per-
   `track_id` map (built for eventual nested-Effect automation capture,
   never actually used that way) down to a single `Pattern pattern_`
   member for its own leaf track - once a command can target any of its
   parent tracks directly from that one Pattern (`Command`'s own
   chain-position digit, planned but not implemented beyond the track's
   own chain position yet - see this same file's own note on that above),
   a second track's worth of content per clip will never be needed.
   `SampleContent` became a
   plain value member too (previously a lazily-`unique_ptr`-allocated
   one), for the same "empty member, not an absent one" reasoning -
   `hasSample()`/`Clip::isEmpty()` both key off content emptiness now,
   never presence/absence of the member itself. `Clip::isEmpty()` itself
   was first shipped checking only `pattern_` - missed that a
   `SampleTrack` clip's own Pattern is never touched, so every content-
   aware check built on it (this same feature's own per-pad LED/row
   display) misreported a populated sample clip as an unused scene slot;
   fixed to `pattern_.isEmpty() && !hasSample()`.

   Two other holes-allowed gaps found on closer testing, both fixed the
   same way as `ensureSessionRecordingClip()` above:
   `Controller::beginSampleCapture()` - the `SampleTrack` twin, reachable
   via the terminal `SessionView` widget's own keyboard-driven
   `toggle-record-arm` (not yet wired to the Launchpad's per-track picker
   at all - the SampleTrack carve-out elsewhere in this same file) - still
   had the exact "retarget to the true next unused slot" logic notes had;
   now calls `Song::ensureClipAt()` at the exact requested index too, and
   only assigns a fresh id/name to a clip that doesn't already have one
   (a still-id-less filler, or this take's own genuinely fresh append),
   rather than always renaming whatever it lands on.

   **Real bug, found by direct user report and fixed:**
   `LaunchpadManager::handlePadEvent()`'s own recording-supersedes-
   assigned-track override (the paragraph above the "Multi-track record
   fan-out" comment in this same file) only fired when the recording
   track's own internal id happened to sort numerically *lower* than the
   already-assigned/cursor track's - it compared each candidate straight
   against the still-assigned `track_id` instead of a fresh sentinel,
   unlike `refresh()`'s own identical override (`recording_reference_
   track_id`, this method's LED/tuning-preview counterpart), which was
   already correct. A track armed/selected later in a session almost
   always has a *larger* internal id than whichever track the cursor
   happens to already be on, so this missed the common case: the grid's
   own tuning/coloring (driven by `refresh()`) correctly showed the
   recording track, but a played note silently wrote into the assigned
   track's own background Pattern instead (audible, via its own
   unconditional `PLAY_NOTE` push, but never recorded) - exactly what was
   reported. Fixed to match `refresh()`'s own logic exactly: resolve the
   lowest recording track id into a fresh `recording_reference_track_id`
   first, then apply it unconditionally whenever it's valid.
   `tools/e2e/verify_launchpad_record_arm_wrong_track.py` covers it -
   deliberately gives the assigned/cursor track (a lane-less
   `PercussionTrack`) different percussion-ness from the recording target
   (a plain pitched track), since a first draft of the fixture used two
   plain pitched tracks and passed even with the bug still present: the
   multi-track fan-out mechanism (this same section, above) ended up
   writing the note into the recording track anyway as a side effect,
   masking the actual primary-resolution bug entirely - only a
   percussion-vs-pitched mismatch (fan-out's own compatibility filter)
   makes the fixture actually prove the fix matters.

   **Another real bug, found by direct user report and fixed:**
   `ArrangementOps.cpp`'s `deleteClip()` predates holes-allowed placement
   and still erased the clip outright (`clips.erase(...)`), shifting
   every later clip's own index down - silently misaligning every other
   track's own scene rows against it, and defeating the whole point of
   holes for the track actually being edited too (deleting scene 3 turned
   scene 4 into scene 3). Fixed to reset the slot in place to a fresh,
   id-less filler instead (`Song::ensureClipAt()`'s own "hole" state),
   same as an unused scene was always meant to look; every other clip's
   own index is now untouched by a deletion elsewhere in the list.
   `ArrangementOpsTests.cpp`/`SongTests.cpp` updated to match.

   **A third real bug, found by direct user report and fixed:**
   `handlePadEvent()`'s step-grid short-circuit (a step-sequenced
   `PercussionTrack`'s grid automatically becomes the step editor in
   NOTES mode) ran *before* the recording-supersedes-assigned-track
   override, so it was never superseded by it - recording a Session View
   take into a step-sequenced `PercussionTrack` still showed the step
   editor, and a pad press toggled a step in the *background* pattern
   instead of ever reaching the armed take at all. The step editor is for
   building a pattern by hand when not performing live; a live take needs
   real free-drumming pad entry instead (`resolveNote()`'s own percussion
   branch maps pads to GM drum sounds regardless of lane count - lanes
   only ever change what the step grid itself shows). Fixed by gating the
   whole step-grid branch on `!controller.isAnySessionRecording()`, both
   in `handlePadEvent()` and in `refresh()`'s own LED-rendering
   counterpart (a new `DeviceState::show_step_grid` field, narrower than
   `assigned_track_is_percussion && !drum_lane_notes.empty()` alone since
   the drum picker's own "already assigned" highlight still needs
   `drum_lane_notes` regardless of recording state). Covered by
   `tools/e2e/verify_launchpad_record_arm_percussion.py`, verified through
   the terminal `SessionView` widget's own text rather than exact LED
   bytes - this sandboxed environment's own pad-press-to-LED round trip
   for the step grid specifically is unreliable even on an unmodified
   checkout (confirmed via `git stash` A/B against `verify_launchpad_
   stepseq.py`/`verify_percussion_layout.py`, both already 0-checks-pass
   at HEAD - see `docs/known_bugs.md`).

   **A fourth real bug, found by direct user report and fixed - deeper
   than the third above.** The recording carve-out just above only
   patched one symptom: the step grid still showed (and unconditionally
   *wrote* into) the section's own background `Pattern` whenever the
   assigned track merely *was* a step-sequenced `PercussionTrack`, with
   nothing recording and no clip open for editing at all - simply
   navigating the shared cursor there was enough. That background
   `Pattern` has no pagination and spans the whole scene, far more than
   this fixed 8x8 grid (even split across several connected Launchpads)
   could ever show meaningfully - and, unlike ordinary note entry, the
   step grid's own writes are unconditional regardless of Record Arm
   (`handleStepGridPadEvent()`'s own "the arm flag gates performance
   capture, not editing" comment), so merely touching pads while browsing
   a laned percussion track could silently mutate live song content with
   Record Arm off. Fixed by requiring a specific clip to actually be open
   for editing on the track (`Controller::getFocusedClipTrackId() ==
   track_id`) before the step grid shows or accepts input at all - the
   same `DeviceState::show_step_grid`/`handlePadEvent()` gates the
   recording carve-out added, now also `&& drum_clip_editing`/`&&
   getFocusedClipTrackId() == track_id`. `handleStepGridPadEvent()`
   itself simplified to match - its own "no clip focused, address rows
   0-7 directly, no paging" fallback became unreachable (this handler is
   never called at all now unless a clip really is focused), so it always
   paginates against the focused clip's own length. Without a focused
   clip, a step-sequenced track now falls through to ordinary NOTES-mode
   entry exactly like a lane-less one always has - Record Arm/capture_
   enabled gates whether anything gets written, matching every other
   track type, and nothing gets written at all with Record Arm off.
   Opening a clip is unaffected: still the terminal-driven "toggle-record-
   arm" (Ctrl-X r) gesture while `SessionView` has focus - there is no
   Launchpad-only way to do this today (`CLAUDE.md`'s own drum-machine
   bullet now says so explicitly). `verify_launchpad_stepseq.py`'s own
   driving sequence rewritten to actually open the clip this way before
   expecting step-grid behavior - it previously never did, relying
   entirely on the (now-corrected) auto-show bug to reach the step grid
   at all. Verified correct via direct `DeviceState::show_step_grid`
   instrumentation (`false` before opening the clip, `true` immediately
   after, `GridMode` correctly forced to `NOTES`) rather than the e2e
   SysEx round trip, which stays blocked by the same pre-existing
   sandbox flakiness as the third bug above - confirmed via `git stash`
   A/B that this exact script already received zero SysEx at all,
   Programmer-Mode handshake included, against an unmodified checkout.

   **A fifth real bug, found by direct user report and fixed:** the four
   arrow buttons (CC91-94, move-row-up/down/prev-track/next-track) stayed
   lit with a static color unconditionally, in every `GridMode` - but
   `handleCommand()`'s own logic means "next-track"/"prev-track" are
   reserved as an unconditional no-op while `GridMode::SESSION` is active,
   and "move-row-up"/"move-row-down" there only ever move the *terminal*
   overview's own section cursor (which section an "assign" press writes
   into) - Session view's own clip-pool/track-column grid never reflects
   it, so nothing on the Launchpad itself ever visibly changes either way.
   A lit button misleadingly suggested a press there does something a
   performer looking only at the Launchpad could ever actually see. Fixed
   by gating all four LEDs dark whenever `state.grid_mode ==
   GridMode::SESSION`, lit (their original static colors) otherwise -
   `refreshLeds()`'s own extra-button section. Command behavior itself is
   unchanged (still a real, if Launchpad-invisible, side effect for move-
   row-up/down) - only the LEDs changed. `verify_launchpad_buttons.py`
   (pre-existing, unrelated to this feature) needed a CC96 (NOTES mode)
   press added before its own CC94 press, since it never switched off the
   `SESSION` default and its LED assertions had been trivially satisfied
   by the always-lit startup frame alone; its own fake device also needed
   to actually drain/print SysEx *after* sending its commands, which it
   never did before - both gaps were latent, only exposed once the LEDs
   became mode-dependent. Its third check (cursor actually moves) fails
   independent of this fix - confirmed via `git stash` A/B - see
   `docs/known_bugs.md`.

   **Sample overdubbing - shipped.** A second take recorded into an
   already-populated `SampleTrack` slot (`Controller::
   beginSampleCapture()`'s own `is_overdub = reuse_existing &&
   clip.hasSample()`) no longer replaces the existing buffer wholesale -
   `Clip` holds one or more independent `SampleContent` layers
   (`sample_layers_`, a `std::deque` rather than a `std::vector` so an
   existing layer's own address survives a later `addSampleLayer()` -
   `SongState.h`'s own render-time pending-sample-start path keeps a raw
   pointer to it alive across one render block), each take kept
   separately addressable rather than destructively summed the moment a
   second one arrives - matching the same "always merges rather than
   replaces" precedent note-based overdub already settled. What actually
   plays (`SampleTrackState::triggerClip()`, and the arrangement-timeline
   trigger path in `SongState.h`) is `Clip::getMixedContent()` - layer 0
   directly for the overwhelming majority of (never-overdubbed) clips, or
   a cached, pre-mixed sum of every layer once there's more than one,
   rebuilt off the audio thread (`Clip::rebuildMixedContent()`, `Clip.cpp`)
   by `Controller::finishSampleCapture()` once a take's audio is final -
   never computed live on a trigger, since the full resample/tempo-stretch
   pass `resolveSampleAudio()` does is documented as unsafe there. An
   overdub only ever grows the clip's own established length, never
   shrinks it. Persisted as one `<sample>` child per layer (backward-
   compatible with an existing single-`<sample>` file, which is just the
   size-1 case of the same reader loop), each with its own sidecar `.wav`
   (`sampleSidecarPath()`'s own per-layer-index suffix - `<clip-id>.wav`
   for layer 0, `<clip-id>_2.wav`/`_3.wav`/... for each later one).
   `SampleTrack` is now wired into the per-track Launchpad arm mechanism
   too: a Session-grid press on a `SampleTrack` armed via the
   track-picker overlay (`LaunchpadManager::triggerSessionClip()`'s own
   SampleTrack branch) arms real audio capture immediately
   (`Controller::armSessionTrackRecording()`/`armThresholdRecording()`) -
   a single, global (track_id, clip_index) target, never bar-quantized or
   fanned out to other simultaneously-armed tracks the way note recording
   is, since there's only one real input stream to route through it;
   pressing that same pad again cancels a still-idle arm
   (`LaunchpadManager::stopSampleTrackRecording()`). The track-picker
   overlay's own Stop Clip purpose (CC49) resolves a `SampleTrack`'s
   in-flight/armed take the same immediate way, not through the
   note-Pattern-specific queued/quantized path (`Controller::
   trimSessionRecordingClip()`) the other track types use there, which
   would misread a `SampleTrack` take's own empty Pattern as "nothing was
   ever recorded." Covered by `tools/e2e/
   verify_launchpad_sampletrack_record_arm.py`, verified through the
   terminal `SessionView` widget's own text (the "●" record indicator)
   rather than LED bytes - a genuinely armed take also engages real ALSA
   capture logic and hits the same class of sandboxed-environment
   LED-read flakiness already documented for `verify_launchpad_
   stopclip.py` (`docs/known_bugs.md`).

   **A sixth real bug, found by direct user report and fixed:** pressing
   a scene-launch button (CC19/89/79/69/59/49/39/29 with mixer submode
   off, `triggerSceneRow()`) only ever started the *first* track in the
   row instantly - every other track in the same row queued instead of
   joining it. `triggerSceneRow()` calls `triggerSessionClip()` once per
   track in a plain loop, and that function's own "is anything already
   triggered/queued anywhere -> launch immediately, otherwise queue"
   check was being recomputed fresh on every one of those calls: the
   first track's own call populated `triggered_pattern_by_track_` with
   its own entry, so every track after it in the same loop saw something
   "already playing" and queued against it instead. Fixed by threading a
   `shared_launch_step` optional parameter through `triggerSessionClip()`
   - `triggerSceneRow()` now resolves the "nothing pending, launch now"
   decision exactly once for the whole row, before touching any track,
   and every per-track call just uses the result rather than re-deriving
   it. An ordinary single-pad press is unaffected (the parameter's
   default, `std::nullopt`, leaves the old per-call logic exactly as it
   was). Covered by `tools/e2e/verify_launchpad_scene_row.py` (a
   two-track fixture, each with a clip at Session index 7) - hits the
   same real-audition-path LED stall as `verify_launchpad_stopclip.py`
   above, so the fix was verified via temporary in-process debug logging
   instead (confirmed `triggered_pattern_by_track_.size()` reaching 2,
   not 1, after a single press - since removed).

   **A seventh real bug, found by direct user report and fixed:**
   launching a clip or scene from total silence sometimes landed the
   *second* note at an uneven, musically-wrong gap after the first - a
   flam-like near-instant repeat, or an almost-full-beat-late one,
   depending on luck - before settling into correct, evenly-spaced timing
   from the third note on. `audition_clock_` (the free-running clock
   behind Session-view auditioning, shared with the drum step grid's own
   auditioning) only ever stops while the transport is playing or armed
   (`refresh()`'s own comment) - merely idle with nothing triggered, it
   free-runs continuously regardless, so a genuine launch from silence
   was inheriting whatever stale, unrelated phase the clock already
   happened to be at instead of starting fresh. The immediately-fired
   first note (`fireOrTriggerClipStep(..., 0)`, called directly by the
   press handler) still landed right on the press either way, but the
   *second* note - the clock's own next tick - fired whenever that
   leftover phase next happened to cross a row boundary, not a full row
   later as it should have. Fixed by
   `LaunchpadManager::restartAuditionClockFromSilence()`, called at every
   site that establishes a genuine launch-from-silence
   (`triggerSessionClip()`'s two branches, `triggerSceneRow()`'s shared
   decision, and the completed-session-recording auto-join) - resets the
   clock's step/phase to 0 and re-anchors its wall-clock reference to
   that exact press, so the second note lands a full, correct row later.
   Verified via temporary in-process debug logging (confirmed `step=0
   phase=0.000000 isRunning=1` immediately after a scene-row launch from
   silence - since removed); no dedicated e2e script, since this is a
   sub-row-boundary timing effect with no terminal-visible signal and the
   real-audition-path LED stall above would apply just the same.

2. **Step sequencer follow-ups - not yet designed, added to this plan on
   request.** Two related gaps left by the step-grid-only-edits-a-clip
   fix above:

   - **A Launchpad-only way to open a clip's own step grid - shipped.**
     CC91 ("move-row-up", printed with an up-arrow icon) doubles as a
     held shift modifier: pressing a Session-view pad while it's held
     opens that pad's own clip for direct step-grid editing instead of
     triggering/assigning it (`LaunchpadManager::handleShiftButton()`/
     `handleSessionPadEvent()`'s own comment). The pad half of the combo
     resolves on its own release, not its press: a press while shift is
     held is recorded (`DeviceState::row_up_shift_pending_pad`) and
     swallowed outright - never falls through to the ordinary trigger
     below, even for a clip that turns out not to be step-sequenced (a
     no-op then, not a trigger) - and only the matching release actually
     calls `Controller::toggleDrumClipFocus()`, so an abandoned press
     (shift released first, the pad dragged off) never has to be undone;
     it completes on release regardless of whether shift is still held by
     then. Reached the shared logic behind the terminal's own
     "toggle-record-arm" drum-machine repurposing by extracting it into a
     new, standalone `Controller::toggleDrumClipFocus(track_id,
     clip_index)` (returns false, a pure no-op, for anything that isn't a
     step-sequenced `PercussionTrack` clip), called from both places now.
     CC91's own ordinary "move the pattern-editor cursor up a row"
     meaning still fires on a plain tap - deferred to release rather than
     press (`DeviceState::row_up_shift_held`/`row_up_shift_combined`), so
     a press that turns out to combine with a pad never has to be undone;
     press/release tracking needed factoring the button-dispatch pipeline
     in `TerminalUI::handleLaunchpadButtonEvent()` into a small reusable
     `dispatch_named_command` lambda, reachable from both the ordinary
     press-driven call site and CC91's own deferred-release one. Works
     the same regardless of Record Arm/the track's own armed state - a
     genuinely different physical gesture (button combo), not competing
     with whatever a plain press on the same pad already means while
     armed, unlike the note-based per-track record-arm mechanism above.

     Closing it is reachable two ways: the same shift+pad combo again
     (only reachable from the plain Session grid, not from the step grid
     a successful open switches every device to
     (`forceNotesModeOnAllDevices()`) - the step grid's own pads mean
     lane/step, not (track, clip index), so this route needs a CC95 press
     back to Session first), or a lone CC95 ("Session") press by itself,
     from any grid mode - Session's own button doubles as "leave the
     sequencer entirely" (`Controller::closeDrumClipFocus()`,
     `LaunchpadManager::handleRawButton()`'s own CC95 case), a simpler,
     more direct route added after the first cut shipped only the
     shift+pad-again route and the user asked for a one-press close.
     `closeDrumClipFocus()` closes whatever's focused without needing to
     already know which (track_id, clip_index) opened it (unlike
     `toggleDrumClipFocus()`), and is a pure no-op when nothing's
     focused; CC95's own existing mixer-submode-toggle check
     (`at_plain_session_grid`) is computed *before* calling it, since
     closing can itself flip every device's `grid_mode` to `SESSION` as a
     side effect (`forceSessionModeOnAllDevices()`), which read after the
     fact would misread "just closed the sequencer" as "already at the
     plain grid" and spuriously flip the mixer submode on a press that
     was only ever meant to leave the sequencer.

     Also fixed while wiring this up: the step grid's own note audition
     (`handleStepGridPadEvent()`'s own `suppress` check) now always plays
     for setting a fresh step - immediate feedback for what was just
     added, regardless of whether the song is playing or the free-running
     audition clock is about to hit this same lane/step for real in a
     moment anyway (an earlier version of this fix suppressed it whenever
     either was true, to avoid a second, musically-arbitrary-timed hit on
     top of the real one - dropped once it turned out the audition clock
     runs almost continuously while just stopped and editing, silencing
     this note-on-set feedback in the overwhelmingly common case) - and
     never for clearing one, which has nothing left to want to hear.

     Both halves of the shift+pad combo light full bright white while
     held, *before* release ever commits anything - CC91 itself (dim
     white beforehand, in every `GridMode` but the step grid, since it
     has a real meaning everywhere else - dark instead while the step
     grid is actually showing, see the unused-button-dimming bullet
     below) and whichever pad it's currently combined with
     (`DeviceState::row_up_shift_pending_pad`) - so a performer sees the
     pair confirmed before ever releasing (`LaunchpadManager::
     refreshLeds()`'s own `GridMode::SESSION` branch).

     Covered by `tools/e2e/verify_launchpad_shift_stepgrid.py`, verified
     through the terminal `SessionView` widget's own text (the "*" focus
     marker `SessionView.cpp` already draws) - this gesture never touches
     real audio/ALSA capture at all (pure Song/Controller state), so it
     doesn't hit the sandboxed-environment LED-read flakiness documented
     for `verify_launchpad_stopclip.py` (`docs/known_bugs.md`) the way a
     `SampleTrack`'s own record-arm gesture does. The LED-highlight-while-
     held behavior has its own dedicated script,
     `tools/e2e/verify_launchpad_shift_highlight.py` - logically the same
     kind of no-real-audio, should-be-reliable check, but it **currently
     fails every check past the first CC91 press** in this sandboxed
     environment, a fresh instance of that same stall class with no real
     audio/ALSA capture involved at all this time (confirmed the state
     changes themselves are correct regardless - `verify_launchpad_shift_
     stepgrid.py`'s own text-based check, reading well after a settle
     rather than a near-real-time LED snapshot, still passes reliably for
     the identical underlying gesture); see `docs/known_bugs.md`. The
     step audition fix has no dedicated e2e coverage at all - there's no
     terminal-visible signal for "a sound did or didn't play," and the
     step grid's own existing LED-based e2e coverage
     (`verify_launchpad_stepseq.py`) already hits that same documented
     sandboxed-environment flakiness independent of this change.
   - **Unused buttons dim while the step grid is showing - shipped, on
     direct user report.** Several extra buttons kept their usual
     out-of-Session color even though pressing them did nothing at all
     once a clip's own step grid was open: move-row-up (CC91, both its
     shift-combo and plain fallback) and move-row-down (CC92) are
     Session-only; Note (CC96) is a true no-op since the device is
     already forced into `NOTES`; Capture MIDI/Draw's (CC98) idle/DRAW-
     eligible color offered a mode switch that would only abandon step
     editing. All four now go fully dark specifically while
     `DeviceState::show_step_grid` is true - CC98's own armed/red
     indicator stays lit regardless, since that's real track-global
     recording state worth keeping visible, not a per-mode affordance.
     prev-track/next-track (CC93/94), already repurposed to page a clip
     longer than 8 steps, needed a subtler rule instead of just going
     dark: lit only when paging is actually useful, i.e. the clip's own
     page count (`DeviceState::drum_edit_page_count`, tracked alongside
     `show_step_grid`) exceeds however many devices are currently
     connected - `resetStepGridView()`'s own device-order split already
     gives every page its own device to show at once below that
     threshold, so a press there would just re-clamp back to where it
     already is.

     A real, related bug surfaced while working through this: pressing
     prev-track/next-track only ever updated the *pressed* device's own
     `DeviceState::drum_edit_page`, letting several connected Launchpads
     drift apart (including onto the exact same page) instead of staying
     split across the clip the way `resetStepGridView()`'s own tiling
     intends. Fixed so every connected device pages together, in
     lockstep, off a single press - `handleCommand()`'s own "next-track"/
     "prev-track" branch now shifts a shared base (derived from the first
     ready device's own current page) and reassigns every ready device's
     page from it, rather than touching only the one that was pressed.

     Covered by `tools/e2e/verify_launchpad_paging_lockstep.py` (two
     simulated devices, a 4-page clip - confirms the device-order split
     and that a single press advances both devices by exactly one page)
     and exercised incidentally by `verify_launchpad_shift_stepgrid.py`'s
     own real LED dumps, which directly confirmed 91/92/96/98 dark and
     93/94 lit at the expected color once the fix landed. Neither script
     involves real audio/ALSA capture, so neither is expected to hit the
     sandboxed-environment flakiness documented above and below - both
     pass reliably.
   - **A lane-less `PercussionTrack`'s own clip opens too - shipped, on
     direct user report, after a rejected first attempt.** Opening a clip
     via shift+pad (or the terminal's own "toggle-record-arm") used to be
     a silent, unexplained no-op whenever the target track had no lanes
     yet - `Controller::toggleDrumClipFocus()` declined outright there
     (`isStepSequenced()` gated it). A first fix routed that case to the
     track's own lane picker instead (`GridMode::CUSTOM`, the same place
     CC97's own press reaches) - rejected once implemented and tested: a
     performer reaching for "open editing" should land on the editing
     surface itself, not somewhere else that merely relates to drum
     tracks. The actual fix drops the lane-count gate entirely -
     `toggleDrumClipFocus()` now opens a lane-less `PercussionTrack`'s
     clip exactly the same way a step-sequenced one's, and its step grid
     (`DeviceState::show_step_grid`, no longer requiring
     `!drum_lane_notes.empty()` either) just shows completely empty (every
     pad black - the empty-lane-list case of the same "past the track's
     actual lane count" rendering every step grid already has) until a
     lane exists. `handlePadEvent()`'s own step-grid dispatch dropped the
     same gate - a press on the empty grid reaches
     `handleStepGridPadEvent()` regardless, which already bounds-checks
     `y` against however many lanes actually exist and safely no-ops
     rather than falling through to free-drumming underneath a grid that
     looks like it isn't one. Reaches both entry points uniformly (the
     Launchpad gesture and the terminal's own "toggle-record-arm"), since
     both funnel through the same shared function.
     `ControllerTests.cpp`'s `toggle_record_arm_on_a_lane_less_
     percussion_clip_arms_normally`/`toggle_drum_clip_focus_is_a_no_op_
     off_a_step_sequenced_percussion_clip` encoded the old behavior and
     were rewritten to match (renamed to `..._focuses_it_too`/`..._off_a_
     non_percussion_clip`). Covered by
     `tools/e2e/verify_launchpad_shift_no_lanes.py` - hits the identical
     CC91-held stall class documented above, in two different shapes
     across separate runs (no LED dump at all after the pad's own
     release, or a later unexplained reconnect with no `PORT_EXIT`/
     disconnect ever logged wiping the device's own state back to
     defaults); verified correct via temporary in-process debug logging
     instead (`grid_mode` becoming `NOTES`, `show_step_grid` becoming
     true, with zero lanes, immediately after the pad's own release -
     since removed) - see `docs/known_bugs.md`.
   - **The step sequencer generalized to pitched tracks - shipped.** Used
     to be `PercussionTrack`-only: its lanes are each keyed to one
     specific GM drum note (`PercussionTrack::getLaneNotes()`/
     `addLane()`/`removeLane()`, picked via the CC97 lane picker's own
     free-drumming-layout-as-picker-surface) - a pitched `InstrumentTrack`
     has no equivalent notion of a small, discrete kit of nameable sounds
     to pick lanes from; it has a whole tuning space instead.

     **Settled design** (direct user answers, not derived): lanes for a
     pitched track are *not* a per-track manually-assigned pick the way a
     drum kit's own lanes are (the two options actually considered - a
     manual picker mirroring CC97's own, or deriving lanes from whatever's
     currently playing - were both turned down in favor of this). Instead,
     a new song-wide `Song::getScale()`/`setScale()` (`Scale` enum -
     `MAJOR`/`MINOR`/`MICROTONAL_A`/`MICROTONAL_B`, `src/model/Scale.h`)
     drives every pitched track's own step grid alike - no per-track lane
     list, no `kMaxLanes` ceiling to reconsider (there's no lane list left
     to cap - the user's own answer to "does the 8-lane ceiling still make
     sense" was "not applicable, as there are no lanes"). `Scale::NONE`
     (the default - no scale ever chosen) falls back to the plain
     chromatic scale rather than an empty lane list, per direct
     instruction. `MICROTONAL_A`/`_B` are two specific scales given
     directly, relative to C: `C D♯ E F G A A♯` and
     `C E𝄫 E♭ F G A♭ B𝄫` - MICROTONAL_B's own E𝄫/E♭ pair is the whole
     reason these are called *micro*tonal: the two are only genuinely
     distinct pitches once the tuning has finer-than-semitone resolution
     (31/53-EDO); in 12-EDO E𝄫 degrades to (sounds identical to) D, but
     still resolves to a real, correct value rather than asserting or
     crashing.

     **Mechanism:** each scale's own degrees are stored as plain note
     names in `Note::stringToKey()`'s own syntax (`Scale.h`'s
     `scaleDegreeNames()`), not decoded into semitone numbers - resolved
     fresh against whichever tuning is actually active
     (`Song::getScaleDegrees()`: each degree's own interval from C is
     `Note::stringToKey(tuning, name) - Note::stringToKey(tuning, "C")`,
     added to the song's own tonic pitch class extracted from
     `Song::getKey()` the same way `LaunchpadManager::resolveNote()`
     already did), so the identical degree list is correct under every
     supported tuning without hardcoding a separate interval set per one -
     cross-checked directly against `docs/31edo_note_numbers.txt`'s own
     published table for MICROTONAL_B (`ScaleTests.cpp`'s own
     `scale_microtonal_b_31edo`). This exposed two real, pre-existing gaps
     in `Note::stringToKey()` itself, both fixed alongside this feature
     (`Note.h`): TET12's own accidental parsing had no double-sharp/
     double-flat case at all (needed for MICROTONAL_B's own E𝄫/B𝄫 there);
     TET19's own branch was an outright `assert(0)` stub, no note-name
     parsing implemented at all (the *display* direction, `keyToString()`,
     already worked - only *parsing* was ever missing) - filled in
     directly from `docs/19edo_note_numbers.txt`'s own published table (a
     W-W-H-W-W-W-H pattern, W=3 steps/H=2, per-letter offsets spelled out
     directly since 19-EDO's own H falls mid-group unlike TET12/31/53's
     uniform "+N per natural-letter run" shape).

     `Song::getScaleDegrees()` returns up to 8 ascending offsets from the
     tonic (register-agnostic - a bare `Song` has no notion of "which
     Launchpad octave"), not full note values, and deliberately never
     wrapped back into a single octave's own pitch-class range (see the
     later "only 7 rows" follow-up below for why);
     `LaunchpadManager::resolveStepGridLaneNotes()`
     (a new shared helper, used by both `handlePadEvent()`'s own
     step-grid press dispatch and `refresh()`'s identical LED-rendering
     resolution, so the two can never disagree) is what shifts each into
     a real absolute value - the exact same register formula
     `resolveNote()`'s own `base_note` already uses
     (`(octave(device_id)+1)*edoStepsFor(tuning)`), so a step placed here
     and a note played on the ordinary isomorphic grid at the same octave
     land on the identical value. `PercussionTrack::getHitNotesAtRow()`'s
     own by-value "was_hit" scan is untouched (still used by its own
     existing callers) - a new, generalized sibling,
     `getHitLaneValues(pattern, effective_row, lane_values)`
     (`ArrangementOps.h`), does the identical by-value scan against an
     arbitrary lane-value list, used by both track types uniformly from
     `LaunchpadManager.cpp` now.

     `handleStepGridPadEvent()` itself changed shape only slightly - it
     already took nothing but a resolved lane-value list plus which row
     (`y`) indexes into it, so generalizing it was just accepting
     `const std::vector<int> &` directly instead of a `PercussionTrack &`
     it only ever called `getLaneNotes()` on. `handlePadEvent()`'s own
     step-grid dispatch gate dropped its `PercussionTrack`-only check the
     same way it already dropped the lane-count one (the "empty sequencer"
     fix above) - a pitched track reaches the identical
     `handleStepGridPadEvent()` now, with its own resolved scale-degree
     lane list. CC97's own lane *picker* stays `PercussionTrack`-only
     deliberately - a pitched track has no per-track lane list to pick
     from at all, the song's own scale is the only thing to change, and
     that has no Launchpad-reachable setter yet (see below).

     **A real bug caught before shipping, not by direct report this
     time:** broadening `Controller::toggleDrumClipFocus()` to accept a
     pitched `InstrumentTrack` also broadened *every* caller of it,
     "toggle-record-arm" included - which would have silently preempted
     multi-track Session View recording (item 1 above) on *any* pitched
     track's own Session-view-focused clip, turning every such Record Arm
     press into "open the step grid" instead of arming it, regardless of
     whether a performer actually wanted to step-sequence it. Caught by
     `ControllerTests.cpp`'s own pre-existing regression coverage for
     plain Record Arm (`toggle_record_arm_on_a_non_drum_machine_track_
     arms_normally`) failing once `toggleDrumClipFocus()`'s own gate
     changed. Fixed by keeping "toggle-record-arm" itself
     `PercussionTrack`-only (a track-type check added right at that one
     call site, `Controller.cpp`) - Record Arm on a pitched track already
     has a real, heavily-used meaning of its own to protect, unlike a
     `PercussionTrack`'s, where this repurposing was always the *only*
     thing a Record Arm press there could sensibly mean. Only the
     Launchpad's own shift+pad gesture reaches a pitched track's step
     grid now; `PercussionTrack` remains reachable both ways, unchanged.

     **Not done:** no interactive way to set `Song::getScale()`/`setKey()`
     from inside the app at all yet - XML-authored only
     (`<song scale="major">`, alongside the pre-existing `key`/
     `temperament` attributes), the same gap `set-song-key`/
     `set-song-tuning` (`Controller.cpp`, menu-visible placeholder stub
     commands, `TerminalMenu`'s own Song section) already document for
     `getKey()`/`getTuning()` themselves - a scale picker is exactly the
     same kind of not-yet-scheduled work, deliberately not tackled here
     either.

     Covered by `tests/ScaleTests.cpp` (the model-level degree resolution
     - chromatic fallback, each of the four scales in 12-EDO, key
     transposition, the 31-EDO microtonal cross-check against
     `docs/31edo_note_numbers.txt`, and the XML round trip) and
     `tools/e2e/verify_launchpad_shift_stepgrid_pitched.py` (the
     Launchpad-side open/close mechanic, same "*" focus-marker
     verification as its `PercussionTrack` sibling script above, no real
     audio/ALSA capture involved so it passes reliably) - the actual
     resolved lane values themselves (register-shifted absolute notes,
     not just pitch classes) were confirmed correct end to end via
     temporary in-process debug logging against that same fixture (a
     12-EDO major scale keyed to C4 resolving to `60 62 64 65 67 69 71` -
     exactly C D E F G A B - since removed) rather than a dedicated e2e
     assertion, the same reasoning `verify_launchpad_shift_no_lanes.py`
     above already established for LED-based content checks in this
     sandbox.

     **A follow-up, found by direct user report and fixed:** the step
     grid only ever showed one fixed 8-row window into a scale/chromatic
     run - fine for the four named scales (exactly 7 degrees, already
     fitting with room to spare) but leaving no way to reach a second
     octave, or any of a chromatic run's own remaining degrees beyond the
     first 8, at all. Fixed by repurposing move-row-up/move-row-down
     (CC91/92, the step grid's own "up"/"down" arrows) into
     `octaveUp()`/`octaveDown()` while the grid is showing - the same
     repurposing prev-track/next-track (CC93/94, "left"/"right") already
     get for paging through steps, just shifting *which* octave the whole
     lane set resolves at instead of which 8-step window of the clip
     shows (`resolveStepGridLaneNotes()`'s own comment) - so between the
     two pairs, everything a scale/chromatic run has is reachable, not
     just the one octave/one page that happened to be current when the
     clip was opened. All four of 91-94 now light the same white rather
     than 93/94's own former blue, reading as one family once a clip's
     open. A second, related report followed immediately: reopening a
     clip could start at an arbitrary, seemingly random octave, since a
     device's own `octave_offset` simply carried over from whatever
     unrelated note entry had last left it at - fixed by having
     `LaunchpadManager::resetDrumEditPaging()` (renamed
     `resetStepGridView()`, since it now resets two independent things,
     not just paging) also zero every connected device's own octave
     register back to 0 on open, alongside its own pre-existing page
     reset - opening the same clip now always starts from the same known
     octave. Verified via temporary in-process debug logging against the
     pitched fixture (change-triggered prints, to survive this sandbox's
     own terminal-escape-interleaving corruption of repeated identical
     lines): confirmed the grid opened at `octave=4 register_base=60`
     (the plain global octave, C4) *even after* two octave-up presses
     sent before ever opening the clip - proving the reset actually wins
     over whatever was there before - and a further CC91 press once
     inside correctly advanced it to `octave=5 register_base=72`, exactly
     one octave up (since removed).

     **A third report followed, on the same feature:** "Up and down only
     scrolls the single Launchpad" - the CC91/92 octave-shift above had
     only ever called `octaveUp()`/`octaveDown()` on the one device that
     was actually pressed, unlike CC93/94's own page-shift, which was
     already lockstepped across every connected device. Fixed by looping
     the octave-shift branch in `LaunchpadManager::handleCommand()` over
     `launchpad_io_->readySessionIds()` the same way CC93/94's own page
     advance already does, applying the shift to every connected device
     from a single press on any one of them - since `resetStepGridView()`
     always starts every device at the identical `octave_offset = 0`,
     there's no per-device relative offset to preserve the way page-shift
     has to preserve each device's own initial split, so the fix is a
     plain uniform loop rather than needing CC93/94's own more careful
     offset-preserving logic. Verified via a new 2-device scratch debug
     test (temporary in-process logging, since removed): a single CC91
     press sent from only device A produced `octave shift: 2 devices,
     dev0_offset=1 dev1_offset=1` - both devices advancing together from
     one press.

     **A fourth report followed, still on the same feature:** "Still
     doesn't start playing from the beginning when opening the
     sequencer" - a follow-up making clear the second report's own fix
     (zeroing `octave_offset` on open) addressed pitch-register
     predictability only, not audition *playback* position. Root cause:
     the free-running `audition_clock_` driving auditioned
     playback/playhead was never actually restarted when a clip opened -
     only Session-view's own trigger paths called
     `restartAuditionClockFromSilence()`, so a freshly opened step grid's
     playhead resumed from wherever that shared clock's stale phase
     already happened to be, not row 0. Fixed by having
     `resetStepGridView()` call `restartAuditionClockFromSilence()` too,
     alongside its existing page/octave resets - three resets on every
     clip-open now, not two. Verified via temporary in-process debug
     logging (a change-triggered print inside `refresh()`'s playhead
     computation, since removed): reopening the pitched fixture's clip
     showed `audition_step=0 drum_playhead_step=0` immediately, with
     natural forward progression afterward, confirming playback now
     starts from the clip's own beginning every time.

     **A fifth report followed, on the pitched step grid specifically:**
     "I don't like the fact that only 7 rows are used by the sequencer" -
     every named scale (`Scale.h`'s `scaleDegreeNames()`) has exactly 7
     degrees, one short of the step grid's own fixed 8 rows, so a pitched
     track's own clip always left its 8th row completely unused. Root
     cause was `Song::getScaleDegrees()`'s own wraparound: it modded every
     degree back into `[0, edoStepsFor(tuning))`, so a scale's own upper
     degrees (once transposed to a nonzero tonic) could fold back *below*
     the tonic instead of climbing past the octave boundary, and it never
     filled a row beyond however many names the scale actually had. Fixed
     by dropping the modulo entirely - `getScaleDegrees()` now returns a
     strictly ascending run of up to 8 offsets from the tonic, crossing
     the octave boundary rather than wrapping back below it wherever a
     transposed degree naturally lands above it, and fills any row past
     the named list's own length by cycling back through the same names
     one octave (`edoStepsFor(tuning)`) higher each wrap - so a 7-note
     scale's own 8th and final row is always its tonic repeated an octave
     up (e.g. C major now shows C D E F G A B C across all 8 rows, not C D
     E F G A B and a dead 8th row). `resolveStepGridLaneNotes()` itself is
     untouched - it already just added a fixed register offset to
     whatever `getScaleDegrees()` returned, so the ascending, octave-
     crossing values it now receives resolve to correct absolute notes
     with no further change. `ScaleTests.cpp`'s own degree-resolution
     tests were updated to the new 8-entry, non-wrapped expectations
     (including the D-major transposition case, whose own last two rows -
     C# then D - now correctly ascend past the octave boundary instead of
     wrapping back down to a pitch class below the tonic); the full suite,
     including the 31-EDO microtonal cross-check against
     `docs/31edo_note_numbers.txt`, was re-run and passes with the new
     values.

     **A sixth request followed, asking for a UX change rather than
     reporting a defect:** "Make the scroll step size (both directions) 4
     rows/cols so it is easier to follow" - both scroll axes (move-row-up/
     -down and prev-track/next-track while a step grid shows) jumped a
     full 8-wide window per press, so consecutive windows never overlapped
     and a performer had nothing to visually anchor a press against. Two
     separate design questions came up asking for clarification (both
     answered directly by the requester rather than assumed): whether the
     column (step/time) scroll should move by 4 steps (yes), and - the
     harder one - whether the row (pitch) scroll on a pitched track should
     keep jumping whole octaves (an already-documented invariant: a step
     placed here matches the exact absolute value a note played on the
     ordinary isomorphic grid at this device's own current octave would)
     or move by literally 4 rows, decoupled from octave entirely. The
     requester chose full decoupling, explicitly noting the invariant
     should not constrain this: "Make sure the up and down arrows are not
     labeled octave up/down, as they are just scroll up and scroll down,
     in this case. It is possible, that we have scales in the future that
     are not tied to octave at all, or span two octaves. Or more likely,
     we have scales that have more than 7 notes."

     **Column scroll:** `DeviceState::drum_edit_page` was renamed
     `drum_edit_step_offset` and its own unit changed from a page index
     (implicitly ×8) to a raw step offset, so it can land on any step, not
     just an 8-step boundary. `handleCommand()`'s own prev-track/
     next-track branch now advances it by `kStepGridScrollStep` (4, a new
     shared constant - see below) instead of a full page, clamped to
     `[0, max(0, clip_length - num_devices*8)]` - each connected device's
     own window still stays exactly 8 steps wide and offset from its
     neighbors by a full page (`new_base + i*8`), only the *scroll
     increment* between presses changed, not the per-device tiling itself.
     `resetStepGridView()`'s own default split (`i*8` per device, in
     connection order) is unchanged. The pad-press dispatch and
     `refresh()`'s own LED-rendering resolution both switched from
     `page*8 + x`/`page*8 + step` to `offset + x`/`offset + step` directly
     - a one-line change at each site, since the underlying row math never
     actually cared about "pages," only about a start-of-window value.
     `DeviceState::drum_edit_page_count` (a page count, compared against
     however many devices were connected to decide the dark/lit no-op
     state) was replaced with `drum_edit_max_step_offset` (the actual
     highest offset still worth scrolling to, `max(0, clip_length -
     num_devices*8)`), computed once in `refresh()` and read directly by
     `refreshLeds()` as `> 0` - simpler than reconstructing the same
     "would this be a no-op" comparison as a second, separate calculation.

     **Row scroll, the bigger change:** rather than reusing `octaveUp()`/
     `octaveDown()` (which mutate `DeviceState::octave_offset`, the same
     register the ordinary isomorphic grid's own note-entry uses),
     move-row-up/-down while a *pitched* track's step grid shows now
     mutate a new, wholly independent field, `DeviceState::
     drum_edit_row_offset`, by `kStepGridScrollStep` rows at a time - a
     scale-degree index, not a register, fed straight into a new, more
     general `Song::getScaleDegreesWindow(start_index, count)` (`Song.h`/
     `.cpp`) that `getScaleDegrees()` itself is now just a thin wrapper
     over (`getScaleDegreesWindow(0, 8)`, preserving every existing
     caller/test unchanged). `getScaleDegreesWindow()` generalizes the
     "cycle back through the degree list one octave higher every wrap"
     logic item 1 above already established to an *arbitrary* starting
     index, including negative ones (floor division, not truncation, so
     index -1 correctly resolves to the scale's own last degree one octave
     down rather than clamping or wrapping the wrong way) - two new tests,
     `scale_degrees_window_positive_start`/`_negative_start`, cover a
     mid-scale window and a below-the-tonic one respectively.
     `resolveStepGridLaneNotes()` changed from `song.getScaleDegrees()` to
     `song.getScaleDegreesWindow(row_offset, 8)`, `row_offset` read off
     this device's own `DeviceState::drum_edit_row_offset` (default 0,
     giving byte-identical results to before at the default position) -
     the register anchor (`octave(device_id)`-derived) is untouched, only
     which 8-entry slice of the extended ascending run gets added to it.
     A `PercussionTrack`'s own step grid is unaffected either way - its
     lanes are a small, fixed, manually-curated list with nothing to
     scroll to, so move-row-up/-down there is now a genuine no-op (no
     field mutation at all, just swallowing the press) rather than
     silently nudging an octave register that never did anything visible
     for it anyway. `refreshLeds()`'s own CC91/92 lit state gained exactly
     that distinction (`row_scroll_useful`) - dark for a percussion step
     grid, unconditionally lit for a pitched one (no ceiling to go dark
     for, same as before). `resetStepGridView()` gained a fourth reset
     (`drum_edit_row_offset = 0` alongside its existing step-offset/
     octave/audition-clock resets) so a freshly opened clip always starts
     at the tonic, not wherever a previous scroll session left the row
     window.

     `kStepGridScrollStep` (a new `constexpr int = 4` in
     `LaunchpadManager.cpp`'s own anonymous namespace, next to
     `kMixerHoldPreviewThreshold`) is the one shared constant both axes'
     scroll increment reads from, so the two stay in step with each other
     if this value is ever revisited. Every doc comment this touched
     (`DeviceState::drum_edit_step_offset`/`drum_edit_row_offset`/
     `drum_edit_max_step_offset`, `resolveStepGridLaneNotes()`,
     `resetStepGridView()`, `Song::getScaleDegreesWindow()`/
     `getScaleDegrees()`) was rewritten rather than patched in place, the
     same as every other fix in this section - stale wording describing
     "octave register"/"page" for what's now a plain scroll offset would
     otherwise mislead the next reader. Verified via the full `ScaleTests.cpp`
     suite (including the two new windowed-degree tests) and a clean
     rebuild with zero new compiler warnings (`-Wsign-conversion` and the
     rest); `verify_launchpad_shift_stepgrid.py`/`_pitched.py` (the two
     e2e scripts that exercise opening a step grid at all) both still pass
     3/3 unchanged. `verify_launchpad_paging_lockstep.py` was checked too,
     out of caution since it directly exercises the renamed step-offset
     field - it fails in this sandboxed environment, but identically so
     when checked against the last commit *before* this entire session's
     own changes (via a temporary `git stash`), confirming the failure
     predates this work entirely rather than being a regression from it;
     left unresolved as a pre-existing environmental issue, the same class
     `docs/known_bugs.md` already documents for other multi-device
     scripts here.

3. **No delay field on `Command`.** Unlike `Note::delay`, a `Command` has
   no sub-row timing offset - automation can only ever be recorded/placed
   quantized to a whole row. A permanent resolution ceiling, independent
   of item 1 above.
