# OutlineView library follow-ups

## Context

OutlineView is now a working instrument/groove browser: Library >
Instruments (taxonomy paths, keyboard audition, NCKEY_ENTER adds to the
song's pool) and Library > Grooves (27 real, standard-named rhythm
patterns spanning several meters, looping preview, NCKEY_ENTER adds a
Clip on a found-or-created PercussionTrack), a Details panel to the right
of the tree, Delete for tracks/pool instruments, mouse scroll. This
captures what's still open, roughly in the order to tackle it - bass-line
generation and the arpeggiator groove part deliberately last, since
neither design is settled yet and both need more thought before
committing to either.

Done:
- Instrument library descriptions (`GmInstrumentDescriptions.h`, one per
  GM taxonomy path, plain array + a test guarding it against
  `GmInstrumentTable.h`, shown in the Details panel the same way a
  groove's own `description` already is).
- Real mouse click support - clicking a tree row moves the cursor there;
  Details panel action lines are real clickable targets now
  (`buildDetailsLines()`/`DetailsAction` is the one shared source both
  drawing and click hit-testing use, so a click and its keyboard
  equivalent can never drift apart).
- Scroll-wheel no longer moves the cursor - a new `scrollBy()` only
  touches the viewport, clamped so it can't scroll past the last row.
- Preview click/pop artifacts - root-caused and fixed: retriggering
  PREVIEW_NOTE/PREVIEW_GROOVE was destroying the previous voice(s)
  outright (a plain `unique_ptr` assignment / `vector::clear()`), a hard
  cut mid-waveform. Now `fastRelease()`s the old occupant(s) and lets
  them finish their own tail in `preview_voices_` (the same "masked by a
  fresh attack" mechanism `InstrumentTrackState::retriggerVoices()`
  already uses for a real track), reclaimed normally once
  `isActive()` goes false. Verified structurally (voice count right
  after retriggering), not by ear.
- Song > Instruments (pool) rows now preview the same way a Library >
  Instruments row does - note keys audition the exact pool slot
  (PlaybackControlEvent::PREVIEW_POOL_NOTE, addressed by pool index rather
  than a re-resolved name, so generator overrides/custom Oscillator
  parameters sound correctly) - and show a description in the Details
  panel: a custom one (new `Instrument::getDescription()`/
  `setDescription()`, an XML attribute on any pool slot) if authored,
  else, for a GenericInstrument slot, its resolved SoundFont/taxonomy
  entry's own curated description (inherited, not duplicated).
- A Library > Grooves row's own Add to Song now has a real target-track
  picker instead of always defaulting to "the first PercussionTrack,
  create one if none exists" - a `[t] Target: <label>` line in its own
  Details panel; 't' or a click opens a real floating ncselector plane
  (`UIPlane::showPicker()`/`addItem()`/`pickerActive()`/
  `getPickerSelection()`/`closePicker()`, new - notcurses's own list-
  picker widget, not text drawn inline into the Details panel) listing
  every root PercussionTrack plus "New track"; Enter or a click on an
  item picks it. Defaults to the old first-or-create behavior when never
  touched. `OutlineView::compatibleTargetTrackRows()`/
  `resolveTargetTrackId()`/`targetTrackLabel()` are the reusable pieces.

## 1. Bass-line generator (design not settled)

Agreed so far: a live generator (an `Arpeggiator::Mode::BASS`, most
likely, reusing its held-chord machinery rather than a new track type)
that reads real chord notes from a pattern - never baked scale-degree/
chord-quality data, since the chord-construction rules for the
project's own microtonal system (quartal chords, interval-splitting)
aren't finalized, and a live generator sidesteps needing them at all: it
only ever reads whatever notes are actually entered, correct by
construction in any key, tuning, or chord shape.

**Sequencing once this is picked back up**: the library/content half
first - extend `GroovePatternTemplate` with a pitch-free
`GrooveBassHit { row, velocity, chord_tone_index, octave_offset }` list
(`chord_tone_index` selects the Nth-lowest held note, never an absolute
pitch or scale degree) and get it audible via preview, before touching
"Add to Song"/track-creation/multi-clip workflow at all - same "hear it
before wiring it up properly" order the drum grooves themselves
followed.

**Still open, deliberately unresolved**: where the chord notes
themselves live relative to the bass rhythm.
- *Same track* (favored so far): the bass-generator track's own pattern
  is the chord track too - you enter C/G/Am/D there directly, and
  `bass_hits`' rhythm re-triggers against whatever's currently held,
  exactly like `ArpeggiatorState` already does. Zero new cross-track
  mechanism.
- *Separate chord track*: lets a groove's own bass rhythm and your
  chord-entry rhythm be edited independently, but means teaching the
  engine to read one track's held notes from a different track's
  renderer - nothing like this exists today, real new machinery.

Also still open: whether multiple clips on the same bass-generator track
(verse chords vs. chorus chords, a C-G / Am-D example) should be able to
carry genuinely different *rhythms* too (a different `bass_hits`
selection per clip - meaning bass rhythm has to live on the Clip, not
just the originating library template), or whether one groove's bass
rhythm is meant to stay fixed across every clip built from it. Worth
settling before the data model is finalized, not after.

## 2. Arpeggiator groove part (last - design not settled)

A groove can already carry a rhythm-only percussion part
(`GroovePatternHit`, absolute GM notes). An arpeggiator part is the same
idea one step more general - a rhythm plus a *mode* (`Arpeggiator::Mode`,
already `UP`/`DOWN`/`UP_DOWN`), stepping through whatever chord is held
on that track, entered as ordinary notes the same way any chord already
is. Since `Arpeggiator`/`ArpeggiatorState` already exist and already
read a held chord from pattern-driven note-on/off, this needs no new
engine mechanism at all - just:

- `GroovePatternTemplate` gains an optional arp part (which `Arpeggiator::
  Mode`, a note-duration/gate, maybe an octave range - `Arpeggiator`'s
  own existing fields already cover this).
- "Add to Song" finds-or-creates a root `Arpeggiator` track the same way
  it already finds-or-creates a `PercussionTrack`, through its own
  floating target-track picker (`compatibleTargetTrackRows()`/
  `resolveTargetTrackId()`/`targetTrackLabel()`/`openTargetPicker()`,
  added for the groove picker above) - generalizing those past their
  current `TrackType::PERCUSSION_CONTROL` hardcoding (a `TrackType`
  parameter, or a small predicate) rather than copy-pasting a second,
  Arpeggiator-only picker.
- The new clip lands empty of real chord notes (or, optionally, ships
  with a literal example chord progression as ordinary Pattern notes -
  no scale/chord table needed for that, it's just notes) - you enter
  the actual chord tones yourself, same as for the bass-line generator
  above.
- Preview: same Player-side groove scheduler, extended to also step an
  arp voice from whatever chord notes the template's arp part (if any)
  supplies for preview purposes, or skipped in preview entirely and only
  wired up once a real clip/chord exists - worth deciding once this is
  actually being built, not now.

Mechanically simpler than the bass-line generator (no new selection
rule, no open chord-source-topology question), but shares the same "the
clip needs real chord notes in it" dependency, which is why it's grouped
down here with it rather than earlier.
