# Glossary

Terms with a specific meaning in this project.

**Aftersound**
The slow tail after a piano note's fast first decay, from the strings'
out-of-phase motion draining the bridge more slowly.

**Aftertouch**
The pressure on a key or pad held down after its note has started, a control
signal that changes while the note sounds (a pattern row of it carries the
pressure as its velocity). Not the aftersound: that is a property of the
sound of a piano note, aftertouch is something the player does.

**Chord**
Three or more notes sounding together, such as a triad (a root with its third
and fifth, like C E G). Not to be confused with a keyboard
*shortcut* (a key with its modifiers, `KeyStroke` in the code) or a *key
sequence* (several shortcuts in a row, such as `C-x C-s`).

**Clip launcher**
The grid of clips inside Live View: one column per track, one row per scene,
each slot triggered on the next bar. On a Launchpad it is the pad grid in the
Live mode (the device's Session button). See launchpad.md and scenes.md.

**Gamut**
The complete set of notes available to a system of music, from which
its scales are drawn. The word is Guido of Arezzo’s (c. 991–after
1033), who named the lowest note of his system Γ, gamma, and sang it
as ut; the whole range from gamma-ut upward became the gamut, first
the full scale of medieval music and then any complete range. The
twelve chromatic notes are the gamut behind the tempered major and
minor scales, the 22 śruti behind the scales of Indian music, the 24
quarter-tones behind the maqāmāt. A gamut may be fixed, every note
having one ratio to the tonic; may hold pairs of notes a comma apart,
as the śruti do; or may be tempered, with no ratios at all. A just
major scale with free modulation has no gamut, only an unbounded
lattice. In the application a gamut is a rule for tuning every key, a
set of accidentals, and the scales drawn from it. Every scale names its gamut,
so choosing the scale chooses the gamut and the song stores nothing else: the
just-intonation tuner tunes any gamut note a song uses, whether or not it is
in the scale, and the scale decides the keys where two gamut notes share a
step. Keys with no gamut note stay tempered, and a scale with no gamut is
tempered throughout. The duodene (Ellis's twelve 5-limit notes) is the gamut of
major and minor; the superparticular gamut (see otonal and utonal) is
generated in rounds of division and is just; the quartertone gamut (see
maqāmāt) is 24-EDO and has no ratios, so the maqamat have none to tune.

**Groove**
The general word for rhythmic feel: how a pattern's timing (and often
accents) deviates from a rigid grid. Swing is the simplest groove. Other
DAWs go further. Ableton's *Groove Pool* holds templates of per-position
timing and velocity that you apply to clips. Renoise's *Groove* panel is
closer to this project: four sliders, each delaying the second line of a pair,
so the pattern repeats every eight lines (at four lines per beat), applied
song-wide at playback without changing the pattern data. Here,
"groove" means timing feel only, and for now that is just swing. The
Library's drum patterns are not grooves in this sense; see Rhythm.

**Just intonation**
Tuning notes to simple whole-number frequency ratios (3/2 for a fifth, 5/4
for a major third) instead of equal steps. Intervals tuned that way sound
pure: for sounds whose partials are whole-number multiples of the fundamental,
as most pitched instruments' nearly are, the partials two notes share coincide
and do not beat. Every EDO approximates these ratios (only its octaves are
exact), so a song's notes stay on its EDO grid and each one carries a
small tuning correction, in cents, that nudges it toward the pure ratio; see
user-interface.md.

The application gives every key one fixed ratio to the key of the song, taken
from the gamut of the song's scale. That works in any EDO, 12 included, and
every interval between any two notes is then a whole-number ratio. What it
cannot promise is that every chord comes out pure. In a major scale, the D
that suits a G major chord is a little too high for a D minor chord, and one
fixed D has to be a compromise: in C major D-A is 40/27, a comma flat of the
fifth. No tuning of a fixed set of notes avoids a comma somewhere.

The otonal and utonal scales (below) are different. They have no D to argue
over, so one fixed pitch per note serves: the scale gives it (otonal 1/1 7/6
5/4 4/3 3/2 5/3 7/4, utonal 1/1 8/7 6/5 4/3 3/2 8/5 12/7), whatever the chord,
and a song in either scale is tuned with both, so the two can be played
together.

**Adaptive just intonation**
Just intonation that follows the music instead of returning to the key; also
called adaptive tuning. It is for music where no single pitch per note suits
every chord, such as the major scale above, where the D wants to differ
between a G major chord and a D minor chord. The adaptive kind tunes each note against what is sounding, or has
only just sounded, in any track, so a melody over a bass line is pure against
that bass line, and an arpeggio hangs together although its notes never sound
at once. It is planned, not yet available; when it comes it shifts the whole scale by a
comma on a switch, which is not tuning a chord against its bass. The otonal
and utonal scales do not need it.

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

**Maqāmāt**
The scales of the quartertone gamut, the modern Arabic system of 24
equal quarter-tones standardised in Cairo in 1932. A maqām (plural
maqāmāt) is a seven-note scale built from two tetrachords, ajnās; in
practice it also carries a melodic path, a dominant and customary
modulations, of which the application holds only the scale. The gamut
is tempered, so the just-intonation tuner leaves its keys as they are;
the neutral degrees at 150 and 350 cents stand where the medieval
frets put 12/11 and 27/22. Accidentals: half-flat 𝄳 and half-sharp 𝄲
(one quarter-tone), ♭ and ♯ (two). The scales are Rāst (C D E𝄳 F G A
B𝄳, given on C), Bayātī, Ḥijāz, Nahāwand, Kurd, ʿAjam, Ṣabā and Sīkāh.

**Narrow major and narrow minor**
Two triads that divide the perfect fourth the way major and minor
divide the fifth. A major triad (4:5:6) places its third at the
arithmetic mean of the fifth, a minor triad (10:12:15 = 1/6:1/5:1/4)
at the harmonic mean; a narrow major triad (6:7:8) places its middle
note at the arithmetic mean of the fourth, a narrow minor triad
(21:24:28 = 1/8:1/7:1/6) at the harmonic mean. Both consist of a
subminor third (7/6) and a supermajor second (8/7), with the larger
interval on the bottom in narrow major and on top in narrow minor, as
with major and minor. Neither has a fifth: the outer interval is a
fourth, so they cannot be mistaken for incomplete seventh chords. In
the otonal and utonal scales they fill the degrees that have no major
or minor triad. C otonal has narrow major on C (C D♯ F) and G (G A♯ C)
and narrow minor on A♯ (A♯ C D♯); C utonal mirrors this with narrow
minor on C (C E𝄫 G) and F (F B𝄫 C) and narrow major on B𝄫 (B𝄫 C
E𝄫). Narrow major is otonal and narrow minor utonal, in Partch’s
sense. The names are the author’s own; the chords have no settled
name in the just-intonation literature. See otonal and utonal.

**Otonal and utonal**
Two scales that mirror each other around the tonic, built by dividing
intervals at their arithmetic and harmonic means. Both are given here
on C. Dividing the octave C–C′ gives G (arithmetic mean) and F
(harmonic mean); dividing each new note’s interval to the nearer of C
and C′ gives four notes a step, of which the superparticular results
are kept. Otonal keeps the arithmetic means. Its 7-note form is 1/1
7/6 5/4 4/3 3/2 5/3 7/4, on C: C D♯ E F G A A♯↑, all harmonics (12,
14, 15, 16, 18, 20, 21) of one fundamental, so the scale is otonal in
Harry Partch’s sense; the fundamental is not the tonic but the note a
fifth below it (F for C otonal), the tonic sitting on identity 3. Its
chords are the two 4:5:6:7 chords C E G A♯↑ and F A C D♯, each read
from every one of its notes, so that every degree has a chord and
every chord contains the tonic; the major triad 4:5:6 and the narrow
major triad 6:7:8 are these chords with a note removed. Utonal keeps
the harmonic means and is the otonal scale turned upside down: 1/1 8/7
6/5 4/3 3/2 8/5 12/7, on C: C E𝄫 E♭ F G A♭ B𝄫, undertones of the note
a twelfth above the tonic (G for C utonal). Its chords are the
mirrors, E𝄫 F A♭ C and B𝄫 C E♭ G (1/7:1/6:1/5:1/4), with the minor and
narrow minor triads as their subsets. A further step of divisions
extends both scales to 11 notes. Partch’s Otonalities and Utonalities
are chords, not scales; these scales and their chords are the author’s
own. Their chords are tabled in otonal-and-utonal.md.

**Performance effect**
An effect held on a track from the Launchpad's Custom grid (stutter,
shuffle, erode, shatter, gate, crush, duck, a send throw, orbit): it
lasts while the pad is held and never changes the song. See
launchpad.md.

**Pythagorean gamut, Pythagorean Mixolydian and nīd qablim**
The Pythagorean gamut is thirteen notes of the chain of fifths, six below
the tonic and six above, octave-reduced (all 3-limit). Pythagorean Mixolydian
selects C D E F G A B♭ from it: 1/1 9/8 81/64 4/3 3/2 27/16 16/9. A scale's
name carries its gamut's, so a 5-limit Mixolydian can be added beside it. The
gamut is for 53-EDO only: its fifth is almost exact and its spellings write
the Pythagorean E, A and B♭ as E↑, A↑ and B♭↓. 31-EDO cannot tune it under
any spelling (81/64 lies between its steps 10 and 11), and 12 and 19-EDO
approximate it as they do every just ratio. The scale will take the
Babylonian tuning's name once that mapping is verified. Nīd qablim is its
Phrygian rotation, C D♭ E♭ F G A♭ B♭ = 1/1 256/243 32/27 4/3 3/2 128/81 16/9
(spelled C♯↑ E♭↓ A♭↓ B♭↓ in the gamut), also 53-EDO only.

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

**Song notes**
Free text saved with a song as its `<notes>` element: where the music comes
from, what was chosen in realising it. It has no effect on playback.

**Spatial mode**
How a track places its notes around its position: `auto` (the instrument
decides), `ring` (a spiral by note column) or `arc` (by pitch). See spatial.md.

**Standard pitch**
The frequency of A4 that every note is tuned from: 440 Hz unless the song
sets another (`Song::getStandardPitch()`, `<song standardPitch="">`, any
positive value). The song's pitch offset (`YUxx`/`YDxx`, commands.md) moves
it at playback, never the setting itself. Not the tuning (the EDO).
Each sample-track take remembers the standard pitch it was recorded at
(`<sample originalStandardPitch="">`, 440 for older takes) and is
pitch-shifted from it to the song's when it plays.

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
(12/19/24/31/53 steps per octave).

**Unison**
Several sounds at the same pitch at once; as an interval, 1:1. Exactly alike
they would only be louder, so a unison's character comes from small
differences: a few cents of pitch, which beat slowly, and differences in
timing and decay. Here it appears as:
- instruments or tracks playing the same notes;
- an `<oscillator>` with several `voices` at `ratio` 1, spread over `detune`
  cents and each drifting on its own;
- detuned copies of an instrument layered in a `<group>` (the `detune` of
  `<fm>` and `<padsynth>`);
- the strings struck together for one piano key (`unisonVoices` and
  `unisonDetune` of `<additive>`), a cent or so apart on a real piano and
  2.5 cents in the `piano` preset. Their beating and their slightly different
  decay rates give a piano note its shimmer and its double decay; see
  additive.md.
