# Known bugs

Found 2026-07-11, not yet fixed.

- **Ctrl-P (raw byte `0x10`) never reaches the application at all**, on any
  terminal tested so far — not even as an unhandled/ignored keystroke.
  Confirmed via a stderr trace at the earliest point notcurses hands input
  to the app (`TerminalUI::readInput`, before any app logic runs): no
  `ncinput` event is ever produced for it. Unexplained; not investigated
  further. Avoid binding Ctrl-P to anything until this is understood.

- **Ctrl-Q (quit) can take many seconds to actually exit** the process
  after being pressed, rather than exiting promptly. Confirmed pre-existing
  (reproduces on old commits too, via `git stash`), not caused by anything
  recent. Not yet investigated — likely something in the audio thread
  shutdown/join path in `Player.cpp`/`UI::start()`.

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

