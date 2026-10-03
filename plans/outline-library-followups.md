# Bass-line generator / arpeggiator rhythm part

Two remaining Library > Rhythms follow-ups, in the order to tackle them -
both deliberately last since neither design is settled yet, and the
arpeggiator part shares the bass-line generator's own "the clip needs
real chord notes in it" dependency.

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
first - extend `RhythmPatternTemplate` with a pitch-free
`RhythmBassHit { row, velocity, chord_tone_index, octave_offset }` list
(`chord_tone_index` selects the Nth-lowest held note, never an absolute
pitch or scale degree) and get it audible via preview, before touching
"Add to Song"/track-creation/multi-clip workflow at all - same "hear it
before wiring it up properly" order the drum rhythms themselves
followed.

**Still open, deliberately unresolved**: where the chord notes
themselves live relative to the bass rhythm.
- *Same track* (favored so far): the bass-generator track's own pattern
  is the chord track too - you enter C/G/Am/D there directly, and
  `bass_hits`' rhythm re-triggers against whatever's currently held,
  exactly like `ArpeggiatorState` already does. Zero new cross-track
  mechanism.
- *Separate chord track*: lets a rhythm's own bass rhythm and your
  chord-entry rhythm be edited independently, but means teaching the
  engine to read one track's held notes from a different track's
  renderer - nothing like this exists today, real new machinery.

Also still open: whether multiple clips on the same bass-generator track
(verse chords vs. chorus chords, a C-G / Am-D example) should be able to
carry genuinely different *rhythms* too (a different `bass_hits`
selection per clip - meaning bass rhythm has to live on the Clip, not
just the originating library template), or whether one rhythm's bass
rhythm is meant to stay fixed across every clip built from it. Worth
settling before the data model is finalized, not after.

## 2. Arpeggiator rhythm part (last - design not settled)

A rhythm can already carry a rhythm-only percussion part
(`RhythmPatternHit`, absolute GM notes). An arpeggiator part is the same
idea one step more general - a rhythm plus a *mode* (`Arpeggiator::Mode`,
already `UP`/`DOWN`/`UP_DOWN`), stepping through whatever chord is held
on that track, entered as ordinary notes the same way any chord already
is. Since `Arpeggiator`/`ArpeggiatorState` already exist and already
read a held chord from pattern-driven note-on/off, this needs no new
engine mechanism at all - just:

- `RhythmPatternTemplate` gains an optional arp part (which `Arpeggiator::
  Mode`, a note-duration/gate, maybe an octave range - `Arpeggiator`'s
  own existing fields already cover this).
- "Add to Song" finds-or-creates a root `Arpeggiator` track the same way
  it already finds-or-creates a `PercussionTrack`, through its own
  floating target-track picker (`compatibleTargetTrackRows()`/
  `resolveTargetTrackId()`/`targetTrackLabel()`/`openTargetPicker()`,
  added for the rhythm picker above) - generalizing those past their
  current `TrackType::PERCUSSION_CONTROL` hardcoding (a `TrackType`
  parameter, or a small predicate) rather than copy-pasting a second,
  Arpeggiator-only picker.
- The new clip lands empty of real chord notes (or, optionally, ships
  with a literal example chord progression as ordinary Pattern notes -
  no scale/chord table needed for that, it's just notes) - you enter
  the actual chord tones yourself, same as for the bass-line generator
  above.
- Preview: same Player-side rhythm scheduler, extended to also step an
  arp voice from whatever chord notes the template's arp part (if any)
  supplies for preview purposes, or skipped in preview entirely and only
  wired up once a real clip/chord exists - worth deciding once this is
  actually being built, not now.

Mechanically simpler than the bass-line generator (no new selection
rule, no open chord-source-topology question), but shares the same "the
clip needs real chord notes in it" dependency, which is why it's grouped
down here with it rather than earlier.
