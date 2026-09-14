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
- `YDxy` - azimuth's own equivalent of `YMxy`/etc., and the Launchpad Pan
  branch now actually records it (`recordFaderAutomationIfArmed()`,
  previously a no-op for Pan - see `docs/commands.md`'s Implemented
  table). Deliberately not called `YPxy`/given `0Pxx`'s own encoding:
  `0Pxx`'s `xx` only reaches half the circle (a real, inherited Renoise
  limitation), which would throw away exactly the range a live Pan press
  can reach, so `YDxy`'s own `x` spans the *full* circle instead -
  `Command::isAzimuthGlide()`/`getAzimuthGlideTargetDegrees()`/
  `azimuthGlide()`, `SongState.h`'s own dispatch to
  `LeafTrackState::glideAzimuth()`. Covered by `CommandTests.cpp`
  (parsing/decoding/wrapping) and `SendSetCommandTests.cpp` (a placed
  `YDxy` command starting a real glide, not an instant jump).

## Remaining

1. **Multi-track Record Arm overlay - postponed.** The track-picker
   overlay was built to be reusable for this (`DeviceState::
   TrackPickerPurpose::RECORD_ARM`), but `capture_enabled_` is a single
   song-wide flag today, not per-track - needs its own design pass on
   that data shape (what "armed" means with several tracks armed at
   once) before the overlay's third purpose can just reuse the
   Stop/Mute/Solo plumbing. Postponed twice by explicit direction.
   Open question: overdubbing - if a clip is already playing on a track
   when recording arms on it, do the newly-played notes merge into that
   same clip (overdub - what the Ableton/Launchpad combo does) or does
   arming always start a fresh clip, leaving the one that was playing
   untouched? Not decided yet.

   The Ableton/Launchpad combo's own documented per-pad feedback for this,
   worth matching once the design above is settled: while a track is
   armed, every empty clip slot in its column lights dim red. Pressing one
   flashes it red (queued to record) with the Record button flashing in
   unison; once recording actually starts the pad pulses red and the
   Record button goes solid bright red. Pressing the Record button again
   while recording flashes the pad red to show it's about to stop. If the
   track is unarmed while still recording, that stops immediately instead
   of queuing.

2. **No delay field on `Command`.** Unlike `Note::delay`, a `Command` has
   no sub-row timing offset - automation can only ever be recorded/placed
   quantized to a whole row. A permanent resolution ceiling, independent
   of item 1 above.
