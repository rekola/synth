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
16. **`SampleTrack` background bed** - `Scene::getSampleBackgroundContent()`/
    `getOrCreateSampleBackgroundContent()`, a `SampleContent` per (scene,
    track) sibling of `patterns_by_track_id_`, never trimmed/looped (both
    fields stay at their unset defaults - only the buffer/native rate are
    ever written). `merge-clip-to-background` (#15) now handles a
    SampleTrack clip too: a real additive multiply-add mix into the bed
    (`resolveSampleAudio()`, a real, materialized-PCM trim/resample/
    stretch resolution shared with `SampleTrackState::triggerVoice()`'s
    own tempo-stretch-needed case - see #17 below for why real-time
    playback otherwise resolves differently), re-baked once per lap for a
    looping clip - the note-track merge doesn't need this (`Pattern::
    getEffectiveRow()`'s own modulo already wraps a looping clip's
    content row-by-row for free), but a real audio buffer isn't row-
    granular, so the sample-mix path explicitly tiles the resolved audio
    across each lap boundary itself, matching what real playback would
    actually have triggered. `SongState::renderBlock()` plays a scene's
    background bed continuously, entirely independent of whatever the
    arrangement/clip layer resolves to - not masked by a placed clip on
    top of it, and not silenced by an explicit stop either (a stop only
    ever means "nothing from the clip layer is overriding this track
    right now"; unlike a note track's own background Pattern, which a
    stop still silences today - a real, confirmed pre-existing bug in #15
    above, found while building this and logged in `docs/known_bugs.md`
    rather than fixed here, since #15 already shipped separately and
    changing its behavior wasn't asked for). Real audio genuinely mixes,
    unlike two `Note`s, so `SampleTrackState` now has exactly two fixed,
    independent voices - `kClipVoiceId` (the one clip that can be
    playing) and `kBackgroundVoiceId` (the scene's own bed) - routed
    through `LeafTrackState`'s existing per-voice `voices_` map, which
    `SampleTrackState::triggerVoice()` and `RenderContext`'s own
    `SampleTrackEvent::is_background` flag address by fixed, named role
    rather than an arbitrary index (an early draft called this a
    "column," which reads as "one of several simultaneous clips" - not
    the model; a SampleTrack still only ever has one clip). Persists
    as `<sampleBackground track="..." file="...">` inside `<scene>` plus a
    `<song-stem>.samples/background_<scene-id>_<track-id>.wav` sidecar,
    swept alongside orphaned clip sidecars on save; a scene gets a real,
    stable id of its own (`Song::generateUniqueSceneId()`) the moment it
    first gets a bed, precisely so the sidecar's own name doesn't have to
    depend on the scene's ordinal position (inserting/reordering scenes is
    a normal edit).

    `PatternEditor`'s own SampleTrack placeholder column (#8/#12 above)
    shows the bed's own waveform too - only when no clip instance covers
    that row, though; a clip placed on top still draws only the clip's
    own shape (the two genuinely mix in the audio, but there's no
    combined-waveform rendering yet - out of scope for this pass). Bright
    plain text color (the same brightness an ordinary defined note gets
    in a NOTE column), not the track's own identity color a real clip's
    waveform is tinted with - that color means "a clip," and the bed
    isn't one. The waveform's own coverage bars sit under the row's own
    *ambient* grid tinting (bar/beat-accent, neighboring-pattern/repeat-
    row dimming) - a real clip's waveform (#12 above) used to blend its
    "on" color toward that state too, tinting visibly as the playhead
    swept down an unremarkable row's own accent color, fixed the same way
    for both here. The playhead row and an actual selection are different,
    deliberate cues though, not ambient state - a selection fully inverts
    the waveform to `cur_fg`, same as every other column type's own
    cursor/selection cue, but the playhead row only tints it partway
    there (`base.blend(0.35f, cur_fg)`) rather than fully replacing it,
    reading as a translucent green layer over the waveform's own shape/
    color rather than solid green. The cell's own background needs the
    identical partial-tint treatment on the playhead row, not the
    playhead's own full-strength `cell_bg` - pairing a lightly-tinted
    foreground against a solid-green background read as a color
    mismatch, not a highlight, the coverage bars losing contrast against
    a background greener than they were. A selection still uses plain
    `cell_bg` (already the full selection color there, no separate case
    needed), and an ordinary row's own ambient bar/beat-accent tinting is
    untouched either way.

    This turned into a whole-`PatternEditor` consistency pass, beyond just
    the waveform: the playhead row used to fully replace every column
    type's own color with solid green (`row_base_fg`/`row_base_bg`'s own
    `if (highlight)` branch, computed once and inherited by NOTE/
    VELOCITY/DELAY/EFFECT/the row-number gutter/dividers/the track
    identifier cell alike) - now a shared `tintForPlayhead()` helper and
    one `kPlayheadTintAlpha` constant (0.35, matching the waveform's own)
    apply the identical translucent-overlay treatment everywhere, so the
    whole row reads as one consistent tint rather than the waveform being
    the only column type treated this way. `row_base_fg`/`row_base_bg`
    themselves are pure ambient now (bar/beat-accent or plain, never
    playhead) - every column type calls `tintForPlayhead()` on its own
    base color instead, skipping it entirely when actually selected
    (`cell_is_selected = column_selected || in_selection`, checked once
    per column and reused). The annotation row's own playhead+red
    combination (a different kind of blend - two meaningful colors
    composited together, not a neutral base tinted by one) was left as
    its own 0.2 ratio, but reuses the same `kPlayheadTint` constant rather
    than duplicating the literal color value. `kPlayheadTint` itself is a
    single shared color (`#80b080`) - separate fg/bg targets (`#80c080`/
    `#80a080`, the exact literals `row_base_fg`/`row_base_bg` already used
    before any of this) existed only because the old scheme fully
    *replaced* a cell's color, and a foreground and background always
    need to differ for text to stay legible against its own background;
    once every column type blends only partway there instead, each
    already-distinct base (fg text color vs. bg fill) keeps enough of its
    own separation on its own, so one shared target color is enough.

    A track's own trailing divider/identifier-cell code (the "▌"/"│"
    glyphs marking a clip instance's own boundary, and the half-block
    continuation-row ear) had its own selection gap, found only after
    this sweep looked otherwise complete: several of its own background
    computations (`right_bg`, the continuation-row's own "▌", the plain
    "│" fallback) read straight from `row_base_bg` - ambient/playhead-
    aware after the fixes above, but never selection-aware at all. Fixed
    by checking `in_selection` (a whole-track/EVERYTHING-scoped mark)
    first at each of those sites, same as every other cell in the row.

    A second attempt tried to extend this further, to the everyday case
    of just moving the cursor with no mark set at all (NOTE_COLUMN-
    scoped, per-column rather than whole-track) - a `trailing_column_
    selected` flag tracking whether a track's own *last* column ended up
    selected, so the divider right after it could pick up the same
    highlight. Reverted: it bled the highlight into places it doesn't
    belong (the "▌" glyph's own *next-track* half, and plain "│"
    dividers that aren't even part of any instance boundary) with no
    clean way found to scope it correctly. The divider/identifier-cell
    code now only ever reflects `in_selection` - a real, whole-track/
    EVERYTHING selection reaches it; the ordinary single-cell cursor
    (no mark) does not, same as before this whole investigation started.

    Deliberately out of scope for this pass, flagged during review rather
    than decided unilaterally: there is no per-instance loudness/velocity
    field anywhere in the data model yet (a scene's instance placement is
    just a clip_id string), so the merge mix always uses a plain gain of
    1.0 - `mixIntoSampleBackground()`'s own `gain` parameter is real
    multiply-add DSP, future-proofed for a real multiplier, but nothing
    sets it to anything else yet. Open questions for whenever that gets
    designed: should a clip instance carry its own velocity/gain at all;
    should a Launchpad pad's own hit velocity feed it at launch time; and
    should the same apply to instrument (note) clips, not just
    SampleTrack ones.
17. **Real-time resampling for a native-rate-mismatched clip** -
    `SampleTrackState::triggerVoice()`'s ordinary (no tempo-stretch
    needed) case no longer converts a whole clip's own buffer to the
    output rate synchronously on the audio thread before it can start
    playing at all - real, potentially XRUN-causing work for a long clip.
    `resolveRealtimeSampleAudio()` (`SampleTrack.h`) instead hands
    `SampleClipVoice` the content's own native-rate buffer directly, plus
    a `playback_ratio` (native frames per output frame); the voice reads
    a fractional position through its own `render()` loop, linearly
    interpolating between neighboring native samples each output sample,
    the same real-time resampling technique any pitch-following playback
    already needs, rather than ever materializing a converted copy.
    `resolveSampleAudio()` (the original, whole-buffer version) still
    exists and is still used for two things that genuinely need real,
    materialized PCM: `mergeClipToBackground()`'s own baking (#16 above),
    and `triggerVoice()`'s own fallback when tempo-stretching is also
    needed (SoundTouch has no real-time path of its own yet -
    `docs/known_bugs.md`'s existing entry on that stays accurate and
    unrelated to this fix).

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
- **SampleTrack background bed (#16), real audio**: place a SampleTrack
  clip in a scene, `merge-clip-to-background` it, and confirm the merged
  audio is actually audible at transport playback (both live and
  `--render`) at exactly the rows it used to cover, including a looping
  clip's own repeats across the rest of the scene, and right through the
  merge's own leftover stop. Then place a *second*, un-merged clip on top
  of the already-merged bed and confirm both are genuinely audible
  together (a real mix - the bed keeps playing underneath, not silenced
  by the clip on top of it) rather than one replacing the other. Save and
  reload, confirm the merged audio survives the round trip and still
  plays back correctly positioned.
- **Real-time resampling (#17), real hardware**: load/trigger a long
  clip whose native rate disagrees with the session's own output rate
  (e.g. a several-minute file at 44.1kHz in a 48kHz session) and confirm
  the XRUN that used to happen right at trigger is gone, and that the
  clip still plays at the correct pitch/speed throughout.

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
For a `SampleTrack`, real audio mixing/rendering into a background bed is
now also built (#16 above).

Deliberately still unresolved: the exact promotion/decay trigger (a
command? a timeout? leaving a clip's row on a Launchpad?), and what
"decay" looks/sounds like while pending.
