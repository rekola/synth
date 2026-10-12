# Just intonation: the Launchpad keyboard, then adaptive tuning

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

Scale ratios are built too: in an otonal or utonal song a note of either
scale gets its exact ratio whatever its chord (`scaleDegreeRatios()`,
`just_intonation::ScaleCorrections`, both scales together, the song's own first
where they share a step in 12/19-EDO); live notes sound with it
(`SongScalars::scale_corrections`, `Player`'s `PLAY_NOTE`) and recorded or typed
notes are written with it (`tuneNewNote()`). Other notes are tuned by the chord
tuner, against the exact ratio when the chord's lowest note is a scale note.

Left: the Launchpad keyboard for these scales (a decision, below) and adaptive
just intonation for everything without scale ratios.

## Launchpad keyboard: chromatic or in-key (to decide)

The pitched Note mode keyboard is isomorphic over every step of the tuning
(`resolveKeyboardNotes()`). In 31-EDO only 11-13 of its 64 pads land on a note
of one 7-note scale (20 on the 11 notes of both), and no other pair of
isomorphic steps does much better for one scale (at most 19, checked over every
step pair and tonic position). The tonic pad stays where it is. The alternative is an in-key
layout that shows only scale notes.

**What existed.** An in-key keyboard was built and then replaced
(rekola/synth#33, then rekola/synth#39): pad (0,0) the tonic at the device's octave, each
column the next degree of `Song::getScaleDegreesWindow()`, each row three
degrees (a fourth) higher, `Scale::NONE` playing as major. rekola/synth#39 made the
keyboard always isomorphic and left the scale to the step view, where 91/92
move the edited sound one scale degree. The step view has no in-key pads of its
own; its bottom rows are the same isomorphic keyboard. So the in-key layout is
gone from the code, but restoring it is small: `getScaleDegreesWindow()` is
still there, and only `resolveKeyboardNotes()` needs a second branch.

**Is the old one suitable?** For major and minor, yes: 7 degrees, rows a
fourth apart, every pad a scale note, four octaves on the grid. For
otonal and utonal played together it is not: it lays out only the song's own 7
notes, so an otonal song's E𝄫, E♭, A♭ and B𝄫 (the utonal notes) cannot be
played at all. For a just scale it should lay out the notes of both, the same
11 the tuner uses:

| 31-EDO step | 0 | 6 | 7 | 8 | 10 | 13 | 18 | 21 | 23 | 24 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Note | C | E𝄫 | D♯ | E♭ | E | F | G | A♭ | A | B𝄫 | A𝄰 |

Each row is 8 of them; the next row starts a fourth higher, which is 5 degrees
here (F is the sixth note), so a row overlaps the next by three notes and the
grid spans about 3.8 octaves. In general the row stride is the number of
degrees below the fourth (3 for a 7-note scale, 5 for the 11, 4 for the 9 that
remain where the scales share steps in 12 and 19-EDO). The cost of any in-key
layout is that it is not isomorphic: a chord's shape depends on which degree
it starts on. Pad colours stay the consonance colours by pitch class.

**Switching on the Launchpad.** A repeated press of Note (CC96) while the
device is already in Note mode toggles chromatic and in-key, the same way a
repeated Session (CC95) press toggles the mixer submode; any other mode's press
of 96 still just enters Note mode in whichever layout it was left. Per device,
like the octave and the grid mode. The Note LED shows which: the current
colour for chromatic, a second hue for in-key (as 95 shows green or orange).
Shift + 96 stays open/close the selected clip. In the step view (where 96 is
dark today) the toggle could apply to the bottom four rows too. The terminal
gets no command: only a pad controller has the keyboard, so it would be a
`pad-` command at most.

**To decide.**
- In-key for a just scale: both scales' 11 notes (recommended), or only the
  song's own 7.
- `Scale::NONE` in-key: play as major (as before), or keep the toggle off with
  a status message.
- Whether the toggle also applies in the step view's bottom rows.
- The in-key LED colour for CC96.

## Adaptive just intonation: tuning across tracks, with drift and loops

For scales where no single pitch per note suits every chord: the major and minor
scales most songs are written in (D is 9/8 in one chord and 10/9 in another),
songs with no scale, and the notes of an otonal/utonal song that are not among
its 11. It replaces the current tuner's per-track, bar-long chords: a track may
hold a single note that only gives body to another track's (a perfect fifth
above it, once), a harmony can change on any row, and the right behaviour
differs between the two views. One model over every track replaces it. A note
with a scale ratio is never free: it is fixed at its ratio and serves
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
- **Live notes** keep only their scale corrections; adapting while playing is not
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

- Adaptive: the memory length (6 pitch classes is a guess); how a scene's cycle is
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
