# Just intonation: scale ratios, then adaptive tuning

Goal: every pitched note in 12/19/31/53-EDO can carry a cent correction toward
just intonation, stored in its local fx (`+hh`/`-hh`) and computed by the app.

Built so far (see `docs/terminal.md`, `docs/commands.md`, `docs/spatial.md`,
`docs/glossary.md`): the `+hh`/`-hh` fx and its playback;
`apply-just-intonation-region` and `clear-tuning-correction-region`; transpose
retuning tuned notes; select-all as `SelectionScope::SCENES` (Live View) or
`ARRANGEMENT` (Arrangement view); the arpeggiator's removal; per-track spatial
modes and the chord spiral. The tuner today (`chordCorrections()`,
`just_intonation::correctionCentsInChord()`) takes the notes that start in a bar,
or are held into it, as one chord, tunes the lowest from the key and every other
note against that lowest one, each through the step table (the simplest ratio
within min(half a step, 20 cents), primes up to 13).

Two phases are left:

1. **Scale ratios** (next): the otonal and utonal scales give each note its
   exact ratio, played together, live as well as written.
2. **Adaptive just intonation** (last): for the major and minor scales, songs
   with no scale, and notes outside a ratio scale. The otonal/utonal work does
   not need it, but writing in the ordinary major and minor scales does.

## Phase 1 - scale ratios for otonal and utonal (next)

### Why the current tuner is not enough for them

The step table guesses which ratio each step stands for, picking the simplest.
An EDO step stands for several ratios a comma apart (31-EDO's step 5 is both 9/8
and 10/9), and when the interval two scale notes actually form is not the
simplest for its step, the guess is wrong. Measured from the tonic the table
happens to give every otonal and utonal note correctly, but the tuner measures
from the bar's lowest note, not the tonic. Every chord containing the tonic
comes out right only while the tonic is lowest (which is why the old check
passed). Voice one with another note lowest and a third of them break: in
31-EDO, of the triads of each scale that contain C with another note lowest,
10 of 30 are wrong. Key C, corrections in cents from `correctionCentsInChord()`:

| Chord (lowest first) | What it is | Wrong note | Gets | Scale ratio gives |
|---|---|---|---|---|
| G C E A | C6 (12:15:18:20) over G | A | +16 (27/16) | -6 (5/3) |
| F A C E | F major 7, otonal notes | E | -17 (13/7 over F) | -1 (5/4) |
| G C F | Csus4 over G | F | +16 (27/20) | -5 (4/3) |
| E C D♯ | otonal | D♯ | -13 | -4 (7/6) |
| E♭ F C | utonal | F | +16 | -5 (4/3) |
| E𝄫 G C | utonal | G | -11 (13/10 over E𝄫) | +5 (3/2) |
| G A♭ C | utonal | A♭ | +17 | +1 (8/5) |

Each time the interval from the lowest note to the wrong one (G-A 10/9, F-E
15/8) is not the simplest ratio of its step. Chords without the tonic fare worse:
34 of the 60 voicings of each scale's tonic-free triads are wrong (F G A♯,
16:18:21, in all three); only the pure ones, E G A♯ (5:6:7) and E𝄫 F A♭
(1/7:1/6:1/5), survive every voicing, because each interval inside them happens
to be the simplest of its step. The bar is also per track: a
melody G-A in one bar gets the 27/16 A even while another track holds C, since
the tuner never sees the other track. So the same pitch class lands up to a
comma apart depending on voicing and on what else its track plays in the bar.

The otonal and utonal scales are fixed just scales: each note has one ratio to the tonic, and every interval between
two of them is the quotient of their ratios. Nothing needs guessing.

Also missing: the two scales are meant to be played together (C E G and
C E♭ G side by side), but a song has one scale; and live notes (pads, MIDI,
keyboard) are never corrected, so what is played is not what is written.

### The ratios

`Scale.h` gets a ratio for each degree next to its name:

| Scale | Names | Ratios | Harmonic numbers |
|---|---|---|---|
| otonal | C D♯ E F G A A♯ | 1/1 7/6 5/4 4/3 3/2 5/3 7/4 | multiples 12:14:15:16:18:20:21 of a low C |
| utonal | C E𝄫 E♭ F G A♭ B𝄫 | 1/1 8/7 6/5 4/3 3/2 8/5 12/7 | a high C divided by 24, 21, 20, 18, 16, 15, 14 |

They are mirror images built from the same numbers. Major, minor and none get
no ratios.

The step a degree sits on still comes from its name (`Note::stringToKey()`, as
`Song::getScaleDegreesWindow()` does), so the Launchpad keyboard, the step view
and the tuner agree on which key is which note. The correction is the ratio's
cents minus that step's equal-tempered cents. Pinned as tests:

| EDO | Otonal | Utonal |
|---|---|---|
| 12 | 0 -33 -14 -2 +2 -16 -31 | 0 +31 +16 -2 +2 +14 +33 |
| 19 | 0 +14 +7 -7 +7 0 +21 | 0 -21 0 -7 +7 -7 -14 |
| 31 | 0 -4 -1 -5 +5 -6 +1 | 0 -1 +6 -5 +5 +1 +4 |
| 53 | 0 -5 +1 0 0 +1 +18 | 0 +5 -1 0 0 -1 +5 |

(In order of the degrees above. 53-EDO's A♯ is a step below the nearest step to
7/4; see Open.)

### One pair, played together

A song whose scale is otonal or utonal is tuned with the ratios of both: 11
notes in 31 and 53-EDO (C, E𝄫, D♯, E♭, E, F, G, A♭, A, B𝄫, A♯), the shared
1/1, 4/3 and 3/2 agreeing. Any mix of the two is then pure: C E G, C E♭ G,
F A C D♯ (an otonal chord on F, 4:5:6:7), B𝄫 C E♭ G (a utonal chord on G), each
interval the quotient of the two ratios. In 12 and 19-EDO the two scales share
keys (12: D♯/E♭ and A/B𝄫; 19: D♯/E𝄫 and A♯/B𝄫); there the song's own scale
takes the step and the other scale fills only the steps left free.

### The rule

A pitched note whose interval from the key (octaves ignored) is one of those
steps gets that ratio's correction: no chord, no bar, no neighbours, no drift.
That also makes a clip's tuning independent of where it is placed. Every other
note (outside the 11, or any note in a song with no ratio scale) is tuned as
today until phase 2. When such a note's chord bass is a scale note, the bass's
exact ratio is what it is tuned against.

### Live notes

Pads, MIDI and keyboard note-ons on a pitched track sound with the scale
correction, so playing is heard as it will be written. `PLAY_NOTE` carries no
correction now; `Player` computes it from the published song's tuning, key and
scale and passes it the way `SongState` passes a pattern note's
`NoteOverride::detune_cents`. Notes outside the scale stay equal-tempered live
(nothing to tune them against in real time). A recorded scale note is written
with its `+hh`/`-hh` so playback matches; others are written untuned, as now.

### Work

- `model/Scale.h`: the ratio per degree, and the pair rule (which steps take
  which ratio for a tuning, key and scale) in one function that both the tuner
  and the live path call.
- `model/JustIntonation.{h,cpp}`: `scaleCorrectionCents(tuning, scale, key,
  note_value)` returning the correction or nothing.
- `PatternBlockOps.cpp`: `chordCorrections()` takes the scale correction first
  for each note, the bass included; the transpose retune goes through the same.
- Live: `Player`'s `PLAY_NOTE`, and the recording paths (`ClipPlayer` takes,
  note capture, pattern-editor note entry) write the fx.
- Docs: `docs/glossary.md` (the otonal/utonal paragraph: their ratios, the
  pair), `docs/terminal.md` (what apply does in a ratio scale, live
  correction), the `Scale.h` comment, the CLAUDE.md just-intonation paragraph.

### Tests

The correction table above for every EDO; every triad of the 11 notes, in
every voicing, giving the scale ratios (this covers the chords above, the ones
without the tonic, and otonal and utonal notes mixed); C E G and C E♭ G in one bar of a 31-EDO otonal song; the 12 and
19-EDO collision rule; a major-scale song unchanged; transpose keeps scale
notes on their ratios; a live note's detune in `Player`; a recorded scale note
carries its fx.

## Phase 2 - adaptive just intonation: tuning across tracks, with drift and loops (last)

For scales where no single pitch per note suits every chord: the major and minor
scales most songs are written in (D is 9/8 in one chord and 10/9 in another),
songs with no scale, and the notes of an otonal/utonal song that are not among
its 11. It replaces the current tuner's per-track, bar-long chords: a track may
hold a single note that only gives body to another track's (a perfect fifth
above it, once), a harmony can change on any row, and the right behaviour
differs between the two views. One model over every track replaces it. A note
with a scale ratio (phase 1) is never free: it is fixed at its ratio and serves
only as a reference.

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
  touched. The memory counts classes, not notes and not time: a chord with
  octave doublings or a repeated-note ostinato cannot push the rest of the
  harmony out, and a silence cannot cut it, so a rest never makes the tuning
  jump (a reverb tail may still be ringing through it). It slides rather than
  cutting time into windows, so a harmony may change on any row, and it is what
  makes an arpeggio hang together. A class that returns while it is remembered
  keeps its pitch, so a tonic that comes back stays put; a chain of stacked
  fifths is long enough to forget where it started, which is what lets it
  drift. By-ear constant, starting at 6 classes.
- **The pick: sounding first.** A note is tuned against the notes sounding at
  its onset, on any track, which include the notes of its own chord that were
  tuned before it (notes that start together are taken lowest first); the
  reference that gives the simplest interval wins (the table ratio for the
  pitch-class interval, smallest Tenney height; ties go to the more recent, then
  the lower). Only when nothing sounds does the memory answer, with the same
  pick among its classes. The note's tuned pitch is that reference's tuned pitch
  plus the ratio; the correction is the difference to the equal-tempered pitch.
  Example in C major: C E G, released, then D F A. D has nothing sounding, so
  the memory gives G, a fifth below, and D comes out 9/8; F is 6/5 above the
  sounding D, 27/20; A is 3/2 above it, 27/16 - a pure minor triad. Without the
  sounding-first rule F would pick the remembered C (4/3, simpler) and D-F would
  not be pure.
- **Nothing to reference** (the very start, or a free note with nothing tuned
  or fixed before it): the key. The current bass-anchored chord is the special
  case of this, and its 31-EDO examples (C D♯ F = 0/-4/-5, C C𝄪 E♭ G =
  0/+10/+6/+5) must still come out the same.
- **Free and fixed.** The notes a command writes are free; every other pitched
  note is fixed at its stored pitch (equal-tempered plus its stored correction,
  `+00` when it has none) and is only ever a reference, as are scale-ratio
  notes. A region is therefore tuned against the rest of the song as it stands,
  and the neighbours' stored corrections are respected, which the current tuner
  does not do.
- **Drift is real.** A note is tuned against the *tuned* pitch of an earlier
  one, so errors accumulate along a progression: stack a perfect fifth on C ten
  times and each step carries the last one's error. That is intended in the
  arrangement. What happens at the +-255 limit is below.
- **Live notes** stay as phase 1 leaves them; adapting while playing is not
  planned.

### Two modes, picked by the view

A region and select-all use their view's mode (`SCENES` in Live View,
`ARRANGEMENT` in Arrangement view, already built).

**Order of work: the timeline model and scene mode first, arrangement mode
last,** because it waits on the clip-instance decision below. Until then
Arrangement view keeps the current tuner.

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
- Docs: the glossary's adaptive entry stops saying "planned"; `docs/terminal.md`
  and CLAUDE.md describe the tuner.
- Tests: C E G then D F A in a major key (27/20 and 27/16); the chord C-1 C-2
  C-3 C-4 E-3 E-4 G-3 G-4 (every octave copy gets the same correction, and the
  31-EDO result is the current tuner's), then a long run of repeated Cs that
  must not push the E and G out of the memory; a single note a fifth above a
  note on another track; ten stacked fifths drift by the sum of their
  corrections; a harmony change in mid-bar; a region tuned against fixed
  neighbours' stored corrections; a silence changes nothing, the next note
  continues from the last tuned ones; an out-of-scale note in an otonal song
  tuned against the fixed scale notes around it; a looping scene closes; a clip
  placed twice has one tuning; percussion is ignored; transposing retunes; the
  limit stops the run with an error and leaves song, corrections and key
  untouched (also for a transpose).

## Open

- Phase 1: 53-EDO spells 7/4 as A♯, a step below its nearest step, so it carries
  +18 cents. Fine within the fx range; respelling the degree would move the
  Launchpad key too.
- Phase 1: the Launchpad keyboard for the 11 notes. In 31-EDO only 11-13 of the
  64 pads land on a note of one scale; moving the tonic pad raises that to 18,
  and no other isomorphic step pair does better than 19. An in-key layout (rows
  of scale degrees) would use every pad but is no longer isomorphic.
- Phase 2: the memory length (6 pitch classes is a guess); how a scene's cycle is
  built when clip lengths do not divide the longest; and which of the three
  clip-instance options (the plan recommends 2), which gates the arrangement
  mode, built last.
- Spatial modes: `kSpiralFull` (6) and the oscillator array's `spread` against
  its slot are by-ear values, as is the 20 cent cap.
- Spatial modes: native pan on percussion and the arc presets may double the key
  table and the arc; check by ear with FluidR3, which this container lacks.
- To settle at the end: which slot a drum or a live note takes on the ring. The
  slot is the `NoteCoordinate` column, which each source fills its own way: a
  pattern note its authored column; keyboard, Launchpad note mode and MIDI the
  lowest free column; and the Launchpad step-grid audition (the sound played
  once when a step is switched on in the step view) the note value, because
  `PLAY_NOTE` and `STOP_NOTE` also use that column as the voice key and a step
  press has no pattern column its release could find. So on a track set to
  `ring`, one drum can sit at three different points: auditioned from a step,
  played on the pad, and played back from the pattern. A kit on `auto` uses its
  key table and ignores the slot, so this matters only for a percussion track
  set to `ring`. One idea: the pad's offset in the 4x4 rack (note - 36, 0 to 15)
  as the slot in the audition and on the playing surface. That makes the first
  two agree but not the playback, which would still use the pattern column; a
  consistent answer needs the percussion slot to come from the note value in the
  placement itself, or the voice key to be separate from the slot. Pitched tracks
  have no rack to take an offset from.
