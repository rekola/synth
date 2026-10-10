# Bass-line generator

The remaining Library > Rhythms follow-up, deliberately last since its
design is not settled yet. (An arpeggiator rhythm part was planned beside
it; the arpeggiator is gone - an arpeggio is written as notes in a clip
that loops - so that part is dropped.)

## Bass-line generator (design not settled)

Agreed so far: a live generator that reads real chord notes from a
pattern - never baked scale-degree/
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
  `bass_hits`' rhythm re-triggers against whatever's currently held. The
  held-chord machinery this once reused (the arpeggiator's) no longer
  exists, so it would have to be built for the generator.
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
