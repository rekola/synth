# Glossary

Terms with a specific meaning in this project.

**Adaptive just intonation**
Just intonation that follows the music instead of holding every note at one
ratio to the key; also called adaptive tuning. It tunes each note against
what is sounding, or has only just sounded, so a melody over a bass line is
pure against that bass line, and an arpeggio hangs together although its
notes never sound at once. It is meant for music where no single pitch per
note suits every chord: in a major scale the D wants to differ between a G
major chord and a D minor chord (see just intonation). The application does
not have it. Its just intonation gives every note one fixed ratio to the
tonic, and the otonal and utonal scales are built so that this is enough.

The price of adaptive tuning is *drift*. A pure interval is exact only
against its own reference, and the reference may itself have been tuned
against an earlier note, so errors accumulate along a progression. Four pure
fifths stacked from C reach an E about 21.5 cents above the pure major third
over C, and a song that keeps every chord pure can end up noticeably higher
or lower than where it began. This is not a defect: pure fifths, pure thirds
and a closed circle of keys cannot all be had at once. Tuning every note
against the key, as the application does, never drifts.

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

**Chord completion**
What the pattern editor adds to the notes played when its cursor is wider
than one note column: the other notes of a chord of the song's scale that
contains them (user-interface.md, Chord entry). Otonal and utonal songs only.

<<<<<<< HEAD
**Chord palette**
The list of a degree's chords the pattern editor offers for the notes of an
entry or the note under the cursor; moving through it writes and plays each
(user-interface.md, Chord palette).
=======
**Cursor height**
How many rows the pattern editor's cursor covers, from 1 to one bar: the same
value as the edit step. Its first row is the cursor's position.
>>>>>>> origin/main

**Cursor width**
How many note columns of its row the pattern editor's cursor covers, 1 to 4;
the size of the chord an entry is completed to. Not the region, which the
mark makes.

**Edit step**
How many rows the pattern editor's cursor moves down after an entry, from 1
to one bar. It is also the cursor's height.

**Clip launcher**
The grid of clips inside Live View: one column per track, one row per scene,
each slot triggered on the next bar. On a Launchpad it is the pad grid in the
Live mode (the device's Session button). See launchpad.md and scenes.md.

**Extended diamond**
The gamut of the otonal and utonal scales: 33 just ratios. Its first
thirteen are the 7-limit tonality diamond (every ratio between two of 1, 3,
5 and 7, octave-reduced: 1/1 8/7 7/6 6/5 5/4 4/3 7/5 10/7 3/2 8/5 5/3 12/7
7/4). The rest come from the pairs that dividing an interval at its two
means gives, the larger otonal and the smaller utonal: (9/8, 10/9),
(15/14, 16/15) and (49/48, 50/49), each with its mirror, which with (3/2,
4/3), (5/4, 6/5) and (7/6, 8/7) are the only six pairs with no prime above
7. Ten further notes are superpartient (see superparticular and
superpartient), the tritones 7/5 and 10/7
among them; each gives a pair its seventh chord. A chord in this gamut is
any set of notes whose intervals are all ratios of the gamut. The gamut
mirrors itself around the tonic. In 53-EDO its ratios lie on 29 keys, four
of which carry one ratio of each scale. The diamond is Max Meyer's (1929),
and Harry Partch's is its 11-limit form; the extension is the author's own.
See extended-diamond.md.

**Follow action**
What a scene does once it has played for its length: `none` (wait for the
operator), `next` (launch the following scene on that very row) or `stop`. Set
in the file as `followAction` on `<scene>`. See scenes.md.

**Gamut**
The complete set of notes available to a system of music, from which
its scales are drawn. The word comes from the lowest note of the
medieval scale, Γ, gamma, which appears in the *Dialogus de musica*
(c. 1000) and was taken up by Guido of Arezzo (c. 991–after 1033), in
whose syllables it was sung as ut; the whole range from gamma-ut
upward became the gamut, first the full scale of medieval music and
then any complete range. The
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
major and minor; the extended diamond (above) is the gamut of the otonal and
utonal scales and is just; the quartertone gamut (see
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

The otonal and utonal scales (below) are different. Their chords use only
intervals that are ratios of the gamut, so no chord asks a note to be two
pitches: D and A, for one, never share a chord. One fixed pitch per note
therefore serves, whatever the chord (the cores are otonal 1/1 7/6 5/4 4/3
3/2 5/3 7/4 and utonal 1/1 8/7 6/5 4/3 3/2 8/5 12/7), and a song in either
scale is tuned with the whole extended diamond, so the two can be played
together.

**Lacuna**
A gap in a text: the place where a manuscript is torn or worn away and some of what
was written is lost (Latin *lacuna*, a hole). Editors of such texts mark one in
the edition. In a pattern a lacuna, written `~~~` (typed with `~` in the note
column), stands for a stretch of notes that are missing from the source, from its row
until the next cell in the column. It has no value, velocity or delay, makes no sound
and ends the previous note in its column, as an OFF does. A *lost note* is the smaller
case: a note was there, but its pitch is gone.

**Lift**
An OFF's velocity: how fast the key is let go. It scales the release time of
every envelope the note has, fixed when the OFF arrives:
`release = instrument release * 2^((64 - lift) / 32)`. Lift 64 leaves the release as
authored, 0 makes it four times longer, 127 about four times shorter. A user
instrument's `<envelope>` (which also releases PadSynth and Additive notes) scales its
release time; a SoundFont adds `(64 - lift) * 37.5` timecents to both its volume and
modulation envelope releases, held to the range the SF2 specification allows. In a
song file it is the `velocity` attribute of an OFF note.

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

**Locator**
A named marker at an absolute row of the arrangement, shown in the pattern
editor and the arrangement grid's header. It is an arrangement object too.

**Lost note**
A note that began on this row whose pitch is gone, written `???` (typed with `?` in
the note column). It has no value, no velocity and no delay, and it
makes no sound, but it ends the previous note in its column, as an OFF does. It is for transcriptions of music that survives
incompletely, such as the Delphic Paean of Athenaeus, where some of the notes are
unknown.

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
fourth. In the otonal and utonal scales each is three notes of a
seventh chord. C otonal has narrow major on C (C D♯ F) and G
(G A♯↑ C) and narrow minor on A♯↑ (A♯↑ C D♯) and, with the semitones,
on B (B D♭↓ E). C utonal mirrors this with narrow minor on C (C E𝄫 F)
and G (G B𝄫 C) and narrow major on B𝄫 (B𝄫 C E𝄫) and, with the
semitones, on A♭ (A♭ B D♭↓). The larger steps add more. Narrow major
is otonal and narrow minor utonal, in Partch’s sense. The names are
the author’s own; the chords have no settled name in the
just-intonation literature. See otonal and utonal.

**Otonal and utonal**
Two scales that mirror each other around the tonic (r → 2/r), drawn
from the extended diamond. Both are given here on C. Each grows from
the tonic by splitting intervals at their arithmetic and harmonic
means, in steps of 1, 3, 7, 15 and 18 notes; every step holds all the
notes that splitting the step before produces. The 3-note step is C,
F and G, which both scales share. The 7-note core is the chords
C E G A♯↑ and F A C D♯ put together: otonal 1/1 7/6 5/4 4/3 3/2 5/3
7/4, on C: C D♯ E F G A A♯↑, all harmonics (12, 14, 15, 16, 18, 20,
21) of one fundamental, so the core is otonal in Harry Partch’s
sense; the fundamental is not the tonic but the note a fifth below it
(F for C otonal), the tonic sitting on identity 3. The utonal core is
the same turned upside down: 1/1 8/7 6/5 4/3 3/2 8/5 12/7, on C:
C E𝄫 E♭ F G A♭ B𝄫, undertones of the note a fifth above the tonic.
The 15-note scale is everything a scale produces from itself (otonal
adds D𝄫 D♭↓ D F♭ F♯↑ F𝄪 B♭ B), and the 18-note scale adds three
notes that arise only between the two scales (otonal D𝄪 G𝄪 B♯).
From 7 notes on each note has a seventh chord made of the step’s
notes, a harmonic seventh 4:5:6:7 or its mirror 1/7:1/6:1/5:1/4, read
upward from the degree; its fourth note is often dropped. The
application’s scale is the full 18 notes; which step a piece uses,
and when it moves up or down, is the artist’s choice. Only the cores
are a single series. Each core contains a septimal pentatonic as a
melodic subscale (otonal C D♯ F G A♯↑, utonal C E𝄫 F G B𝄫). Partch’s
Otonalities and Utonalities are chords, not scales; these scales and
their chords are the author’s own. Their chords are tabled in
extended-diamond.md.

**Performance effect**
An effect held on a track from the Launchpad's Custom grid (stutter,
shuffle, erode, shatter, gate, crush, duck, a send throw, orbit): it
lasts while the pad is held and never changes the song. See
launchpad.md.

**Pythagorean gamut and the Babylonian modes**
The Pythagorean gamut is thirteen notes of the chain of fifths, six below
the tonic and six above, octave-reduced (all 3-limit). Its scale has
seven modes, each a rotation of one seven-note pattern and named after
the Babylonian tuning text (the table below; išartum, the "normal" scale, is
the gamut's default). The gamut is for 53-EDO only: its fifth is almost
exact, and its spellings put a comma arrow on every note of the chain beyond
F, C, G and D: E↑, A↑ and B↑ above, B♭↓, E♭↓ and A♭↓ below. The ends of the
chain lie two commas from their plain names and are respelled: D♭ as C♯↑,
G♭ as F♯↑ and F♯ as G♭↓. The table gives the conventional names. 31-EDO cannot tune it under
any spelling (81/64 lies between its steps 10 and 11), and 12 and 19-EDO
approximate it as they do every just ratio.

| Name | Strings | Mode | Notes on C |
| --- | --- | --- | --- |
| nīš tuḫrim | 1–5 | Locrian | C D♭ E♭ F G♭ A♭ B♭ |
| išartum | 2–6 | Ionian | C D E F G A B |
| embūbum | 3–7 | Dorian | C D E♭ F G A B♭ |
| nīd qablim | 4–1 | Phrygian | C D♭ E♭ F G A♭ B♭ |
| qablītum | 5–2 | Lydian | C D E F♯ G A B |
| kitmum | 6–3 | Mixolydian | C D E F G A B♭ |
| pītum | 7–4 | Aeolian | C D E♭ F G A♭ B♭ |

The names follow the descending reading of the tuning text accepted since
1990; under the older ascending reading each name denotes the inverse mode
(kitmum and pītum, išartum and nīd qablim, nīš tuḫrim and qablītum exchange;
embūbum is unchanged), which is what older tables and the Wikipedia
heptachord table show. Sources: Kilmer, "Mesopotamian Music Theory Since
1977" (2014), which gives the Greek octave-species names; Gurney, "Babylonian
Music Again" (1994); Krispijn (1990). The tuning text is UET VII 74.

**Phrygian tonos**
The Greek Phrygian key as Athenaeus uses it, diatonic and chromatic mixed: ten
notes of the Pythagorean gamut (53-EDO only), 1/1 256/243 9/8 32/27 4/3 1024/729
3/2 128/81 27/16 243/128 (C C♯↑ D E♭↓ F F♯↑ G A♭↓ A↑ B↑ on C). Chromatic
tetrachords are tuned limma, apotome, minor third, so the chromatic third note is
a 9/8 tone above the tetrachord's base. West's names for each degree are in
`Scale.h`. Not Archytas' septimal chromatic, which has 28/27 as its lowest step.

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

**Superparticular and superpartient**
Two classes of ratio in ancient and medieval theory. A superparticular
ratio (Greek *epimoric*) exceeds the unison by one part, (n+1)/n: 3/2, 5/4,
16/15. A superpartient ratio (*epimeric*) exceeds it by several parts: 5/3,
7/5, 9/7. Every ratio between the unison and the octave is one or the other,
and Pythagorean theory counted only multiple and superparticular ratios as
consonant. In the extended diamond a superparticular note stands at a
superparticular ratio from the tonic or from its octave (5/3 lies 6/5 below
the octave); these are the notes of the pairs. A superpartient note does so
from neither. There are ten: 7/5 and 10/7, 9/7 and 14/9, 35/24 and 48/35,
49/40 and 60/49, 49/30 and 80/49. They are dissonant by design, and each
gives a pair its seventh chord.

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

**Tempo change and signature change**
Arrangement objects, like a clip instance: from their row on, the tempo
(bpm) or the time signature (bars counted from that row) is the change's, until
the next. The song's own tempo and signature hold before the first. A
live recording writes them when a launched scene has either. See
time_signatures.md.

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

**Velocity**
How hard a note is struck, 0 to 255 (two hex digits, `00` to `ff`; one bit more than a MIDI 1.0 velocity, which is widened on input). One field per
note: on a note-on the strike velocity, on an OFF the *lift*, on aftertouch the
pressure. Velocity 0 is a real note-on that starts from silence; an OFF is its own
kind of cell and is never told by its velocity. 128 is *neutral velocity*, what code
gives a note made without one and what a song file leaves out; the editor and the
pad controllers give a new note-on 200 (the *default note-on velocity*) and a new
OFF the neutral one.
