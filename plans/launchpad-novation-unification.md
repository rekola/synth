# Launchpad Mixer Mode: closing the gap with Novation's own Session mode

## Shipped

- Session pad flash/pulse for playing/queued clips, matching Novation's
  own published guide (`LaunchpadManager::SessionPadHighlight`).
- Fader velocity-sensitive glide + micro-values (`FaderState`,
  `applyFaderPress()`/`tickFaderRamps()`).
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

## Remaining

1. **Multi-track Record Arm overlay - postponed.** The track-picker
   overlay was built to be reusable for this (`DeviceState::
   TrackPickerPurpose::RECORD_ARM`), but `capture_enabled_` is a single
   song-wide flag today, not per-track - needs its own design pass on
   that data shape (what "armed" means with several tracks armed at
   once) before the overlay's third purpose can just reuse the
   Stop/Mute/Solo plumbing. Postponed twice by explicit direction.

2. **Playback-time interpolation between sparse recorded automation -
   its own future phase, not started.** Recorded `0Lxx`/`0Fxx`/`0Mxx`
   commands are row-quantized; played back today they step/jump at each
   command's own row rather than reproducing the glide that was actually
   performed. Matters for Volume/Send (an instant gain step is audible)
   but not Pan (an instant reposition isn't), so scoped to those three
   commands only. Sketch: generalize `RenderContext`'s existing
   `pending_azimuth_ticks_` queue mechanism (`0Hxx`/`0Kxx`'s own per-tick
   interpolation) into three parallel Send queues, consumed by
   `InstrumentTrackState::render()`'s chunked loop the same way; add a
   `SongState.h` forward-lookahead scan for the next same-type command to
   interpolate toward (no existing precedent - `0Hxx`/`0Kxx` spread a
   fixed one-row slide instead).

   Once built, unify Launchpad's own *live* fader glide onto the same
   engine-side mechanism too, rather than keeping Launchpad's own
   real-time timer ramp (`tickFaderRamps()`) as a second, separate
   implementation - a press should tell the engine once (target + rate)
   and let the engine carry the glide forward, live or played back,
   through one shared code path. Needs a live (not pattern-row) entry
   point into that mechanism, and a new `Command` carrying both target
   and rate as an `xy` pair (like the already-planned `0Txy` Tremolo)
   scoped specifically to what a Launchpad fader press can express - open
   questions: its mnemonic letter (needs the same Renoise-precedent check
   every other letter here got), and how a micro-value press (up to ~32
   distinct positions, more than a 16-step nibble addresses, and applied
   instantly with no glide at all) fits in - possibly it just keeps using
   the plain, instant `0Lxx`/`0Fxx`/`0Mxx` Set command instead of this new
   one. Not decided.

3. **No delay field on `Command`.** Unlike `Note::delay`, a `Command` has
   no sub-row timing offset - automation can only ever be recorded/placed
   quantized to a whole row. A permanent resolution ceiling, independent
   of items 1/2 above.
