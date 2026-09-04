# SampleTrack clips

Raw-audio clips for tracks (recorded live or loaded from disk), addressable/
placeable/loopable the same way a note Pattern is, with Session-view
launching, waveform display, tempo-matched time-stretch, and
loudness-threshold-triggered recording. Everything below is implemented,
tested, and committed unless a section says otherwise.

## Built

1. **Removed `FileInstrument`** - confirmed dead code, deleted.
2. **Removed the note-launches-audio remnant** - the old non-functional
   `UI::handleRecordEvent()`/recording state, deleted.
3. **Data model** - `SampleTrack`, `Clip` (raw audio + in/out trim points via
   `SampleContent`), mono downmix on load.
4. **Live recording** - mic capture into a `Clip`'s own audio buffer
   (`start-sample-capture`/`stop-sample-capture`).
5. **Playback** - `SampleTrackState::triggerClip()`, scheduled via
   `RenderContext`'s own per-block sample-track events, transport-driven.
6. **Session view / Launchpad triggering** - a placed clip is launchable
   like any other Session-view row (`triggerClipStep()`).
7. **Persistence** - `<clip>` XML + a `<song-stem>.samples/` sidecar WAV per
   clip, orphans swept on save.
8. **UI/editor integration** - `add-sample-track`, a `SampleTrack`'s own
   placeholder column in `PatternEditor`.
9. **Tests** - unit coverage for the model/resampler/round-trip pieces
   above.
10. **Clip-based note recording** - live keyboard/Launchpad note recording
    now also writes into a real, reusable `Clip`
    (`ensureNoteRecordingClip()`), not straight into the scene's own
    background Pattern.
11. **Recording-latency compensation** - `snd_pcm_delay()`-measured
    playback+capture latency trims the lead-in and backdates a take's own
    placement (`armRecordingStart()`/`beginSampleCapture()`).
12. **Waveform rendering** - RMS-per-bucket waveform glyphs in a clip's
    `PatternEditor` box; sextant/quadrant by terminal capability, or
    braille via `M-x toggle-waveform-glyph-style` - kept as a permanent
    side-by-side, no winner picked.
13. **Time-stretching** - SoundTouch-based pitch-preserving stretch when a
    clip's own recorded tempo disagrees with the song's current one.
14. **Threshold-triggered recording** (one of three designs considered) -
    Record Arm starts the transport for real and buffers mic input into a
    pre-roll ring; once loudness crosses a threshold, the clip starts with
    the ring's own content spliced onto its front and its placement
    backdated to match.
15. **`merge-clip-to-background`** (C-x m) - folds a note-based clip's
    placement back into the scene's background Pattern, freeing the clip
    slot without deleting the clip. Introduced `Song::getCurrentTrackId()`
    - a single, `Song`-owned "current track" every UI widget now writes
    through to, so this and other Launchpad-routed commands work
    regardless of which widget has focus.

## Manual testing still needed

- **Mic recording, end to end**: record a take (start, speak/play, stop);
  confirm it becomes a launchable Session-view clip and survives a
  save/reload round-trip. Also: load a stereo file from disk, confirm it
  downmixes to mono and plays back correctly positioned.
- **Hand-edited trim points**: edit a saved clip's `<clip in="..."
  out="...">` attributes directly, reload, confirm playback (transport and
  Session-view) starts/ends exactly where authored, and that a looping
  trimmed clip loops within the trimmed window.
- **Recording-latency compensation (#11), real hardware**: play a song
  with a steady beat, record yourself clapping/tapping along, play it back
  layered against the same backing track - hits should land on-beat, and
  the saved clip's `<clip in="...">` value should be a small, plausible
  round-trip-latency number, not `0` or something absurd.
- **Threshold-triggered recording (#14), real hardware + mic**: arm a
  SampleTrack against an already-playing backing track, wait an arbitrary
  pause, then hit a sharp attack. Confirm the attack isn't chopped, the
  clip lands in sync with the backing track, the VU meter shows real input
  level while armed (not just once a clip exists), Record Arm again
  mid-take finishes the capture, and pressing it again while merely armed
  cancels cleanly.

## Future direction (not built): clip drafts + merge-to-background lifecycle

The clip-slot-scarcity problem: once every recording becomes a real,
reusable `Clip` (#10 above), a track's clip list can fill up (Session view
shows 8 rows/track) - a performer just laying down ordinary, never-reused
scene content would accumulate one clip per take with nothing to launch
them for.

The sketch: a freshly recorded take starts as a third, provisional
*draft* state (neither a permanent `Clip` nor permanent background
content) that either gets explicitly promoted to a real clip, or decays -
explicitly or by not being promoted - folding into the track's background
and freeing its slot.

Merging isn't symmetric underneath even though the *capability* should be:
for a note track, two `Note`s have no meaningful "sum," so merging is a
destructive overwrite (built - see `merge-clip-to-background`, #15 above).
For a `SampleTrack`, real audio mixing is easy (straightforward addition)
but wanted mainly for symmetry - and needs a `SampleTrack` "background
bed" to merge into, which doesn't exist yet (this whole design is built
around discrete triggered clips). That background would need no trim
points of its own (trimming happens before merge) and no looping (a scene
doesn't loop) - what's actually open is just how the background audio is
stored and summed against whatever's currently placed.

Deliberately still unresolved: the exact promotion/decay trigger (a
command? a timeout? leaving a clip's row on a Launchpad?), what "decay"
looks/sounds like while pending, and the `SampleTrack` background's own
storage shape.
