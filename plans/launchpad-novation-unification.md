# Launchpad Mixer Mode: closing the gap with Novation's own Session mode

## Shipped

- Session pad flash/pulse for playing/queued clips, matching Novation's
  own published guide (`LaunchpadManager::SessionPadHighlight`).
- Fader velocity-sensitive glide + micro-values (`FaderState`,
  `applyFaderPress()`/`tickFaderRamps()`) - Pan only now (see below for
  why Send Main/A/B moved off this).
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
  longer interpolates them locally at all (`tickFaderRamps()` only still
  ticks Pan; a Send press instead resolves its target once and sends it,
  with the same velocity-derived duration as before, in a single event -
  `Controller::glideTrackSendA()`/`B()`/`Main()`, new
  `PlaybackControlEvent::GLIDE_TRACK_SEND_*` types). The engine glides it
  (`dsp::ValueRamp`, `LeafTrackState::glideSendA()`/`advanceSendRamps()`,
  advanced once per render chunk, reaching already-sounding voices the
  same way an instant set always did). Since the model no longer gets
  ticked through every intermediate value the way the old client-side
  ramp incidentally kept it in sync, it's now kept honest a different
  way: the engine reports its own real, possibly-mid-glide value back
  every snapshot (`TrackInfo::getLiveSendMain()`/`A()`/`B()`,
  `Controller::syncLiveSendsIntoModel()`, called from
  `receivePlaybackSnapshot()`) - so `LaunchpadManager`'s own LED refresh
  needed no change at all, still just reading the model. Automation
  recording is unaffected - `recordFaderAutomationIfArmed()` still only
  ever needs the resolved target at press time, which is still known
  immediately regardless of how the value is later reached. Covered by
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
  shared by `applyFaderPress()`/`resolveSendFaderTarget()`) compressing
  the top of the velocity range so a realistic hard press already reaches
  most of the way to full speed, while a genuinely soft press still
  glides meaningfully slower.

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
   commands only.

   **Decided: recording needs a new command, not a reuse of the existing
   tick-based slides (`0Hxx`/`0Kxx`).** Those move by a fixed magnitude
   per tick, `TICKS_PER_ROW` ticks per row - and a row's own real-time
   duration is tempo-dependent (`getRowDuration(tempo)`), so a slide
   command's own traversal time stretches or shrinks with the song's
   tempo. That's the wrong shape regardless of tempo: a live fader
   press's own velocity-to-speed relationship is confirmed (Novation's
   own guide - "Faders are velocity sensitive... hitting harder causes
   the value to move more quickly") to be a physical-gesture thing, nothing
   to do with tempo, and this codebase's own engine-side glide
   (`LeafTrackState::glideSendA()`/etc., shipped above) is deliberately
   real-time/frame-based for exactly that reason. So a command meant to
   record and later reproduce that same feel needs its own real-time
   duration (or rate) field - a genuinely new mnemonic, not
   `0Hxx`/`0Kxx`'s per-tick one. Still open: its actual letter (the same
   Renoise-precedent check every other letter here got) and its exact
   encoding (a two-hex-digit target plus a separate rate byte needs more
   than the current 4-character/2-argument-digit shape allows in one
   command - possibly two hex digits each for target and rate, trading
   away `0Lxx`/etc.'s own full 8-bit target resolution, or some other
   split not worked out yet). Doesn't need to cover a micro-value tap at
   all - those apply instantly, no glide involved, so recording one can
   keep using the plain `0Lxx`/`0Fxx`/`0Mxx` Set command exactly as today.

   Once that exists, `recordFaderAutomationIfArmed()`'s own callers
   (`resolveSendFaderTarget()`, which already computes the exact
   velocity-derived `out_duration_seconds` a fresh press means - currently
   discarded, only the target reaches the recorded `Command` today) would
   thread that duration into the new command instead. Playback then
   reproduces the *actual* recorded glide directly from it - simpler, and
   more faithful, than the row-distance-guessing scan originally sketched
   here (scan forward for the next same-type command and interpolate the
   full row-span): with a real duration recorded, playback just needs
   `dsp::ValueRamp` (the *live* fader glide's own engine-side primitive,
   shipped above, already proven to interpolate in dB, not linear gain -
   see `LeafTrackState.h`'s own comment on why that distinction matters)
   started at the command's own row for that many frames, consumed by
   `InstrumentTrackState::render()`'s chunked loop the same way `0Hxx`/
   `0Kxx`'s own per-tick queue already is generalized to three parallel
   Send queues in `RenderContext`. The row-distance heuristic remains a
   fallback worth keeping in mind only for a hand-typed pair of `0Lxx`
   rows with no recorded duration between them, not the primary mechanism.

3. **No delay field on `Command`.** Unlike `Note::delay`, a `Command` has
   no sub-row timing offset - automation can only ever be recorded/placed
   quantized to a whole row. A permanent resolution ceiling, independent
   of items 1/2 above.
