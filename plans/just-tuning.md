# Just intonation, chord extent and whole-song select

Goal: notes in 12/19/31/53-EDO carry a per-note cent correction toward just
intonation, computed by the app over a region or the whole song, stored in the
note's local fx. Chords get spread in the ambisonic space so the tuned
intervals are heard as separate voices. Three phases, in this order: 1 and 3 are decided, 2 is a sketch. Voice
placement is last because it is the most involved part.

## Phase 1 - the `+xx` / `-xx` fx command and key-relative just intonation

### Storage and placeholder

- A note's local fx is `+hh` or `-hh`: sign in the mnemonic slot, two hex digits,
  1 cent per unit (+-255, clamped). Playback: `NoteOverride.detune_cents`,
  passed as the existing `detune` frequency ratio (`2^(cents/1200)`) from
  `InstrumentTrackState::noteOn()` to `playNote()`.
- `-` can no longer be the empty placeholder: store `.`, show `·`. Touches
  `Note` (init, `clear`, `hasFx`, `fxCharFor`), the `fx` attribute in
  `Song.cpp` / `PatternView.cpp`, the pattern editor display and typing,
  `PlaybackContent` hashing; convert the repo's songs (no old-format reader).
  The 4-character effect column keeps its dashes for now.
- Tuning overwrites whatever fx a note had (pans move to the track's effect
  column: `-Pxx`, `-Wxx`). It clears `-Rxy` on pitched notes. Percussion is
  never tuned. A tune pass reports how many fx it overwrote.
- A tuned note always carries a sign fx, `+00` when no correction is needed;
  "tuned" means "has a sign fx".
- Lineage for the docs Source column: finetune, ProTracker `E5x` / Impulse
  Tracker `S2x` (a nibble in 1/8-semitone steps); here a byte in cents.

### Algorithm (key-relative)

For a note, the interval from the song key (`Song::getKey()`, the song's EDO)
is matched to a ratio. A candidate ratio's error to the note's EDO pitch must
be within min(half an EDO step, about 20 cents); the 20 cents is a constant to
tune by ear, not a setting. Among candidates the simplest wins (smallest
numerator times denominator). No prime limit is chosen anywhere: the limit
follows from the EDO and the error cap (12-EDO ends up 5-limit plus 7/5;
31-EDO reaches 7 and 11). The correction is the ratio's cents minus the
EDO pitch's cents, rounded.

The function takes the set of sounding pitch classes and returns a ratio per
note, so phase 2 replaces the caller, not the data. Phase 1 passes {key, note}.

Examples to pin as tests (31-EDO, key C): C D♯ F = 6:7:8, corrections
0 / -4 / -5; C C𝄪 E♭ G = 10:11:12:15, corrections 0 / +10 / +6 / +5.
12-EDO, key C: D +4, E -14, F -2, G +2, A -16, B -12.

### Commands and selection

- `apply-just-intonation-region`: in `PatternEditor`, like `transpose-region-*` (never clears
  the mark), block ops `applyJustIntonationBlock` / `applyJustIntonationBlockNotes` in
  `PatternBlockOps`. Covers every pitched note in the region.
- `clear-tuning-correction-region`: removes the `+`/`-` fx from the region's
  notes whatever produced them, so it is not named for just intonation. Same
  scopes as the apply command; `clearTuningCorrectionBlock` /
  `clearTuningCorrectionBlockNotes`.
- Transpose retunes the notes that carry a sign fx (so a C moved to G gets the
  key's perfect fifth). A whole-song transpose also moves `Song::getKey()`, so
  recomputed corrections come out identical (test: tune, transpose song, tune
  again, no change).
- `C-x h`, `mark-whole-buffer`: selects the whole song, no widening. Arrangement
  view: the arrangement (every background pattern and every placed clip).
  Live View: every clip of every track. New `SelectionScope::SONG`, set by the
  command, ended by any cursor move or `keyboard-quit`. The existing region
  model covers only what supplies a track at the anchor row, which cannot
  express this. Tune, transpose and humanize act on it; kill, copy and yank
  say it is not supported.

### Docs and tests

`docs/commands.md` (the `+`/`-` row), `docs/terminal.md` (commands, `C-x h`),
`docs/glossary.md` (just intonation, tuning correction; "tuning" stays the song's EDO), the `Note.h` fx comment. Command
parse/round-trip, block ops, a render test measuring the frequency ratio of a
note at `+64`, SONG scope bounds.

## Phase 2 - chords and arpeggios (sketch)

- Context for a note: the notes sounding with it, held notes, and a short window
  of recent rows so an arpeggio counts as one chord. Pick the simplest chord
  (smallest harmonic numbers) whose members are all within the error cap.
- A correction applies at note-on, so a held note keeps its tuning; new notes
  tune relative to what still sounds. Anchor on the key or the bass to avoid
  drift between chords.
- Transpose-retune then re-runs the chord tuner over the affected region.

## Phase 3 - voice placement: spatial mode per track, chord extent

Today extent only widens a single voice (an oscillator array's cloud,
`OscillatorVoice.h`; the SoundFont per-feature offsets). The notes of a chord
are separate voices at one azimuth, so a chord never separates. Where a note
sits is also decided in three unrelated places: the percussion key table, the
pitched-arc families (piano, mallets, harp, timpani) and the region pan.
A per-track spatial mode makes that one choice.

### Modes

A leaf instrument track gets `spatial="auto|point|ring|arc"` (a `LeafTrack`
property next to azimuth/elevation/distance/extent, a string property in
`TrackNodes.h`, written only when not `auto`). Set in the song XML only; no
command or UI for it yet.

- `auto` (default): what the instrument does today. Percussion (bank 128 /
  `Tuning::PERCUSSION`) uses its key table with jitter; the SoundFont arc
  families use the arc; everything else is `point`. No existing song changes
  its note placement.
- `point`: every note at the track position.
- `ring`: each note column gets a fixed slot on the cloud layout of the
  track's N note columns: N = 1 is the centre, 2 a left/right pair, 3 a
  triangle, 4-6 one ring, more concentric rings. `OscillatorVoice::
  ringCounts()` and `cloudPoint()` already build exactly this; move them to a
  shared header (`ambisonic/CloudLayout.h`) and have both callers use it.
  Radius is `atan2(extent, distance)`, from `-Wxx` or the instrument's default
  extent. No scatter (`scatter_coord = nullptr`): per-note hashed turns would
  rotate the triangle between chord events. The slot is the column index, so
  an arpeggio keeps every column on its own point. N is the widest row of the
  pattern or clip being played (check `PlaybackContent` can give it to the
  audio thread cheaply); live-played notes use their column.
- `arc`: the note's key along the extent, low to high. SoundFont presets use
  their mapped key range as now; other instruments use a fixed A0-C8 span.

Percussion is not selectable: it has no chords, so it stays on its table.
Any pitched instrument can take `ring`, an organ included. A chord on `ring`
or `arc` is spread; on `point` it is not.

Implementation: ring is applied once in `InstrumentTrackState::noteOn()` to
`resolved_position` after the note override. The kit and arc placement live in
`SoundFontInstrument::playNote()`; the mode has to reach them, either as a
parameter on the eight `playNote` overrides or through a small virtual
`placeNote()` on `Instrument` - decide when writing it.

### SoundFont region pan

SF2 generator 17 (`pan`, +-0.5 per region) stays an azimuth offset in every
mode. `adjustPositionForPan()` already does this for pitched presets; drop the
`skip_native_pan` exemption (marked TEMPORARY) so percussion and the arc
families get it too. In `ring` mode the pan offsets around each slot, so a
stereo-split preset (one sample stored as two regions panned hard left and
right) keeps its width at every chord point.

Watch for doubling: a GM drum kit's per-drum pans and the key table encode the
same thing, as do the arc and a piano's stereo-mic zones. If kit or arc sounds
doubled, put the exemption back for those two only. The pan's mirror
convention was marked "under investigation" and this does not settle it.

### Docs and tests

`docs/commands.md` (`-Wxx` spreads chords on a `ring` track), a spatial-mode
section next to the position docs, glossary entries. Layout unit tests (counts,
N = 3 gives three points 120 degrees apart, no scatter), `spatial` XML
round-trip, a render test (a `ring` chord with extent has energy on both sides,
`point` does not), percussion unchanged under `auto`.

## Open

- `auto` leaves non-arc instruments as `point` (assumed above); the variant is
  `ring` whenever the track has a nonzero extent, which changes existing songs.
- Region pan on percussion and arc presets (re-enabled above) may double the
  key table / arc; check by ear with FluidR3, which this container lacks.
- The 20 cent cap and the oscillator-array radius inside a chord are by-ear
  values.
