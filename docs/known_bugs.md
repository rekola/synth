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

- **`tools/e2e/verify_launchpad_e2e.py`'s pad-press-writes-a-note checks fail
  in at least one sandboxed test environment**, independent of any code
  change: confirmed by running the identical script against an unmodified
  `git stash`-clean checkout, where it fails the exact same 3 of 7 checks
  (note-entry, off-sentinel, aftertouch-in-place) while still passing the
  Programmer-Mode/Device-Inquiry SysEx checks - so the fake device
  connects and synth talks to it, but a press's *effect on the
  pattern* doesn't show up within the script's wait window in this
  environment. Not investigated further (e.g. whether it's ALSA sequencer
  event delivery timing, scheduler fairness between the fake-device
  process and the audio thread, or something else sandbox-specific) -
  flagging so a future real hardware/less-restricted-environment run isn't
  mistaken for a regression if this same subset fails there too, and so a
  new e2e script that also depends on "press changes something, verify
  it" (e.g. `verify_launchpad_stepseq.py`) doesn't get blamed for a
  failure that reproduces on main. `verify_launchpad_stepseq.py` in
  particular hits an even more total version of this same flakiness in
  this environment: the fake device sometimes receives no SysEx at all
  (not even the initial Programmer-Mode/Device-Inquiry handshake),
  consistent with the two test runs racing over the same reused ALSA
  sequencer client id right after the prior run's `synth` process was
  `SIGKILL`ed rather than shut down cleanly. A manual, unscripted repro
  of the identical interaction (spawn synth against
  `drum_machine_stepgrid_test.xml`, connect the same fake device,
  navigate onto the track, press the pad) does complete the full
  handshake and step-toggle correctly, confirming this is the harness
  racing itself in this environment rather than a feature regression.

- **`tools/e2e/verify_launchpad_stopclip.py` hits the same class of
  flakiness above, in a new shape**: the simulated device's SysEx LED
  traffic goes completely silent after the *first* Session-view pad
  press, for the rest of the run - not just a pad press's effect on the
  pattern failing to show up (the earlier bullet's symptom), but every
  further LED refresh for that device going missing entirely, regardless
  of what's pressed afterward. Confirmed independent of the Stop Clip
  redesign this script exists to cover: a minimal two-plain-pad-press
  repro (no CC49 involved at all) reproduces the identical one-dump-then-
  silence pattern. `synth` itself stays alive and responsive throughout
  (its own info line keeps updating every second) - only this specific
  device's LED SysEx stream stalls, consistent with the same "harness vs.
  this sandboxed environment" territory the two bullets above are already
  in, not a real regression.

  Narrowed further since: timestamped tracing on the receiving `synth`
  process (both in `LaunchpadIO::pollEvents()`'s own per-event read loop
  and in `TerminalUI`'s outer `poll()` loop) shows the fake device's own
  CC49/second-pad-press/CC49-release messages genuinely never reach
  synth's ALSA sequencer receive queue until the fake device's own
  `snd_seq_close()` tears the connection down - they then arrive all at
  once, several seconds late, bundled with that same connection's
  PORT_EXIT/CLIENT_EXIT notifications. Two direct fix attempts on the
  fixture's own C source both failed to change this: matching
  `fake_launchpad_session.c`'s proven-reliable "sleep once, read back
  once" pattern instead of `fake_launchpad_stopclip.c`'s original
  per-100ms polling wait, and separately widening every inter-send gap to
  a full second (matching `fake_launchpad_session.c`'s own timing
  exactly). Neither budged the stall, ruling out both the fixture's own
  wait/poll pattern and the gap length between sends as the cause. The
  one remaining structural difference from the passing session script:
  this fixture triggers with Record Arm off, taking the real audition/
  playback path (an actual synthesized note starts rendering through
  ALSA) rather than session's own Record-Arm-on assign path (which writes
  song data and renders nothing) - consistent with real audio rendering
  contending with this sandboxed environment's ALSA sequencer delivery
  somehow, though that's inference from what's now ruled out, not
  confirmed directly. Still not investigated further past this point -
  same reasoning as above.

  `tools/e2e/verify_launchpad_sampletrack_stopclip.py` (SampleTrack's own
  Session-view audition path, reusing `fake_launchpad_stopclip.c`
  unchanged against a SampleTrack fixture) hits the identical symptom for
  the identical reason - it takes the same real audition/playback path,
  just triggering a `SampleClipVoice` instead of a pitched one. Confirmed
  by reverting to the last commit before that script existed and
  re-running `verify_launchpad_stopclip.py` alone: it already fails the
  same way with none of that work present, ruling out anything
  SampleTrack-specific.

  Still reproduces identically after Stop Clip's own redesign from a held
  column modifier into the track-picker overlay (both scripts now fail
  most of their checks, more than before since the overlay's own
  stays-open-after-a-pick and Session-view-only redesigns each added
  their own further LED assertions) - the stall still happens on the very
  first real-audition pad press, before CC49 is ever sent, so it's
  orthogonal to which gesture eventually reads the resulting LED state.
  `verify_launchpad_mute_picker.py` (the Mute purpose's own e2e script,
  which never triggers real playback at all) originally avoided this
  specific stall - some evidence for the "real audition/playback path"
  theory above - but now also hits it intermittently since Session's own
  mixer submode shipped (`GridMode`'s own comment): the script has to
  press CC95 a second time before CC39 means anything, and that
  particular press is the one that most often goes missing/delayed now,
  the same symptom class, just on a different button. Not itself evidence
  against the playback-path theory (CC95 presses toward real Session
  content have always been part of every script here, this is the first
  one where a *second* CC95 press mid-script - not just the connect-time
  default - is required for the test to mean anything).

  `verify_launchpad_stopclip.py`/`verify_launchpad_sampletrack_stopclip.py`
  gained two further checks each (Session view's own triggered-pad pulse/
  flash animation, `LaunchpadManager::refreshLeds()`'s SESSION branch) that
  hit the identical stall for the identical reason - both scripts still
  fail the same 3/9 checks (the ones that don't merely need the very first,
  pre-stall LED dump) with these two included, confirmed reproducing
  bit-for-bit identically across repeated runs, not a regression from that
  work. (This particular script's own failure count has since drifted
  further in this sandbox, independent of any of the above - see the
  `verify_launchpad_mixer_hold.py` bullet below.)

  `tools/e2e/verify_launchpad_mixer_hold.py` (new, covering the mixer
  radio group's own momentary hold-to-preview gesture -
  `LaunchpadManager::armMixerHoldPreview()`/`handleMixerFunctionRelease()`)
  hits the identical stall for the identical reason, right after its own
  first CC95 mixer-submode-entry press - 1/6 checks pass (only the
  startup Programmer-Mode handshake), reproducing identically across
  repeated runs and identically on an unmodified checkout predating this
  feature (confirmed by `git stash`-ing this work and re-running
  `verify_launchpad_stopclip.py` alone, which already reproduces its own
  1/9 - down from the 3/9 recorded above, this sandbox's own stall
  severity apparently isn't perfectly stable run to run either - on that
  same unmodified checkout), so this is the same pre-existing
  environmental limitation, not a regression from this feature.

  This sandbox also had real Launchpad X and Launchpad Mini MK3 hardware
  attached (confirmed via `aconnect -l` - `type=kernel` clients, not the
  `type=user` clients every `fake_launchpad_*.c` simulator registers as),
  which used to be its own separate source of noise from the delayed-
  SysEx-delivery stall above: a `synth` instance spawned for one of these
  e2e scripts auto-connects to real hardware exactly as readily as to the
  simulator, alongside whatever interactive `synth` session (if any) a
  person also has running against the same hardware at the time - two
  processes ending up subscribed to the same physical device's input port
  simultaneously. Observed once as a handful of spurious extra CC49
  presses reaching a `verify_launchpad_mute_picker.py` run's own
  `LaunchpadManager` (traced via temporary debug logging, since removed)
  with no corresponding line in that run's own fake-device script - never
  root-caused (a firmware handshake echo being misdecoded as a button
  press was the leading guess) or reproduced a second time before
  `harness.py`'s `spawn()` started setting `SYNTH_LAUNCHPAD_NO_HARDWARE=1`
  (`LaunchpadIO.h`'s own `ignore_hardware_` comment), which keeps every
  e2e-spawned `synth` from ever auto-connecting to real hardware at all -
  the fix in case this exact cross-talk symptom is ever seen again on a
  sandbox with `SYNTH_LAUNCHPAD_NO_HARDWARE` for some reason not taking
  effect (e.g. a `harness.py` bypassed, or predating this fix).

- **`tools/e2e/verify_launchpad_sendmode.py`'s own row-math predates the
  current dB-based Send fader curve** (`sendRowToDb()`/`sendLinearToRow()`
  in `LaunchpadManager.cpp`), not anything touched by the velocity-scaled
  glide/micro-value work above. Its docstring/comments describe an old
  linear `row = round(value * 7)` mapping - under that, the fixture's
  initial `sendA=0.3` would resolve to row 2, and pressing row 5 would be
  a clear, large jump to ~0.714. Under the real curve, `linearToDb(0.3)`
  already resolves to row 5 on its own (confirmed by comparing an
  unmodified checkout's own "before press" LED dump against its "after
  press" one - both already read identically, `(00, 7f, 7f)`, before any
  of this session's changes), so pressing row 5 barely moves anything and
  the two checks built on "this press visibly changes row 5" (3 of the
  script's 6) have been silently checking a near-no-op for some time,
  independent of anything in this file. Confirmed the glide/micro-value
  work reproduces this exact same pre-existing final LED state byte-for-
  byte against an unmodified checkout, not a regression from it. Not
  fixed - the fixture would need to press a row genuinely far from
  `sendA`'s own starting point (e.g. row 0 or 7) to demonstrate a real
  change under the current curve, and its own docstring math corrected to
  match.

- **`LaunchpadManager`'s fader glide/micro-value feature (`applyFaderPress()`/
  `tickFaderRamps()`) has no e2e coverage for the Pan path**, only Send A
  (`verify_launchpad_sendmode.py`/`verify_launchpad_sendmode_autocreate.py`)
  - Pan's own `wraps = true` branch (row 7 neighboring row 0, unlike a
  Send's true ceiling there) is exercised only by reasoning/code review,
  not a real simulated press sequence. Not fixed - would need a new
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

- **`merge-clip-to-background` (note tracks) silences the content it just
  merged, immediately, contradicting its own doc comment.** Confirmed
  directly (a small instrumented test, not kept in the suite): after
  `mergeClipToBackground()` copies a clip's own notes into the section's
  background `Pattern` and calls `placeStopInstance()` to free the
  placement, `resolveInstanceAt()` at that same row returns
  `Section::kStopInstance`, not `Section::kNoInstance` - and `SongState::
  renderBlock()`'s own note-scheduling loop only ever reads a track's
  background `Pattern` on `kNoInstance` (an explicit stop resolves "the
  same way as no instance at all" only for `resolveEditTarget()`, i.e.
  what the pattern editor lets you *edit*, per its own doc comment - real
  *playback* draws a different, undocumented line). So real transport
  playback goes silent at exactly the row(s) that were just merged,
  directly contradicting `mergeClipToBackground()`'s own doc comment
  ("reproducing exactly what was already audible... not combining the
  two"). Root cause: `placeStopInstance()`'s "OFF" sentinel is used for
  two different things that don't actually mean the same thing - a
  deliberate user "silence this track" gesture (Session view/
  ArrangementGrid), and "this placement is over, nothing further to say
  about it" (this merge's own cleanup) - and `SongState.h` can't tell
  them apart, so it treats both as "silence everything, background
  included." The SampleTrack background bed (this file's own #16) was
  deliberately built to *not* have this problem - its own background only
  ever gets masked by a real clip instance, never by a stop - but that fix
  was scoped to `SampleTrack` only; the note-track version of this bug is
  untouched. Not fixed - would need `SongState.h`'s note-track background-
  Pattern branch to read the background on `kStopInstance` too (matching
  the SampleTrack fix), or a real distinction between "this placement
  ended, fall through" and "silence this track" at the storage layer if
  the two are ever meant to behave differently after all.

- **`GroovePatternLibrary.cpp`'s swung entries ("Swing", "Boogie") are
  straight-16th-grid approximations of a genuine 2:1 triplet swing, not
  the real thing** - their ride patterns fake the "long-short" feel by
  spacing plain 16th-note hits unevenly rather than actually placing the
  upbeat at the true swung position. The engine already has the right
  primitive to do this precisely (`Note::getDelay()`/`getDelayAsFloat()`,
  the pattern editor's own DELAY column, already consumed by real
  playback - `SongState.h`'s `delay_samples` computation), and it was
  tried directly on these two patterns' own hit data with correct results,
  but reverted: a delay baked into one lane of static library data doesn't
  compose - it can't also shuffle a bass line or anything else added
  alongside the same groove later, since nothing ties them to a shared
  swing amount. A real fix needs a pattern/song-level swing or groove-
  shuffle setting applied at playback time to everything scheduled against
  it (a new model field plus engine support plus UI), not a per-hit
  workaround in this library's own data - not designed or scoped yet.
  Every other groove either sits fine on plain straight rows or, where the
  genre genuinely needs triplet subdivision ("Slow Rock", "Shuffle Blues"),
  already sidesteps this by using real 12/8 meter (24 rows = 12 real
  eighth-note triplet pulses) instead of approximating it inside a 4/4
  grid, so this is scoped to those two entries only, not the row grid in
  general.

