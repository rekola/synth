# Glossary

Terms with a specific meaning in this project.

**Aftersound**
The slow tail after a piano note's fast first decay, from the strings'
out-of-phase motion draining the bridge more slowly.

**Clip launcher**
The grid of clips inside Live View: one column per track, one row per scene,
each slot triggered on the next bar. On a Launchpad it is the pad grid in the
Live mode (the device's Session button). See launchpad.md and scenes.md.

**Groove**
The general word for rhythmic feel: how a pattern's timing (and often
accents) deviates from a rigid grid. Swing is the simplest groove. Other
DAWs go further. Ableton's *Groove Pool* holds templates of per-position
timing and velocity that you apply to clips. Renoise's *Groove* panel is
closer to this project: four per-line amounts that repeat every four lines
and apply song-wide at playback, without changing the pattern data. Here,
"groove" means timing feel only, and for now that is just swing. The
Library's drum patterns are not grooves in this sense; see Rhythm.

**Just intonation**
Tuning notes to simple whole-number frequency ratios (3/2 for a fifth, 5/4
for a major third) instead of equal steps. Intervals tuned that way have no
beating between their partials and sound pure. Every EDO only approximates
these ratios, so a song's notes stay on its EDO grid and each one carries a
small tuning correction, in cents, that nudges it toward the pure ratio; see
terminal.md.

The simplest way to have it is *static*: every note gets one fixed ratio to the
key. That works in any EDO, 12 included, and every interval between any two
notes is then a whole-number ratio. What it cannot promise is that every chord
comes out as simple as it could. In a major scale, the D that suits a G major
chord is a little too high for a D minor chord, and one fixed D has to be a
compromise.

The 7-note otonal and utonal scales are different. They are named after
Partch's overtone and undertone series but are not his much longer scales:
each has seven notes, chosen so that the intervals between them are simple
ones. They have no D to argue over, so one fixed pitch per note serves.

**Adaptive just intonation**
Just intonation that follows the music instead of returning to the key; also
called adaptive tuning. It is for music where no single pitch per note suits
every chord, such as the major scale above, where D wants to be 9/8 in one
chord and 10/9 in another. Tuning each bar on its own against the key makes
bars that are pure in themselves but do not fit their neighbours: a note shared
by two chords, or one that moves by a step, lands on slightly different pitches
each time. The adaptive kind tunes each note against what is sounding, or has
only just sounded, in any track, so a melody over a bass line is pure against
that bass line, and an arpeggio hangs together although its notes never sound
at once. It is planned, not yet available. The 7-note otonal and utonal scales
do not need it.

The price is *drift*. A pure interval is exact only against its own reference,
and the reference may itself have been tuned against an earlier note, so errors
accumulate along a progression. Four pure fifths stacked from C reach an E about
21.5 cents above the pure major third over C, and a song that keeps every chord
pure can end up noticeably higher or lower than where it began. This is not a
defect: pure fifths, pure thirds and a closed circle of keys cannot all be had
at once. Drift is fine in an arrangement, where the music is free to wander.
Tuning against the key every bar never drifts.

**Live sequencer**
A tool used to program, trigger and manipulate musical patterns, notes and
rhythms in real time during a performance. Here that is Live View: clips are
launched, stopped and recorded into while the transport runs, changes land on
the next bar, and scenes launch a whole row of clips together. What is played
can also be placed into the arrangement.

**Live View**
The view with the clip launcher, the outline panel and the pattern editor
showing each track's own clip; Tab switches to and from the arrangement
view. The Launchpad's Live mode shows the same clip grid on the pads.

**Rhythm**
A pre-written drum pattern in the Library, such as Waltz, Funk or Bossa
Nova. It is content (which drum hits fall on which rows), where swing is
feel (when those rows sound), so the same rhythm can be played with or
without swing. A few (Swing, Boogie, Jazz Waltz) carry a swing of their own,
heard when previewed and adopted by the song on Add to Song. Named Rhythm, not Groove, to keep clear of the DAW sense of
groove above.

**Row**
This engine's grid step: one sixteenth note, so a 4/4 bar is 16 rows and a
beat 4 (other time signatures: see time_signatures.md). Notes sit on rows; a note's delay
column shifts it by a fraction of a row.

**Scene**
A row of the clip grid: the clips at one position across every track's clip
list, launched together. It has no object of its own beyond an optional
name, tempo and time signature stored by position. See scenes.md.

**Spatial mode**
How a track places its notes around its position: `auto` (the instrument
decides), `ring` (a spiral by note column) or `arc` (by pitch). See spatial.md.

**Strike point**
Where a hammer or pluck meets the string, as a fraction of its length. Modes
with a node there are not excited: at 1/8, the 8th, 16th and 24th partials.

**Swing**
A timing feel that delays the second note of each eighth-note pair (4 rows),
giving a long-short, lilting feel. Stored as a percentage of the pair taken
by its first note: 50% is no swing (the second note falls exactly halfway
through the pair), about 67% is triplet swing (jazz, shuffle, boogie), and
75% is very heavy. The 50-75% range and what the percentage means follow the
Akai MPC's swing. Only the second note of a pair moves; on-beat notes
never do. A song-level setting (`Song::getSwing()`, `<song swing="">`)
applied at playback time to everything scheduled against it, not baked into
note data.

**Tuning correction**
A note's pitch offset in cents, stored in its fx as `+hh` or `-hh`
(commands.md). Not the song's tuning, which always means the EDO
(12/19/31/53 steps per octave).

**Unison**
The strings struck together for one piano key, tuned a cent or so apart.
Their slow beating, and their slightly different decay rates, give a piano
note its shimmer and its double decay. See additive.md.
