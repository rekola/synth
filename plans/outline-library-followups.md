# Bass-line generator / arpeggio rhythm part

Two remaining Library > Rhythms follow-ups, in the order to tackle them -
both deliberately last since neither design is settled yet, and the
arpeggio part shares the bass-line generator's own "the clip needs
real chord notes in it" dependency. Neither is a live generator or a track
kind (the arpeggiator track was removed): each writes ordinary notes
directly into the clip, which can then loop like any other.

## 1. Bass-line generator (design not settled)

Agreed so far: a generator that reads real chord notes from the clip and
writes the bass notes into it as ordinary notes - no track type and no
live machinery - never baked scale-degree/chord-quality data, since the
chord-construction rules for the project's own microtonal system
(quartal chords, interval-splitting) aren't finalized, and reading the
notes actually entered sidesteps needing them at all: it is correct by
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
- *Same clip* (favored so far): you enter C/G/Am/D in the clip directly,
  and the generator writes the bass notes into the same clip, in note
  columns beside the chords, stepping `bass_hits`' rhythm against
  whatever chord is sounding at each row. Zero new cross-track
  mechanism.
- *Separate chord track*: lets a rhythm's own bass rhythm and your
  chord-entry rhythm be edited independently, but the generator then
  reads another track's pattern at generation time - no engine
  mechanism, since it is an edit-time read, but the written notes go
  stale if the chords are edited afterwards.

Also still open: whether multiple clips on the same bass-generator track
(verse chords vs. chorus chords, a C-G / Am-D example) should be able to
carry genuinely different *rhythms* too (a different `bass_hits`
selection per clip - meaning bass rhythm has to live on the Clip, not
just the originating library template), or whether one rhythm's bass
rhythm is meant to stay fixed across every clip built from it. Worth
settling before the data model is finalized, not after.

## 2. Arpeggio rhythm part (last - design not settled)

A rhythm can already carry a rhythm-only percussion part
(`RhythmPatternHit`, absolute GM notes). An arpeggio part is the same
idea one step more general - a rhythm plus a *mode* (up, down, up-down:
a small enum of its own now that `Arpeggiator::Mode` is gone), stepping
through the chord notes entered in the clip and written out as ordinary
notes, the way `songs/arptest1.xml`'s arpeggio is spelled out. It needs
no engine mechanism at all - just:

- `RhythmPatternTemplate` gains an optional arp part (the mode, a
  note-duration/gate, maybe an octave range).
- "Add to Song" finds-or-creates a root instrument track the same way
  it already finds-or-creates a `PercussionTrack`, through its own
  floating target-track picker (`compatibleTargetTrackRows()`/
  `resolveTargetTrackId()`/`targetTrackLabel()`/`openTargetPicker()`,
  added for the rhythm picker above) - generalizing those past their
  current `TrackType::PERCUSSION_CONTROL` hardcoding (a `TrackType`
  parameter, or a small predicate) rather than copy-pasting a second,
  arp-only picker.
- The new clip lands empty of real chord notes (or, optionally, ships
  with a literal example chord progression as ordinary Pattern notes -
  no scale/chord table needed for that, it's just notes) - you enter
  the actual chord tones yourself, same as for the bass-line generator
  above.
- Preview: same Player-side rhythm scheduler, extended to also play the
  generated arpeggio from whatever chord notes the template's arp part (if
  any) supplies for preview purposes, or skipped in preview entirely and
  only wired up once a real clip/chord exists - worth deciding once this
  is actually being built, not now.

Mechanically simpler than the bass-line generator (no new selection
rule, no open chord-source-topology question), but shares the same "the
clip needs real chord notes in it" dependency, which is why it's grouped
down here with it rather than earlier.
