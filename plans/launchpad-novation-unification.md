# Launchpad Mixer Mode: closing the gap with Novation's own Session mode

## Context

Comparing our Session view/Mixer submode implementation against
Novation's own published Launchpad X user guide ("Using Launchpad X's
Session mode", `userguides.novationmusic.com`, saved locally after the
live site 403'd a fetch) turned up that our design - arrived at
independently, not copied from the guide - already tracks Novation's own
architecture closely: the mixer submode radio group over the same seven
buttons, Session button green/orange, Stop/Mute/Solo cycling one shared
overlay. The comparison validated the existing design rather than
exposing a wrong one. One real, already-shipped gap it did surface: a
playing/queued clip pad now pulses/flashes green (hardware-animated,
matching the guide's own described behavior) instead of a brighter/
dimmer shade of its own identity color - see `LaunchpadManager::
SessionPadHighlight`.

This file tracks what's left.

## Shipped

**Fader velocity-sensitive speed + micro-values.** A press now glides the
pressed track's own value toward that row's canonical one at a rate
scaled by press velocity (`LaunchpadManager::applyFaderPress()`/
`tickFaderRamps()`, `FaderState`) rather than snapping instantly, ticked
forward by real elapsed time each `refresh()` call - `TerminalUI`'s main
loop shortens its own poll wait while any ramp is in flight
(`hasActiveFaderRamp()`) the same way it already does for the Escape-chord
indicator's delayed appearance, since nothing else would otherwise wake it
often enough for a smooth glide. Repressing the exact row *last pressed*
on that fader (not merely a row the live value coincidentally already
resolves to - `FaderState::touched`/`last_pressed_row` deliberately don't
conflate those two) instead cycles 4 finer micro-values between that row
and the next, shown via that row's own pad brightness
(`LAUNCHPAD_FADER_MICRO_MIN_SCALE`). The open question this file's
original draft flagged (does a second press mid-ramp retarget
immediately, or does the current ramp finish first) was resolved as
"retarget immediately" - matching how a real fader responds to a second
touch before the first has settled. Not calibrated against real hardware
(no documented equivalent to compare against) - see `docs/known_bugs.md`
for the one adjacent pre-existing issue this surfaced (a stale e2e
fixture's own row-math, unrelated to this work) and the still-open gap
(no e2e coverage for the Pan/wraps path, Send-only so far).

**Momentary hold-to-preview across mixer functions.** A press that
switches to a genuinely different one of the seven radio-group members
still applies immediately (`toggleGridMode()`/`toggleTrackPicker()`,
unchanged), but now arms a preview (`armMixerHoldPreview()`, snapshotting
whatever was showing right before this press into `DeviceState::
mixer_hold_previous_*`) that `handleMixerFunctionRelease()` resolves on
release: a quick tap (< `kMixerHoldPreviewThreshold`, 600ms - the same
value `handleDrawToggleButton()`'s own tap-vs-long-hold gesture already
uses, declared separately per that file's own "conceptually independent,
tune apart later" convention) leaves the switch standing, sticky; a real
hold reverts back to the snapshotted previous member. Only a press
switching to a *different* member arms this - repressing the one already
active (which closes the group outright) never does, matching the
"nothing to preview-and-revert about turning it off" reasoning. Needed
its own press+release routing in `TerminalUI::handleLaunchpadButtonEvent()`
(`LaunchpadManager::isMixerFunctionButton()`), since `handleRawButton()`
is press-only - the same "CC98 needs both, everything else through it
doesn't" split DRAW's own gesture already established. New e2e coverage:
`verify_launchpad_mixer_hold.py` (hits the same pre-existing sandbox ALSA-
delivery stall as the other Session-adjacent scripts once it presses CC95
a second time - see `docs/known_bugs.md`, confirmed via an unmodified-
checkout control run, not a regression from this).

## Remaining gaps

1. **Multi-track Record Arm overlay - deliberately last.** The track-
   picker overlay was built from the very start to be reusable for
   this, explicitly out of scope back then. Novation's guide confirms
   Record Arm is indeed the group's 8th member, same overlay shape
   (bottom-row-per-track, bright when armed) as Stop/Mute/Solo already
   have. Unlike the shipped fader/hold-to-preview work above, this
   isn't just a display/interaction change: `capture_enabled_` is a
   single song-wide flag today, not per-track, so multi-track arm is a
   real semantic change to what "armed" even means (which tracks
   receive live input at once), not only new UI wired onto existing
   state - needs its own design pass on that data shape before the
   overlay's third purpose (`DeviceState::TrackPickerPurpose::
   RECORD_ARM`) can just reuse the Stop/Mute/Solo plumbing. Left for
   last on both grounds - real semantic risk, and by explicit
   instruction.

2. **Recording live mixer moves as automation - deliberately last, and
   the biggest item here.** Prompted by a direct comparison: on Ableton,
   moving any automatable parameter (mouse, or a MIDI-mapped controller
   like the Launchpad's own Mixer Mode faders) while Arrangement
   recording is active writes real automation breakpoints at that exact
   moment, so playback reproduces the move itself, not just wherever the
   value ended up. Confirmed we have nothing like this: `Controller::
   setTrackSendA/B/Main()`/`setTrackAzimuth()` (what a mixer-submode
   fader press already calls) only mutate the live `LeafTrack` scalar
   and push one immediate `PlaybackControlEvent` - nothing timestamped
   or row-keyed gets written, so only the value the song was saved with
   survives at all (a separate, narrower gap than the shipped work
   above, which is about the press gesture itself, not what recording
   does with it).

   **Where this has to live, settled up front rather than left as an
   open question**: not in a `Clip`'s own `Pattern` content. A `Clip` is
   shared/reusable across every position it's placed at
   (`ArrangementOps.h`'s `placeClipInstance()` - editing one placement
   edits all of them) - a live automation move happened at one specific
   point in the timeline, so baking it into the clip would replay it at
   every other placement of that same clip too, which is wrong. It has
   to go into the target track's own `Section`-level content instead -
   `Section`'s directly-inline `Pattern` (`patterns_by_track_id_`),
   always an independent copy already, unlike a `Clip`'s shared one, and
   already exactly where `2Lxx`/`2Rxx`'s hand-typed azimuth-slide
   commands live today (`Section::setCommand(row, track_id, ...)`) - not
   a new storage concept, just a new *writer* into one that already
   exists for exactly this "per-position, per-track, not shared" shape.
   This is also why it "solves some open questions about automation":
   `docs/commands.md`'s Planned effect commands and this live-recording
   feature are two views of the same underlying data path, not separate
   features.

   "A suitable track must be selected for the commands" - concretely,
   whichever `LeafTrack` actually owns the parameter being moved (Volume/
   Pan/Send A/Send B are all per-track already), not some separate
   global automation track - so a live mixer move writes into *that*
   track's own row at the section/row the transport is currently at.
   Correcting an over-eager claim from an earlier pass of this file:
   that doesn't automatically coexist with a `Clip` instance placed at
   the same row. `SongState.h`'s own per-row playback loop picks
   `active_pattern` as *either* the resolved `Clip`'s own
   `getLeafPattern()` *or* the section's own `patterns_by_track_id_`
   (`section.getPatternsByTrack()`) - never both - and reads both notes
   and `Command` from whichever one won. So a `Command` recorded into
   the section's own background pattern would currently be silently
   masked out at any row where a real `Clip` instance is also present
   for that track, exactly the collision this file previously claimed
   didn't exist.

   Open questions still genuinely unresolved, not just unbuilt:
   - Does `Clip` need its own `Command`/effect column at all? If a
     `Clip` stayed notes-only and `Command` were always read from the
     section's own background pattern regardless of which pattern
     supplied that row's notes (decoupling the two reads in the loop
     above instead of picking one `active_pattern` for both), the
     masking problem above goes away on its own, and automation
     genuinely becomes independent of whatever `Clip` happens to be
     playing - matching "the clip could be just about notes and the
     background has the command column for automation" directly, not
     as a workaround.
   - Only Pan/azimuth has a slide command (`2Lxx`/`2Rxx`) today - Volume/
     Send A/Send B need their own new command codes before there's
     anything for a recorded move to write into.
   - A row currently carries exactly one `Command` per track
     (`Pattern::setCommand`/`getCommand`, singular) - recording two
     parameters moved in the same pass on the same row/track (e.g. Pan
     and Volume together) has nowhere to both go yet; needs either
     multiple effect slots per row or accepting that simultaneous
     multi-parameter recording drops one, unresolved either way.
   - More generally: what if multiple automation changes land on the
     same row at all - do we actually need more than one command
     column per row/track as a first-class feature (one column per
     concurrently-automatable parameter, closer to a real DAW's
     per-parameter automation lanes), rather than one shared `Command`
     slot reused across every recordable parameter? The two-parameter
     case above is the smallest instance of this, not the whole
     question - genuinely undecided, not just unbuilt.
   - Commands have no delay field the way `Note` does (`Note::delay`/
     `getDelayAsFloat()` - a sub-row fraction; `Command` has nothing
     equivalent). So however this ends up recorded, automation can only
     ever land quantized to a whole row, never at the finer sub-row
     offset a note's own delay allows - a real, permanent resolution
     ceiling on how precisely a recorded move's timing can be captured,
     not something a future command format tweak fixes without also
     adding a delay field to `Command` itself.
   - Note recording already has its own "ensure somewhere to write
     exists" path for clips (`ensureSessionRecordingClip()`); a parallel
     "ensure this section/track has inline pattern rows to write
     automation commands into" is needed too, since that's a different
     target than a clip.

## Suggested order

1 (Record Arm) and 2 (automation recording), in that order - 2 in
particular needs new command codes and a multi-parameter-per-row answer
settled before it's buildable at all, not just a data-model design pass
like item 1.

Delete each bullet (or the whole file) as its own item ships.
