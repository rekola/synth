# Known bugs

Found 2026-07-11, not yet fixed.

- **`SongState::getRelativePosition()` doesn't wrap back to pattern 0** once
  playback advances past the last pattern in the song's pattern list — it
  only handles moving *forward* between multiple existing patterns in
  sequence, so for a short or single-pattern song (e.g. a freshly created
  new-song, 64 rows / 1 pattern), once playback runs past row 64 it starts
  reporting an out-of-range pattern index (`Song::getPattern()` bounds-checks
  and returns a static empty pattern, so this doesn't crash, but the
  displayed pattern/row numbers become nonsensical and playback presumably
  renders silence instead of looping).

- **Space (toggle-playing) can become unresponsive for several seconds
  while a busy song is actively playing** — reproduced repeatedly with
  `songs/demo3.xml` (multiple tracks, high simultaneous voice counts):
  pressing Space had no effect at all for 3+ seconds of continuous
  playback in one run, while the same key reliably worked instantly
  against a freshly created (empty, `Ctrl-N`) song. Not yet root-caused;
  suspect keyboard input getting starved behind a backlog of
  playback/render work in `TerminalUI`'s shared `poll()` loop when the
  audio-event descriptor is almost always ready, though this wasn't
  confirmed (the existing anti-backlog skip in `UI::handlePlaybackEvent`
  addresses redundant *rendering* work for superseded events, not input
  responsiveness specifically). Worth a closer look if users report the
  UI feeling unresponsive during dense playback.

- **`effects/Distortion.cpp` likely distorts Main and AuxA/AuxB
  inconsistently whenever clipping is involved.** It applies its curve
  (`HARD_CLIP`/`SOFT_CLIP`/`TANH`) independently to each channel, and now
  intentionally processes Aux channels too (not just Main - amplitude/
  dynamics effects were changed to affect the send bus the same way the
  dry signal does), but Main and Aux carry *differently scaled* copies of
  the same underlying dry signal (Main is gain-encoded per direction/
  distance; Aux is `dry * sends.a`/`dry * sends.b`, whatever the track's
  own Send A/B knobs are). Distortion curves here are nonlinear and
  amplitude-dependent (that's the whole point of a clipper), so feeding
  differently-scaled copies of the same waveform through the same curve
  does not produce a uniformly-scaled version of the same distortion
  character - a channel loud enough to actually clip sounds audibly
  different (harmonically) from one that stays under threshold, even
  though both started from the same dry signal. In practice: if Send A/B
  is set low relative to the dry level, Main may clip hard while Aux stays
  clean (or vice versa with a hot send and quiet dry mix), rather than the
  reverb/delay bus hearing "the same distortion, just quieter." Not fixed -
  would need normalizing each channel to a common reference level before
  the curve and undoing it after, or accepting the mismatch as a known
  character quirk of this effect.

- **A song with zero root tracks would crash on the very next render tick**,
  independent of the Launchpad or any other single feature.
  `PatternEditor::render()` does `track_ids[new_cursor.track]` (and several
  sibling call sites - `getTrackInfoFor(song, track_ids[current_cursor.track])`,
  `offerInput()`'s `song.getTrackByInternalId(track_ids[current_cursor.track])`,
  etc.) with no bounds check anywhere, on the assumption that at least one
  root track always exists. Found as a side effect of adding
  `PatternEditor::handleLaunchpadPadEvent`'s auto-create-missing-tracks
  behavior (both its Send/Pan-mode and NOTES-mode branches now grow the
  song up to whatever track index is needed, including from zero) - that
  fix makes the *Launchpad* input path safe against a track-less song, but
  the rest of the editor was never exercised against one, since nothing in
  the app can currently produce one in practice (`Ctrl-N`/new-song always
  creates exactly one track, and the pattern editor's own "duplicate/delete
  track" keybinding is an unimplemented stub - see the `'d'` case in
  `PatternEditor::offerInput()`). Not fixed - no current code path reaches
  it, so it's a latent landmine rather than something a user can trigger
  today, but it should be addressed (either bounds-check every
  `track_ids[...]` site, or make it structurally impossible to reach zero
  tracks - e.g. refuse to delete the last remaining track, once track
  deletion is actually implemented) before either the Launchpad auto-create
  path's zero-track branch or a real delete-track feature ships.

- **`LaunchpadManager`'s fader glide/micro-value feature
  (`resolveSendFaderTarget()`/`resolveAzimuthFaderTarget()`) has no e2e
  coverage for the Pan path**, only Send A
  (`verify_launchpad_sendmode.py`/`verify_launchpad_sendmode_autocreate.py`)
  - Pan's own wraparound branch (row 7 neighboring row 0, unlike a Send's
  true ceiling there) is exercised only by reasoning/code review, not a
  real simulated press sequence. Not fixed - would need a new
  `fake_launchpad_pan.c`/`verify_launchpad_pan.py` pair.

- **A voice's envelope keeps progressing while playback is stopped**, so a
  long-held note can resume out of sync with the (frozen) row/pattern
  position once playback restarts. `SongState::renderBlock()` calls every
  track's own `render()` unconditionally, every block, regardless of
  `isPlaying()` - only the note-scheduling/position-advance section is
  gated behind it - so any already-sounding voice's envelope, LFO, or
  effect tail keeps advancing through however long the transport sits
  stopped, the same way `ArpeggiatorState`'s own step timer used to (see
  `plans/arpeggiator-timing-fixes.md`, which fixes that one case
  specifically via a `resyncPlayhead()`/`resyncPlayheadAfterStop()` pair,
  without touching this more general issue). Whether keeping every track
  "live" through a stop is even the right behavior at all is genuinely
  undecided, not just unfixed - it's also what a real
  reverb/delay/decaying-note tail continuing to ring out after Stop relies
  on, which is arguably a deliberate, valued feature, not an oversight. Not
  fixed - see that plan's "Related, out-of-scope issue" section for the
  two directions considered (freeze rendering entirely while stopped, vs.
  extending the arpeggiator's own resync approach to envelopes generally)
  and why neither was attempted as part of that work.

- **Rendered output isn't bit-exact across genuinely different builds**
  (compiler version, optimization level, or CPU architecture), only across
  repeated runs of one fixed build. `-ffast-math` (`CMakeLists.txt`)
  permits reassociating floating-point operations, and floating-point
  addition/multiplication isn't associative - the compiler is free to
  group a reduction differently depending on its own vectorizer/target,
  so identical source can round to different bits on GCC vs. Clang,
  `-O2` vs `-O3`, or x86 vs. ARM. `SYNTH_MARCH` (pinned to `x86-64-v2` in
  CI) only fixes *which instructions* get used, not *what order*
  operations happen in, so `tests/RenderTests.cpp`'s golden-render hash
  test already gates its exact-hash assertions on that one canonical
  build rather than expecting them cross-platform. Not fixed - would need
  dropping `-ffast-math` (and default FP contraction) for a real
  performance cost on the per-sample DSP hot paths, or hand-controlling
  evaluation order in every reduction that needs to be portable; out of
  scope for now.

- **`Utf8::truncateToWidth()`/`Utf8::displayWidth()` (`src/util/Utf8.h`)
  don't merge flag emoji or multi-emoji ZWJ sequences into a single
  grapheme cluster**, so `PatternEditor::renderHeading()`'s track/
  instrument-name truncation can split one of those between its own
  codepoints rather than keeping it as one unit - narrower than the
  byte-offset-corruption bug this module otherwise fixes (ordinary text,
  including accented characters and non-BMP characters, is unaffected;
  no codepoint is ever split, only a multi-codepoint cluster). Root
  cause: libunistring's grapheme-break function is a plain pairwise
  `uc_is_grapheme_break(a, b)` with no state beyond the two adjacent
  codepoints, so it can't implement UAX #29's regional-indicator-pairing
  or emoji-ZWJ-sequence-lookback rules, both of which need to look past
  more than one pair (confirmed directly: `utf8proc`'s *stateful*
  grapheme-break function, which does carry that extra state, merges
  both cases correctly against the same input). Not fixed - accepted as
  out of scope for now since track/instrument/SF2-preset names
  realistically never contain a flag emoji or a ZWJ emoji sequence, and
  switching to `utf8proc` would add a second, otherwise-unneeded Unicode
  library to the dependency graph purely to cover that case.

- **SoundTouch-based tempo change (`TimeStretcher.h`'s `stretchMono()`)
  blocks the renderer and sounds metallic.** `SampleTrackState::
  triggerClip()` (`SampleTrack.cpp`) calls it synchronously, inline, the
  first time a clip is triggered at a song tempo that disagrees with its
  own recorded tempo - there's no cache yet at that point
  (`SampleContent::getStretchedBuffer()` only starts returning a hit
  *after* this same call already ran once and stored it via
  `setStretchedBuffer()`), and this whole call chain runs on the audio
  thread's own render path, not off it - so the very first trigger of a
  tempo-mismatched clip stalls real-time rendering for however long
  SoundTouch takes to process that clip's whole trimmed length, audible
  as a dropout/glitch. Separately, the output quality itself doesn't
  sound good - a metallic, reverb-like character - for reasons not yet
  root-caused; `stretchMono()` only calls `setSampleRate()`/
  `setChannels()`/`setTempo()` before processing, using every other
  SoundTouch setting (WSOLA sequence/seek-window/overlap length,
  `AA_FILTER`, etc.) at its own compiled-in default, which may simply not
  suit this engine's typical content/stretch ratios - not confirmed
  against tuning those settings, or against an entirely different
  algorithm. Not fixed - would need either moving the stretch off the
  render thread (background it and hold/mute until ready, or precompute
  it eagerly whenever a clip's tempo relationship becomes known rather
  than lazily on first trigger) and a real investigation into SoundTouch's
  own tunable parameters for the metallic artifact, or replacing the
  library entirely if tuning doesn't resolve it.

- **`SampleFileLoader.h`'s `writeMonoSample()` return value is ignored at
  both its call sites in `Song.cpp`** (a real clip's own sidecar, and a
  SampleTrack background bed's own sidecar), so a write that libsndfile
  actually rejects fails silently. Confirmed directly: `sf_open()` for
  `SFM_WRITE` with `sample_rate == 0` (e.g. a `SampleContent` whose
  `setNativeSampleRate()` was never called - live recording/file-loading
  always sets a real one, but nothing stops other code from constructing
  one without it) returns null (`writeMonoSample()` correctly returns
  `false`), yet libsndfile still leaves an 80-byte stub file on disk
  first - a valid-looking RIFF/WAVE/fmt header with a zero sample rate and
  zero-length `data`/`fact` chunks, not a missing file and not an error
  the writer's own caller ever sees. The next `Song::open()` of that same
  song then fails outright (`loadMonoSample()` can't read real audio back
  from the stub), aborting the whole load - so a clip/background whose
  own sample rate is somehow still 0 at save time produces a song file
  that looks like it saved fine but never opens again. Not fixed - would
  need checking `writeMonoSample()`'s own return value at both call sites
  and either bailing the whole save out the same way a malformed `<sample>`
  already fails a load, or skipping just that one `<sample>`/
  `<sampleBackground>` element with a diagnostic.

- **`merge-clip-to-background` (note tracks) silences the notes it just
  merged.** The command copies a placed clip's notes into the track's own
  background pattern and then removes the clip's placement by writing a stop
  marker there. During playback the background pattern is only read where a
  track has no placement at all, and a stop marker counts as a placement, so
  the merged rows play nothing. The pattern editor treats a stop marker like
  no placement, so the notes look merged and the bug shows only when
  playing. Not fixed. Either playback reads the background after a stop
  marker too (as it already does for sample tracks), or the merge writes no
  stop marker: the marker means both "silence this track here" and "this
  placement was removed", and only the first should silence it.
