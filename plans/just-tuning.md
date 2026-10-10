# Just intonation, chord extent and whole-song select

Goal: notes in 12/19/31/53-EDO carry a per-note cent correction toward just
intonation, computed by the app over a region or the whole song, stored in the
note's local fx. Chords get spread in the ambisonic space so the tuned
intervals are heard as separate voices. Six phases: 1, 2, 3 and 4 are built; 5 and 6 are decided. The order of work is
5, 6. Phase 3 takes the per-track tuner of phases 1-2 to the whole song and
splits select-all by view; it is all the otonal and utonal scales need. The
arpeggiator (4) went before voice placement (5). Dynamic just intonation across
tracks (6) is last because it is the most involved part and only scales without
a tonic in every chord need it.

## Phase 1 - the `+xx` / `-xx` fx command and key-relative just intonation (built)

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
numerator times denominator). No prime limit is chosen by the user: what is
reachable follows from the EDO and the error cap (12-EDO mostly 5-limit plus
7/5 and 15/14; 31-EDO reaches 7 and 11). A fixed ceiling of prime 13 keeps out
ratios like 31/30, which the simplicity rule alone picked at 31-EDO's first step. The correction is the ratio's cents minus the
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

## Phase 2 - chords and arpeggios (built; phase 6 replaces its model)

`apply-just-intonation-region` is chord-aware; the per-note key-relative
function stays as its fallback and as the anchor.

- **Context = the bar.** Per track and per pattern (a track's background or one
  clip), the notes that start in a bar, plus the notes still held into it (the
  last event in a column before the bar is a pitched note), are one chord. An
  arpeggio is one chord because all its steps start in the same bar; a chord
  held across bars is the context of each. Bars are the song's signature,
  counted from the pattern's row 0.
- **Anchor on the bass.** The chord's lowest note is tuned from the key as in
  Phase 1. Every other note is tuned as its interval above the bass: the
  bass's key ratio times the simplest ratio for that interval, with the
  corrections measured against the equal-tempered pitch. The bass keeps the key
  from drifting between chords, and a chord is pure against its own bass. Both
  of the plan's 31-EDO chords come out as before (6:7:8, 10:11:12:15); a ii
  chord's D F A is 10:12:15 above its D rather than F and A measured from C.
- **Commands.** Apply computes the context from the whole pattern but writes only
  the notes in the region (and the chosen note columns). Transpose gives the
  notes that carried a correction a fresh one, in the same way, once the notes
  have moved. A whole-song transpose also moves the key.
- **Limits, by design.** The context is one track: a bass track and a pad track
  agree on the root (both measure it from the key) but not on the upper
  notes. A chord change inside a bar is read as one chord (the bar is the
  window; `kChordWindowBars`). A held note keeps the correction it got at its
  onset. The 20 cent cap bounds the bass and the interval separately, so a
  note can end up further from its equal-tempered pitch than 20 cents.

## Phase 3 - song-wide scope: scenes and arrangement (built)

The scales the song is written in now, otonal and utonal, need nothing more
than the tuner of phases 1-2. Every chord in them holds the tonic, so every
note is tuned against it: no memory, no context across tracks, no loops, and no
drift. This phase takes that tuner to the whole song and splits select-all by
view. The dynamic tuner (phase 6) comes last, so everything here can be tried
first on those scales.

- **Why there is no drift.** Checked against the table in 31-EDO for the chords
  C D# F, F A C', C E G, G A# C', E G C, A C E and A# C D#: for any two notes of a chord, one tuned
  from the key and the other as the simplest interval above it equals the
  other tuned directly from the key, to 0.00 cents. Phase 2's bass-anchored
  result is therefore the key-relative one for them, and each pattern can be
  tuned alone. It does not hold for every EDO step pair: the table is not
  additive (worst case of reference-plus-interval against direct, 49 cents in
  12-EDO, 46 in 19, 34 in 31, 22 in 53), and across chords only pairs through
  the tonic compose (D# then E is 8.9 cents off, F then A# 16.6). Hence the
  tonic in every chord; a chord set added later gets the same test, and a chord
  without the tonic is outside the guarantee (still pure against its own bass).
- **Scopes.** `SelectionScope::SONG` becomes two. `SCENES` (Live View's select
  all) is every clip of every track, placed or not. `ARRANGEMENT` (Arrangement
  view's) is each track's background pattern and the content of every clip
  that is placed in the arrangement. Region, transpose, humanize, apply and
  clear all take the scope. Percussion is skipped as before.
- **Clip instances need no decision here.** Tuned against the tonic, a note's
  correction does not depend on where its clip is placed or what plays around
  it, so a clip has one tuning that is right in every placement and the
  collapse and per-placement options are not needed. A clip is shared content:
  tuning or transposing it from the arrangement changes it in the clip grid
  too.
- **The limit.** Each correction is bounded by the 20 cent cap on the bass and
  on the interval, so the +-255 error cannot occur in this phase; it arrives
  with the dynamic tuner.
- **Work.** Replace `SONG` and `whole_song_` with the two scopes (a view picks
  one in `mark-whole-buffer`); `forEachSongPattern` takes the scope; docs
  (`docs/terminal.md`, `docs/commands.md`, CLAUDE.md) follow.
- **Tests.** SCENES tunes unplaced clips and leaves backgrounds; ARRANGEMENT
  tunes backgrounds and placed clips and leaves unplaced ones; a clip placed
  twice has one tuning; the four chords above come out as the key-relative
  corrections; select-all picks the scope by view; transpose and humanize take
  both scopes.

## Phase 4 - remove the arpeggiator (built)

The arpeggiator (an `<arpeggiatorTrack>`, `Arpeggiator`/`ArpeggiatorState`)
steps a held chord on a free-running clock of its own. It does not work with
clips correctly, and its timing has needed rounds of fixes of its own. It also
makes the note path non-uniform: its steps are played by the track state
itself, bypassing the per-note override (so a tuning correction or a per-note
azimuth never reaches them), and the chord rings of Phase 5 would have to
special-case it as well. Arpeggios are written as notes instead, which the
just-intonation tuner sees like any other.

Order of work:

1. **Spell out the one song that uses it.** `songs/arptest1.xml`, track `arp`
   (mode up, noteDuration 2 rows, octaves 1, gate 1, 31-EDO, 3/4). Each chord
   (rows 0, 48, ..., 288, held until the OFF at 324) steps through its notes
   and the same notes one octave (31 steps) up, ascending, a step every 2 rows
   from the chord's row, starting again at each chord. Each step is a note
   followed by an OFF a row later (gate 1); the last chord's final step is
   row 322. Replace the track with a plain `<track>` that keeps the
   instrument, position and sends, one note column, 24 steps per 48-row chord
   and 18 for the last. Check by rendering the song before and after
   (`--render`): the onsets line up; the sound is near, not bit-equal, since
   each note's start phase comes from its note coordinate.
   `songs/backup/arptest1.xml` is a stale copy in an older form and goes too.
   `songs/songtest20.xml` only names a plain track "Arpeggio".
2. **Remove the track kind.** Delete `instruments/Arpeggiator.{h,cpp}` and
   `state/ArpeggiatorState.{h,cpp}`, their lines in both `CMakeLists.txt`
   files, the `arpeggiatorTrack` factory entry in `TrackNodes.cpp` and the
   include in `Song.cpp`. A file that still has the element then fails like any
   unknown track element; check the message is clear.
3. **Remove what existed only for it,** after grepping that nothing else uses
   it: `NoteOrigin` and `noteOn()`'s `origin` argument (both callers),
   `InstrumentTrackState::endPatternRow()`, the virtual hooks on
   `noteOn()`/`notePressure()` if nothing else overrides them,
   `TrackState::resyncPlayhead()`, `SongState::resyncPlayheadAfterStop()` with
   its position-edit bookkeeping, and the restart hook in `Player`. Clean the
   comments that name it (`Player`, `LeafTrackState`, `TrackState`,
   `SongState`, `VisibleTrackInfo`, `SongStructure`, `NoteCoordinate`).
4. **Tests.** Delete `ArpeggiatorStateTests.cpp`, the
   `arpeggiator_pattern_chord.xml` fixture and
   `render_arpeggiator_steps_through_a_pattern_authored_chord`; trim what
   mentions it in `ResyncPlayheadTests.cpp`, `PlayerMultiBufferTests.cpp`,
   `HashFieldTests.cpp` and `RenderTests.cpp`. Keep a render test that the
   spelled-out song's arp track plays its steps.
5. **Docs.** CLAUDE.md, `docs/known_bugs.md` (the playhead-resync paragraph),
   `docs/commands.md` (the +hh row says arpeggiator notes are not reached),
   `todo.txt` if its arpeggio line is about this. Delete
   `plans/arpeggiator-timing-fixes.md`; edit `plans/transport-pause.md` (the
   "where an arpeggiator resumes" question goes), `plans/outline-library-
   followups.md` (its bass generator and arpeggio part now write notes into
   the clip instead of using the track kind), `plans/launchpad-custom-mode.md` (the arpeggiator step editor) and
   `plans/instrument-identity-generator-overrides.md` (mentions).

There is no arpeggiate command: an arpeggio is a clip with the notes written out,
looping.

Note on what was built: an `<arpeggiatorTrack>` in a song is refused with
"Unrecognized or malformed <arpeggiatorTrack>", and `songs/arptest1.xml`
renders the same as before (per-row envelope correlation 1.0000, no pitch
differences).

## Phase 5 - voice placement: spatial mode per track, chord extent

Today extent only widens a single voice (an oscillator array's cloud,
`OscillatorVoice.h`; the SoundFont per-feature offsets). The notes of a chord
are separate voices at one azimuth, so a chord never separates. Where a note
sits is also decided in three unrelated places: the percussion key table, the
pitched-arc families (piano, mallets, harp, timpani) and the region pan.
A per-track spatial mode makes that one choice.

### Modes

A leaf instrument track gets `spatial="auto|ring|arc"` (a `LeafTrack` property
next to azimuth/elevation/distance/extent, a string property in
`TrackNodes.h`, written only when not `auto`). Set in the song XML only; no
command or UI for it yet. There is no `point` mode: a chord spread of zero is
just a ring with no extent, and the extent also drives the single voice's own
cloud, so a separate "no spread" mode would only duplicate it.

- `auto` (default): only a default, chosen per instrument. Percussion
  (bank 128 / `Tuning::PERCUSSION`) uses its key table with jitter; the
  SoundFont arc families (piano, mallets, harp, timpani) use the arc; every
  other instrument uses the ring. A track with no extent puts every note at its
  position, as today, so only songs whose pitched tracks already have an extent
  change.
- `ring`, `arc`: an explicit choice that works on every instrument, the ones
  whose `auto` is something else included (a piano on the ring, an organ or a
  drum kit on the arc). Nothing is specific to the instruments that default to
  them.
  - `ring`: each note column gets a fixed slot on a spiral (below).
  - `arc`: the note's key along the extent, low to high. A SoundFont preset
    uses its mapped key range as now; any other instrument, and a kit, a fixed
    A0-C8 span.

### The ring is a spiral

A ring cannot know how many note columns a chord has, and it must not: when a
voice is added the voices already sounding would have to move. So a slot's
place depends only on its own index k, the note column. Slot 0 is the track's
position, and slot k >= 1 is offset by

- angle `phi_k = k * 137.508 degrees` (the golden angle), measured in the
  azimuth/elevation plane of the track's position, and
- radius `rho_k = rho * min(1, sqrt(k / kSpiralFull))`, where `rho` is
  `atan2(extent, distance)` from `-Wxx` or the instrument's default extent and
  `kSpiralFull` (6, by ear) is the slot where the spiral reaches the rim; later
  slots go on round the rim at their golden angles.

Equal area per slot, no two slots ever on the same bearing, a lone note exactly
where the track is, a chord of three or four an open triangle or quad rather
than a line, and nothing moves when a column is added. No scatter: the same
chord lands on the same points every time, so an arpeggio keeps each column on
its own point. The current concentric-ring layout (`OscillatorVoice::ringCounts()`
/`cloudPoint()`, which depends on how many buckets there are) stays where it is
for an oscillator array's own cloud; it is not shared.

A live-played note has no column, so it takes the lowest slot no sounding voice
of the track holds; a voice keeps its slot for as long as it sounds.

The oscillator array's cloud (`spread`) stays centred on the note's slot and
scaled by the voice's own `spread`, not by the chord: scaling it by the number
of columns would make a note sound different as the chord around it changes,
which is the problem the spiral solves. The cloud may overlap its neighbours'
slots; `spread` is set per instrument by ear.

### Where it is applied

`Instrument` gets a virtual `placeNote(position, spatial mode, slot, key)` that
returns the note's position; the track's mode is passed in with each note
(it is a track property, so it reaches the instrument as an argument, not
as instrument state). The default implementation does `auto`: the ring.
`SoundFontInstrument` overrides it for the kit and the arc families, which
today live in its `playNote()`, and `InstrumentTrackState::noteOn()` calls it on
the position after the note override. The slot is the `column` that `InstrumentTrackState::noteOn()` already takes (the
voice id a pattern note's column becomes, which is also how its off finds it
and how a note in the same column retriggers); live-played notes send one in
`PLAY_NOTE`'s second parameter. Two bass drum hits in different columns are two
voices on two slots, on the kit as on any other instrument.

### SoundFont region pan

SF2 generator 17 (`pan`, +-0.5 per region) is an azimuth offset in every mode
and for every instrument, percussion and the arc families included: drop the
`skip_native_pan` exemption (marked TEMPORARY) in `adjustPositionForPan()`.
In `ring` mode it offsets around each slot, so a stereo-split preset (one
sample stored as two regions panned hard left and right) keeps its width at
every chord point.

Watch for doubling: a GM drum kit's per-drum pans and the key table encode the
same thing, as do the arc and a piano's stereo-mic zones. That is accepted for
now; if the kit or arc sounds doubled by ear, put the exemption back for those
two only. The pan's mirror convention was marked "under investigation" and this
does not settle it.

### Docs and tests

`docs/commands.md` (`-Wxx` spreads chords on a `ring` track), a spatial-mode
section next to the position docs, glossary entries. Layout unit tests: slot 0
at the centre, slot positions independent of how many slots there are (the same
for k = 1 whether the chord has 2 or 6 columns), golden-angle spacing, radius
reaching the rim at `kSpiralFull`; `spatial` XML round-trip; a render test
(a `ring` chord with extent has energy on both sides, a track with no extent
does not); percussion unchanged under `auto`; a stereo-split preset keeps its
width at a chord slot.

## Phase 6 - dynamic just intonation: tuning across tracks, with drift and loops (planned, last)

This is for scales whose chords have no common pitch (a major scale, say). The
otonal and utonal scales do not need it: every chord there holds the tonic, so
phases 1-3 already tune it without drift. Phase 2 tunes each track alone, one
chord per bar, and always returns to the key. Neither is acceptable: a track may hold a single note that only gives
body to another track's (a perfect fifth above it, once), a harmony can change
on any row, and the right behaviour differs between the two views. Phase 6
replaces it with one model over every track.

### The chord goes away

A chord is not a unit any more. The tuner walks the pitched notes of all
tracks in time order (percussion excepted) and tunes each against what is
sounding or has just sounded:

- **References.** Per pitch class, the latest tuned pitch of a note of that
  class (octaves count as one: the interval is zero, the simplest there is, so
  doubled notes get identical corrections). A class is a reference while one of
  its notes sounds, on any track and of any length (a held pad stays a
  reference for as long as it holds) and, as a fallback used only when nothing
  sounds, for as long as it is among the last `kTuningMemoryClasses` classes
  touched. The memory counts classes, not
  notes and not time: a chord with octave doublings or a repeated-note ostinato
  cannot push the rest of the harmony out, and a silence cannot cut it, so a rest
  never makes the tuning jump (a reverb tail may still be ringing through it).
  It slides rather than cutting time into windows, so a harmony may change on
  any row, and it is what makes an arpeggio hang together. A class that returns
  while it is remembered keeps its pitch, so a tonic that comes back stays put;
  a chain of stacked fifths is long enough to forget where it started, which is
  what lets it drift. By-ear constant, starting at 6 classes.
- **The pick: sounding first.** A note is tuned against the notes sounding at its
  onset, on any track, which include the notes of its own chord that were tuned
  before it (notes that start together are taken lowest first); the reference
  that gives the simplest interval wins (the table ratio for the pitch-class
  interval, smallest Tenney height; ties go to the more recent, then the
  lower). Only when nothing sounds does the memory answer, with the same pick
  among its classes. The note's tuned pitch is that reference's tuned pitch plus
  the ratio; the correction is the difference to the equal-tempered pitch,
  clamped to the fx range of +-255 cents. Example in C major: C E G, released,
  then D F A. D has nothing sounding, so the memory gives G, a fifth below,
  and D comes out 9/8; F is 6/5 above the sounding D, 27/20; A is 3/2 above it,
  27/16 - a pure minor triad. Without the sounding-first rule F would pick the
  remembered C (4/3, simpler) and D-F would not be pure.
- **Nothing to reference** (the very start, or a free note with nothing tuned
  or fixed before it): the key, as in phase 1. Notes that start together are
  taken lowest first, each against everything already tuned. Phase 2's
  bass-anchored chord is the special case of this, and its 31-EDO examples must
  still come out the same.
- **Free and fixed.** The notes a command writes are free; every other pitched
  note is fixed at its stored pitch (equal-tempered plus its stored correction,
  `+00` when it has none) and is only ever a reference. A region is therefore
  tuned against the rest of the song as it stands, and the neighbours' stored
  corrections are respected, which phase 2 did not do.
- **Drift is real.** A note is tuned against the *tuned* pitch of an earlier
  one, so errors accumulate along a progression: stack a perfect fifth on C ten
  times and each step carries the last one's error. That is intended in the
  arrangement. What happens at the +-255 limit is below.

### Two modes, picked by the view

Select-all splits, and a region uses its view's mode. In Live View
`mark-whole-buffer` selects every scene (`SelectionScope::SCENES`); in
Arrangement view the arrangement (`SelectionScope::ARRANGEMENT`). They replace
`SONG`.

**Order of work: the timeline model and scene mode first, arrangement mode
last,** because it waits on the clip-instance decision below. Until then
Arrangement view keeps phases 2-3 unchanged.

- **Scenes: looping only if a clip loops.** A scene is the clips of one
  clip-list row, on every track, which play together. A track whose slot is
  empty and has no stop button carries on with the previous scene's clip
  (`Clip::hasStopButton()`), so that clip belongs to this scene's context too,
  found by looking back to the nearest earlier clip on the track.
  - *Some clip loops:* the scene is a cycle as long as the longest looping clip,
    the shorter looping ones repeating, and context loops with it: the first
    notes see the end of the cycle as what came before. One-shot clips play on
    the first pass only, so they are tuned there, as part of the context, and
    take no part in closing the loop. A progression does not return to its
    starting pitch, so a compromise is needed: tune the cycle from the key,
    measure how far the end context sits from where it began, and spread that
    difference over the pass so the loop closes (a tempered comma, not a jump at
    the loop point). Intervals then miss just by the spread. Pinned by a test:
    the second pass starts where the first did.
  - *No clip loops:* it plays once, so it is a plain linear timeline from the
    key, free to drift like the arrangement.
- **Arrangement: linear, drifting (last).** One timeline of the whole
  arrangement: each track's background pattern and the clip instances placed on
  it, expanded to absolute rows with loops repeated as they play. The tuner runs
  once from the start. Clip instances are tuned too (see below).

### Clip instances in the arrangement (to decide)

A clip's notes hold one correction each, but one clip may be placed many times
among different surroundings.

1. **Collapse:** tune every placement as independent notes, which means
   un-sharing the clip. `merge-clip-to-background` already does that by hand,
   and stays available for it; doing it automatically would destroy the clips.
2. **One tuning per clip that works in every placement** (recommended first).
   Each clip is tuned as a loop, as in a scene. In the arrangement a placed clip
   is fixed, a reference for the other tracks, and only background notes are
   free. Selecting the arrangement tunes the clips (as loops) and then the
   background around them. No format change; drift builds in the background
   only.
3. **A cents offset per placement.** The clip is tuned once and each placement
   carries an added offset (the drift at that point), a new instance property
   with document, undo and playback changes. A later step if (2) is not enough.

### At the +-255 limit: an error, and nothing changes

A correction is a signed byte, so a tuned pitch cannot be more than 255 cents
from its equal-tempered pitch. That is far: more than half a step in every
tuning (2.5 semitones in 12-EDO, 6.6 steps in 31-EDO). It takes a long
unbroken chain to get there: each pure fifth stacked on the last drifts +2
cents in 12-EDO (about 130 in a row), +5 in 31-EDO (about 50), +7 in 19-EDO
(about 35), almost nothing in 53-EDO; stacked major thirds in 12-EDO drift -14
each (about 19).

Nothing is clamped and nothing is reset. When a note's target would pass the
limit the command stops with an error that names the track and row and the
correction it needed, and the song is left exactly as it was. That holds for
every command that tunes (apply, and the retune after a transpose): the
tuner works on a copy of the timeline, and notes, corrections and key are
written only once the whole run has succeeded, so there is nothing to roll
back. To start again from the key on purpose, tune the song in sections:
corrections that are cleared count as untuned, fixed at their equal-tempered
pitch, so a section after them starts from there.

### Work and tests

- New `model/IntonationTimeline.{h,cpp}`: builds the timeline from patterns and
  instances (`resolveInstanceAt()`), runs the tuner, closes loops.
  `chordCorrections()` and `kChordWindowBars` go; the `PatternBlockOps` entry
  points keep their names. Transpose retunes through the same code.
- `SelectionScope::SONG` becomes `ARRANGEMENT` and `SCENES`; docs and
  CLAUDE.md follow.
- Tests: C E G then D F A in a major key (27/20 and 27/16); the chord C-1 C-2 C-3 C-4 E-3 E-4 G-3 G-4 (every octave copy gets the
  same correction, and the 31-EDO result is phase 2's), then a long run of
  repeated Cs that must not push the E and G out of the memory; a single note
  a fifth above a note on another track; ten stacked fifths
  drift by the sum of their corrections and clamp at +-255; a harmony change in
  mid-bar; a region tuned against fixed neighbours' stored corrections;
  a silence changes nothing, the next note continues from the last tuned ones; a looping scene closes; a clip placed twice has
  one tuning; percussion is ignored; transposing retunes; the limit stops the run with an
  error and leaves song, corrections and key untouched (also for a transpose).

## Open

- Phase 5: `kSpiralFull` (6) and the oscillator array's `spread` against its
  slot are by-ear values, as is the 20 cent cap.
- Phase 5: native pan on percussion and the arc presets may double the key table
  and the arc; check by ear with FluidR3, which this container lacks.
- Phase 5: what column the live sources (keyboard, Launchpad, MIDI) send in
  `PLAY_NOTE`; if it is not a small column index, they take the lowest free slot.
- Phase 6: the memory length (6 pitch classes is a guess); how a scene's cycle is
  built when clip lengths do not divide the longest; and which of the three
  clip-instance options (the plan recommends 2), which gates the arrangement
  mode, built last (phase 3 settles it for tonic-referenced scales).
