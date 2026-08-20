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
  failure that reproduces on main.

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

- **`NoteCoordinate::track_id_` is sourced from the wrong value** -
  `SongState.h`'s note-scheduling loop builds it from `scene.getPatternsByTrack()`'s
  keys, which are a track's `getInternalId()` (`SongObject.h`): a
  process-lifetime `std::atomic<int>` counter shared by *every*
  `SongObject`-derived instance ever constructed in the process (tracks,
  patterns, instruments, everything), assigned in construction order,
  starting fresh each run. `NoteCoordinate.h`'s own doc comment already
  budgets `track_id_` as "1M tracks" of address space and treats it as a
  stable per-track identity for `HashField`-derived per-voice jitter/phase
  seeding (`InstrumentVoice.h`'s note-phase salt, SF2 percussion offset
  jitter, unison decorrelation) - but an internal id is neither: it depends
  on how many unrelated objects (most concretely, however many
  `SoundFontInstrument`s `InstrumentProvider::loadSoundFont()` happens to
  construct at startup, which varies with the loaded font/alias table)
  got constructed *before* this track, not on anything about the song or
  the track itself. Confirmed directly while verifying an unrelated,
  purely-cosmetic `InstrumentProvider` alias-table rename (see
  `plans/instrument-identity-generator-overrides.md`'s "Song rewrite
  mechanics" section): removing 4 incidental startup object constructions
  shifted every later track's internal id by 4, which changed the
  rendered audio of every song using the affected jitter/decorrelation
  paths - including songs with no SoundFont content at all
  (`songs/songtest18.xml`, built entirely from `<oscillator>` elements).
  What `track_id_` should actually be, per its own stated purpose (a
  stable, human-legible per-track identity for reproducible jitter,
  matching `column_`/`absolute_row_`'s already-song-relative scoping): the
  track's **physical/visible order** - its index within the song's own
  flattened track list, the same ordering already shown to the user in the
  pattern editor - not a global, run-order-dependent counter value. Not
  fixed - the call site (`SongState.h`'s two `NoteCoordinate(track_id, ...)`
  constructions) would need the track's position in `Song`'s track list
  (or `getRootTrackIds()`'s ordering) instead of the internal id it
  currently reuses for this.

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

- **The confirm-before-quit/confirm-before-switch-buffer prompt
  (`Controller::hasUnsavedChanges()`/`hasAnyUnsavedChanges()`, added
  2026-08-17/18) never fires for ordinary note/command entry** -
  typing/deleting a note, editing its velocity/delay, or editing an effect
  command directly in `PatternEditor` leaves `hasUnsavedChanges()` reporting
  false even though the pattern genuinely changed. Root cause:
  `hasUnsavedChanges()` compares `Song::getVersion()` against a baseline
  saved at load/save time, entirely dependent on `Track`/`Song` mutations
  calling `incVersion()` - but `PatternEditor::offerInput()`'s direct
  keyboard note-entry paths (`scene.setNote()`/`deleteNote()`/`pushNote()`/
  `setCommand()`, the velocity/delay-column edits) never call it. They set a
  `row_edited` flag instead, which predates `hasUnsavedChanges()` by years
  (traces back to 2022) and exists for a real, separate reason:
  `PatternEditor::render()` treats a `song.getVersion()` change as a
  signal to redraw the *entire* visible grid (`render_all`, gated in part on
  `song.getVersion() != current_song_version`), while `row_edited` alone
  only repaints the one row that changed - calling `incVersion()` on every
  keystroke would make each keystroke pay for a full-grid redraw instead of
  a single-row one. So the two flags were never meant to be interchangeable:
  `row_edited` is a redraw-scope hint, `incVersion()` is a content-identity
  counter, and `hasUnsavedChanges()` was built assuming the latter already
  covered every mutation, which it doesn't and never has for this specific
  path. Not fixed - the naive fix (add `incVersion()` calls at every
  `row_edited`-only site) would silently reintroduce the exact per-keystroke
  full-redraw cost `row_edited` exists to avoid; a real fix needs
  `render()`'s full-redraw trigger decoupled from the same counter
  `hasUnsavedChanges()` reads, so a version bump can be cheap to detect
  without being expensive to redraw.
