# The extended diamond: scales, chords and gamut

## Overview

Otonal and utonal are the application's two default scales, and each is the
other turned upside down (r → 2/r). They are given here on C and can be built
on any tonic. Each grows from the tonic alone by splitting intervals, in a
ladder of steps that each hold the ones below:

| Round | Notes | Otonal adds | Utonal adds | Seventh chords in each scale |
|---|---|---|---|---|
| 0 | 1 | C | C | |
| 1 | 3 | F G | F G | |
| 2 | 7 | D♯ E A A♯↑ | E𝄫 E♭ A♭ B𝄫 | 2 |
| 3 and 4 | 15 | D𝄫 D♭↓ D F♭ F♯↑ F𝄪 B♭ B | D♭↓ D↓ G𝄫 G♭↓ G♯ B♭↓ B B♯ | 6 |
| extended | 18 | D𝄪 G𝄪 B♯ | D𝄫 F𝄫 A♭↑ | 7 |

Every step is complete: it holds every note of the gamut that splitting the
intervals of the step before produces. From 7 notes on, every note also has
a seventh chord made of the step's own notes. An artist can stay on a small
step, or go up and down the ladder, even within one song: going up adds
chords, going down takes them away, and nothing already played changes. The
application's scale is the full 18 notes; which step a piece uses is the
artist's choice.

Three things set these scales apart from major and minor:

1. **Chords use only the gamut's intervals.** Every interval between two
   notes of a chord is itself a ratio of the gamut, so a chord never contains
   a near-pure interval or one the scales do not have.
2. **Every note has a seventh chord.** From 7 notes on, each degree belongs
   to a harmonic seventh, 4:5:6:7, or its mirror, 1/7:1/6:1/5:1/4.
3. **Nothing drifts.** Every note has one fixed ratio to the tonic. D enters
   at 15 notes, and D and A never share a chord, because 40/27 is not a
   ratio of the gamut; so the diatonic wolf fifth and the comma pumps of
   5-limit just intonation cannot occur.

**Notation.** Notes are spelled as in 53-EDO, the default tuning: the natural
notes are the just major scale, 7/4 is the augmented sixth A♯↑, 7/6 the
augmented second D♯, 8/7 the diminished third E𝄫 and 12/7 the diminished
seventh B𝄫. An arrow raises or lowers a note by one step of 53-EDO, and a
ratio is spelled by the key nearest it: 7/4 is A♯↑ and not A♯, because the
tempered A♯↑ lies 5 cents from it and the tempered A♯ 18. D𝄫, D♭↓, B and
B♯ each name two ratios, one per scale. Degrees are named by letter,
with the note's accidental attached. Chords are written lowest note first; a
proportion in fractions is the same chord as the whole numbers beside it.

## The steps

A round splits every interval between two different notes of the step
before, at its arithmetic and its harmonic mean, and keeps the results that
are notes of the scale. The derivations below are for otonal; utonal has
the mirror of each.

**1 note.** The tonic and its octave.

**3 notes: the frame.** Splitting the octave C–C′ gives G (the arithmetic
mean) and F (the harmonic mean). Both scales have all three, and they carry
no chord yet.

**7 notes: the core.** Splitting C–F, C–G, F–C′ and G–C′ gives D♯, E, A and
A♯↑. They make two harmonic seventh chords, C E G A♯↑ on the tonic and
F A C D♯ on the fourth degree, so the scale has two roots a fifth apart. The
seven notes are harmonics 12, 14, 15, 16, 18, 20 and 21 of the F a fifth
below the tonic, so this step is one harmonic series (in utonal, one series
of undertones of the G a fifth above, with the chords E𝄫 F A♭ C and
B𝄫 C E♭ G). It holds the major triads C E G and F A C, the minor A C E and
the narrow major triads C D♯ F and G A♯↑ C.

**15 notes: the full scale.** This step takes two rounds. The first splits
the core and gives six notes:

- D (9/8) divides C–E, and B (15/8) divides A♯↑–C′.
- D♭↓ (15/14) divides A–G.
- F♯↑ (7/5) and F𝄪 (35/24) are the two means of D♯–A♯↑.
- D𝄫 (49/48) divides A♯↑–D♯.

That leaves D without a chord. The second round gives the two notes it
needs: F♭ (9/7) divides D♭↓–G, and B♭ (9/5) divides G–D. The 15 notes are
everything the scale produces from itself, and they bring four more seventh
chords:

- C D♯ F♯↑ A♯↑, with the tritone. It is a mirror chord, the first harmonic
  contrast, and holds the minor triad D♯ F♯↑ A♯↑.
- D♭↓ E G B, the first chord without the tonic, so the harmony can move
  away from it. E gains a pure fifth and the minor triad E G B, and B is a
  leading note.
- D F♭ G B♭, with the second degree. G becomes a full degree, with the major
  triad G B D and the minor G B♭ D.
- D♯ F𝄪 A♯↑ D𝄫, a harmonic seventh built on a note that is itself a
  seventh. D𝄫 lies 36 cents above the tonic.

**18 notes: the extended scale.** Three more notes arise only between the two
scales: D𝄪 (49/40) is the mean of E♭ and E, G𝄪 (49/30) the mean of A♭ and
A, and B♯ (49/25) then divides G𝄪–D𝄪. B♯ is the mirror of the utonal 50/49
and completes the last pair of the gamut; D𝄪 and G𝄪, a neutral third and
sixth, give it its chord, D𝄪 F♯↑ G𝄪 B♯.

**Smaller complete sets.** A piece may keep to less than a step. The core
with the tritone (8 notes) has three seventh chords, all containing the
tonic, which suits music over a drone. The core with D♭↓ and B (9 notes) has
harmony that leaves the tonic without the small steps around the tritone. A
single seventh chord is complete in itself. None of these is a step,
because splitting produces more notes than they hold.

**Pentatonic subscales.** Each core contains a pentatonic with steps of 7/6,
8/7 and 9/8 and no semitone, useful for melody: C D♯ F G A♯↑, the septimal
minor pentatonic, in otonal, and its mirror C E𝄫 F G B𝄫 in utonal. They are
not complete, and take their chords from the steps above. The ordinary
pentatonic (C D E G A) does not occur.

## Chords

A chord is any set of notes in which every interval is a ratio of the gamut.
A degree's chord is a seventh chord it belongs to, read upward from the
degree. In practice the fourth note is often dropped, which also keeps two
degrees of one seventh chord from sounding alike.

### 15 notes

The 7-note core has the rows of its own notes and the chords made of them.
The last column lists the major, minor and narrow triads the degree roots.

| C otonal | Chord | Proportion | Or | Triad |
|---|---|---|---|---|
| C | C E G A♯↑ | 4:5:6:7 | C D♯ F A (6:7:8:10), C D♯ F♯↑ A♯↑ (1/7:1/6:1/5:1/4) | C E G, major; C D♯ F, narrow major |
| D𝄫 | D𝄫 D♯ F𝄪 A♯↑ | 7:8:10:12 | | |
| D♭↓ | D♭↓ E G B | 1/7:1/6:1/5:1/4 | | |
| D | D F♭ G B♭ | 1/8:1/7:1/6:1/5 | | D F♭ G, narrow minor |
| D♯ | D♯ F A C′ | 7:8:10:12 | D♯ F♯↑ A♯↑ C′ (1/12:1/10:1/8:1/7), D♯ F𝄪 A♯↑ D𝄫′ (4:5:6:7) | D♯ F𝄪 A♯↑, major; D♯ F♯↑ A♯↑, minor |
| E | E G A♯↑ C′ | 5:6:7:8 | E G B D♭↓′ (1/12:1/10:1/8:1/7) | E G B, minor; E F𝄪 A, narrow major |
| F♭ | F♭ G B♭ D′ | 1/7:1/6:1/5:1/4 | | |
| F | F A C′ D♯′ | 4:5:6:7 | | F A C, major |
| F♯↑ | F♯↑ A♯↑ C′ D♯′ | 1/10:1/8:1/7:1/6 | | |
| F𝄪 | F𝄪 A♯↑ D𝄫′ D♯′ | 5:6:7:8 | | |
| G | G A♯↑ C′ E′ | 6:7:8:10 | G B D♭↓′ E′ (1/10:1/8:1/7:1/6), G B♭ D′ F♭′ (1/12:1/10:1/8:1/7) | G B D, major; G B♭ D, minor; G A♯↑ C, narrow major |
| A | A C′ D♯′ F′ | 5:6:7:8 | | A C E, minor |
| A♯↑ | A♯↑ C′ E′ G′ | 7:8:10:12 | A♯↑ C′ D♯′ F♯↑′ (1/8:1/7:1/6:1/5), A♯↑ D𝄫′ D♯′ F𝄪′ (6:7:8:10) | A♯↑ D𝄫 D♯, narrow major; A♯↑ C D♯, narrow minor |
| B♭ | B♭ D′ F♭′ G′ | 1/10:1/8:1/7:1/6 | | |
| B | B D♭↓′ E′ G′ | 1/8:1/7:1/6:1/5 | | B D♭↓ E, narrow minor |

| C utonal | Chord | Proportion | Or | Triad |
|---|---|---|---|---|
| C | C E𝄫 F A♭ | 1/8:1/7:1/6:1/5 | C E♭ G B𝄫 (1/12:1/10:1/8:1/7), C E𝄫 G♭↓ B𝄫 (7:8:10:12) | C E♭ G, minor; C E𝄫 F, narrow minor |
| D♭↓ | D♭↓ F A♭ B | 4:5:6:7 | | D♭↓ F A♭, major |
| D↓ | D↓ F G♯ B♭↓ | 5:6:7:8 | | |
| E𝄫 | E𝄫 F A♭ C′ | 1/7:1/6:1/5:1/4 | E𝄫 G♭↓ B𝄫 C′ (4:5:6:7), E𝄫 G𝄫 B𝄫 B♯ (1/12:1/10:1/8:1/7) | E𝄫 G♭↓ B𝄫, major; E𝄫 G𝄫 B𝄫, minor |
| E♭ | E♭ G B𝄫 C′ | 1/10:1/8:1/7:1/6 | | E♭ G𝄫 A♭, narrow minor |
| F | F A♭ C′ E𝄫′ | 1/12:1/10:1/8:1/7 | F A♭ B D♭↓′ (5:6:7:8), F G♯ B♭↓ D↓′ (6:7:8:10) | F A♭ C, minor; F G♯ B♭↓, narrow major |
| G𝄫 | G𝄫 B𝄫 B♯ E𝄫′ | 1/10:1/8:1/7:1/6 | | |
| G♭↓ | G♭↓ B𝄫 C′ E𝄫′ | 5:6:7:8 | | |
| G | G B𝄫 C′ E♭′ | 1/8:1/7:1/6:1/5 | | G B𝄫 C, narrow minor |
| G♯ | G♯ B♭↓ D↓′ F′ | 7:8:10:12 | | |
| A♭ | A♭ C′ E𝄫′ F′ | 1/10:1/8:1/7:1/6 | A♭ B D♭↓′ F′ (6:7:8:10) | A♭ C E♭, major; A♭ B D♭↓, narrow major |
| B𝄫 | B𝄫 C′ E♭′ G′ | 1/7:1/6:1/5:1/4 | B𝄫 C′ E𝄫′ G♭↓′ (6:7:8:10), B𝄫 B♯ E𝄫′ G𝄫′ (1/8:1/7:1/6:1/5) | B𝄫 C E𝄫, narrow major; B𝄫 B♯ E𝄫, narrow minor |
| B♭↓ | B♭↓ D↓′ F′ G♯′ | 4:5:6:7 | | B♭↓ D↓ F, major; B♭↓ D♭↓ F, minor |
| B | B D♭↓′ F′ A♭′ | 7:8:10:12 | | |
| B♯ | B♯ E𝄫′ G𝄫′ B𝄫′ | 1/7:1/6:1/5:1/4 | | |

### 18 notes

The extended step adds one seventh chord to each scale. It serves its four
notes, read upward from any of them, and brings these triads:

| Scale | Seventh chord | Proportion | Triads it brings |
|---|---|---|---|
| otonal | D𝄪 F♯↑ G𝄪 B♯ | 1/8:1/7:1/6:1/5 | G𝄪 D𝄫 D𝄪 major, G𝄪 B♯ D𝄪 minor, D𝄪 F♯↑ G𝄪 narrow minor |
| utonal | A♭↑ D𝄫′ F𝄫′ G♭↓′ | 4:5:6:7 | A♭↑ D𝄫 F𝄫 major, A♭↑ B♯ F𝄫 minor, F𝄫 G♭↓ A♭↑ narrow major |

### Triads and other chords

| Triad | Proportion | Divides |
|---|---|---|
| major | 4:5:6 | the fifth, at its arithmetic mean |
| minor | 10:12:15 = 1/6:1/5:1/4 | the fifth, at its harmonic mean |
| narrow major | 6:7:8 | the fourth, at its arithmetic mean |
| narrow minor | 21:24:28 = 1/8:1/7:1/6 | the fourth, at its harmonic mean |
| septimal diminished | 5:6:7, and its mirror 1/7:1/6:1/5 | |

Each is three notes of a seventh chord of the gamut. For A C E in otonal,
and A♭ C E♭ in utonal, that chord (G♭↓ A C E, A♭ C E♭ F♯↑) also needs the
other scale's tritone, so within its own scale the triad stands alone. The
subminor and supermajor triads
(6:7:9, 14:18:21) hold a 9/7, the ratio of a superpartient note; they are
admitted as dissonant chords.

Beyond the seventh chords, the rule admits chords that contain a semitone.
The otonal scale has the major seventh chord F A C E (8:10:12:15) and, with
B, C E G B as well. Several chords of the otonal core sound D♯ (7/6)
against E (5/4), a 15/14 apart. Two of them are familiar:

- C D♯ E A♯↑ (12:14:15:21) is the harmonic seventh without its fifth, with
  D♯ added: a 7♯9 chord with no fifth. The 7♯9 is nicknamed the Hendrix
  chord, after the E7♯9 of Jimi Hendrix's "Purple Haze", though jazz players
  used it earlier. Its sound is the major third held against the blue third,
  and convention spells that sharp ninth D♯, as this notation spells 7/6. So
  the chord appears here with its blue note at the pure septimal third that
  blues players bend towards.
- C D♯ E A (12:14:15:20) is the minor triad A C E with D♯, a 7/5 above the
  A: A minor with its augmented fourth sounding as well.

### Five-note chords

Chord entry at a width of five offers these, nine to a scale. Each kind has
a mirror, and a chord is named after its lowest note in that form.

| Kind | Proportion | Notes above the lowest |
|---|---|---|
| harmonic ninth | 4:5:6:7:9 | 5/4, 3/2, 7/4, 9/4 |
| mirror harmonic ninth | 1/9:1/7:1/6:1/5:1/4 | 9/8, 9/7, 3/2, 9/5 |
| major ninth | 8:10:12:15:18 | 5/4, 3/2, 15/8, 9/4 |
| mirror major ninth | 1/18:1/15:1/12:1/10:1/8 | 6/5, 3/2, 9/5, 9/4 |
| 7♯9 with fifth | 12:14:15:18:21 | 7/6, 5/4, 3/2, 7/4 |
| mirror 7♯9 with fifth | 1/21:1/18:1/15:1/14:1/12 | 7/6, 7/5, 3/2, 7/4 |

The nine of otonal are F harmonic ninth (C D♯ F G A on C), F major ninth,
C 7♯9 with fifth, C harmonic ninth, C mirror harmonic ninth, C major ninth,
A mirror major ninth, C mirror 7♯9 with fifth and A mirror harmonic ninth.
The nine of utonal are F mirror harmonic ninth, F mirror major ninth, E𝄫
mirror 7♯9 with fifth, B♭↓ harmonic ninth, B♭↓ mirror harmonic ninth, D♭↓
major ninth, B♭↓ mirror major ninth, E𝄫 7♯9 with fifth and D♭↓ harmonic
ninth. A degree's list is every one of the nine that contains it, read
upward from it: chords from a lower step first, then harmonic ninths and
their mirrors, major ninths and theirs, 7♯9 chords and theirs, then the chord
named after the degree itself. Degrees with none (D𝄫, for one) have only
their four-note chords.

Otonal, on C (transpose with the tonic):

| Degree | Five-note chords |
|---|---|
| C | F harmonic ninth / C, F major ninth / C, C 7♯9 with fifth, C harmonic ninth, C mirror harmonic ninth, C major ninth, A mirror major ninth / C, C mirror 7♯9 with fifth |
| D♭↓ | A mirror harmonic ninth / D♭↓ |
| D | C harmonic ninth / D, C mirror harmonic ninth / D, C major ninth / D |
| D♯ | F harmonic ninth / D♯, C 7♯9 with fifth / D♯, C mirror 7♯9 with fifth / D♯ |
| E | F major ninth / E, C 7♯9 with fifth / E, C harmonic ninth / E, A mirror harmonic ninth / E, C major ninth / E, A mirror major ninth / E |
| F♭ | C mirror harmonic ninth / F♭ |
| F | F harmonic ninth, F major ninth |
| F♯↑ | C mirror 7♯9 with fifth / F♯↑ |
| G | F harmonic ninth / G, F major ninth / G, C 7♯9 with fifth / G, C harmonic ninth / G, A mirror harmonic ninth / G, C mirror harmonic ninth / G, C major ninth / G, A mirror major ninth / G, C mirror 7♯9 with fifth / G |
| A | F harmonic ninth / A, F major ninth / A, A mirror harmonic ninth, A mirror major ninth |
| A♯↑ | C 7♯9 with fifth / A♯↑, C harmonic ninth / A♯↑, C mirror 7♯9 with fifth / A♯↑ |
| B♭ | C mirror harmonic ninth / B♭ |
| B | A mirror harmonic ninth / B, C major ninth / B, A mirror major ninth / B |

Utonal, on C:

| Degree | Five-note chords |
|---|---|
| C | F mirror harmonic ninth / C, F mirror major ninth / C, E𝄫 mirror 7♯9 with fifth / C, B♭↓ harmonic ninth / C, B♭↓ mirror harmonic ninth / C, D♭↓ major ninth / C, B♭↓ mirror major ninth / C, E𝄫 7♯9 with fifth / C |
| D♭↓ | D♭↓ harmonic ninth, D♭↓ major ninth, B♭↓ mirror major ninth / D♭↓ |
| D↓ | B♭↓ harmonic ninth / D↓ |
| E𝄫 | E𝄫 mirror 7♯9 with fifth, B♭↓ mirror harmonic ninth / E𝄫, E𝄫 7♯9 with fifth |
| E♭ | F mirror harmonic ninth / E♭, F mirror major ninth / E♭, D♭↓ harmonic ninth / E♭, D♭↓ major ninth / E♭ |
| F | F mirror harmonic ninth, F mirror major ninth, E𝄫 mirror 7♯9 with fifth / F, D♭↓ harmonic ninth / F, B♭↓ harmonic ninth / F, B♭↓ mirror harmonic ninth / F, D♭↓ major ninth / F, B♭↓ mirror major ninth / F, E𝄫 7♯9 with fifth / F |
| G♭↓ | E𝄫 7♯9 with fifth / G♭↓ |
| G | F mirror harmonic ninth / G, F mirror major ninth / G |
| G♯ | B♭↓ harmonic ninth / G♯ |
| A♭ | F mirror major ninth / A♭, E𝄫 mirror 7♯9 with fifth / A♭, D♭↓ harmonic ninth / A♭, B♭↓ mirror harmonic ninth / A♭, D♭↓ major ninth / A♭, B♭↓ mirror major ninth / A♭ |
| B𝄫 | F mirror harmonic ninth / B𝄫, E𝄫 mirror 7♯9 with fifth / B𝄫, E𝄫 7♯9 with fifth / B𝄫 |
| B♭↓ | B♭↓ harmonic ninth, B♭↓ mirror harmonic ninth, B♭↓ mirror major ninth |
| B | D♭↓ harmonic ninth / B |

## Names

A harmonic seventh (4:5:6:7) is named by its root, the lowest note; a mirror
seventh (1/7:1/6:1/5:1/4) is named by its top note, its root too. The status
line and the chord palette use it: C E G A♯↑ is "C harmonic seventh", and C D♯
F♯↑ A♯↑ is "A♯↑ mirror seventh" ("A♯↑ mirror seventh / C" read upward from C).

## The diamond scale

**Diamond** is the 7-limit tonality diamond itself, every ratio between two of
1, 3, 5 and 7: the two 7-note cores and the two tritones, 13 notes.

| Note | C | E𝄫 | D♯ | E♭ | E | F | F♯↑ | G♭↓ | G | A♭ | A | B𝄫 | A♯↑ |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Ratio | 1/1 | 8/7 | 7/6 | 6/5 | 5/4 | 4/3 | 7/5 | 10/7 | 3/2 | 8/5 | 5/3 | 12/7 | 7/4 |

Each note has a key of its own in 53-EDO and in 31-EDO, and the Launchpad's
step view steps through its 13 keys. A note's chords are those of otonal, then
those of utonal, then the chords that take notes from both, and a chord is
offered only if all its notes are notes of the diamond, so it has eight seventh
chords: C E G A♯↑, F A C D♯ and C D♯ F♯↑ A♯↑ from otonal, E𝄫 F A♭ C, B𝄫 C E♭
G and E𝄫 G♭↓ B𝄫 C from utonal, and two that take notes from both scales and
need none outside the 13: G♭↓ A C E (E mirror seventh) and A♭ C E♭ F♯↑ (A♭
harmonic seventh).

**A key with two ratios.** In 53-EDO four keys of the gamut carry one ratio
of each scale. The scale decides which one a song has, and no scale holds both:

| Key | Name | Otonal ratio | Utonal ratio |
|---|---|---|---|
| 2 | D𝄫 | 49/48 | 50/49 |
| 5 | D♭↓ | 15/14 | 16/15 |
| 48 | B | 15/8 | 28/15 |
| 51 | B♯ | 49/25 | 96/49 |

## The gamut

The gamut has 33 ratios: the tonic, F and G, which both scales share, and
fifteen notes of each scale, every otonal note the mirror of a utonal one.
The number after a ratio is its key in 53-EDO.

| Notes | Otonal | | Utonal | |
|---|---|---|---|---|
| 7 | E | 5/4, 17 | A♭ | 8/5, 36 |
| 7 | A♯↑ | 7/4, 43 | E𝄫 | 8/7, 10 |
| 7 | D♯ | 7/6, 12 | B𝄫 | 12/7, 41 |
| 7 | A | 5/3, 39 | E♭ | 6/5, 14 |
| 15 | D | 9/8, 9 | B♭↓ | 16/9, 44 |
| 15 | B | 15/8, 48 | D♭↓ | 16/15, 5 |
| 15 | D♭↓ | 15/14, 5 | B | 28/15, 48 |
| 15 | F♯↑ | 7/5, 26 | G♭↓ | 10/7, 27 |
| 15 | F𝄪 | 35/24, 29 | G𝄫 | 48/35, 24 |
| 15 | D𝄫 | 49/48, 2 | B♯ | 96/49, 51 |
| 15 | F♭ | 9/7, 19 | G♯ | 14/9, 34 |
| 15 | B♭ | 9/5, 45 | D↓ | 10/9, 8 |
| 18 | D𝄪 | 49/40, 16 | A♭↑ | 80/49, 37 |
| 18 | G𝄪 | 49/30, 38 | F𝄫 | 60/49, 15 |
| 18 | B♯ | 49/25, 51 | D𝄫 | 50/49, 2 |

C is key 0, F (4/3) key 22 and G (3/2) key 31; they are in both scales from
the 3-note step.

**Pairs and superpartient notes.** Dividing an interval at its arithmetic
and harmonic means gives a pair of ratios, the larger otonal and the smaller
utonal. Starting from the octave and dividing toward the tonic gives the
pairs (3/2, 4/3), (5/4, 6/5), (7/6, 8/7), (9/8, 10/9), (15/14, 16/15) and
(49/48, 50/49), the only six with no prime above 7. Each is in the gamut
with its mirror. The other ten notes are superpartient: they stand at a
superparticular ratio from neither the tonic nor its octave, and are
dissonant by design. They are the tritones 7/5 and 10/7, then 9/7 and 14/9,
35/24 and 48/35, 49/40 and 60/49, 49/30 and 80/49, and each gives a pair
note its seventh chord. The thirteen ratios between two of 1, 3, 5 and 7,
the 7-limit tonality diamond, are the two cores and the tritones.

## Tuning

53-EDO is the default tuning, and every note of a scale has a key of its own
in it. Four keys carry two ratios, one of each scale (keys 2, 5, 48 and 51);
the song's scale decides which. Played live, recorded or typed, and after
`apply-just-intonation-region`, each note sounds at its exact ratio to the
tonic ([user-interface.md](user-interface.md#just-intonation)), and a note
borrowed from the other scale is tuned as well. Without correction 53-EDO is
within 5 cents of every ratio of the core, 10 cents at 15 notes and 11 cents
at 18.

31-EDO holds the scales up to 15 notes: every note has a key of its own,
within 6 cents for the core and about 11 beyond. The extended step needs
53-EDO. A note keeps its spelling in every tuning, and in 31-EDO the utonal
A♭↑ would fall on A♭'s key. 12, 19 and 24-EDO are much coarser: the septimal
ratios lie 19 to 35 cents from their keys and those of 5 about 15, and many
gamut notes share a key.

## Technical details

**The diamond.** The 7-limit tonality diamond was first set out by Max Meyer
(1929); Harry Partch's diamond is its 11-limit form. Its thirteen notes are
the notes of the eight seventh chords that contain the tonic: the two cores
and the two tritones. Two of the eight, G♭↓ A C E and A♭ C E♭ F♯↑, take
notes from both scales and belong to neither.

**The pairs converge.** The two members of a pair lie ever closer: 204, 71,
36, 22, 7.7 and 0.7 cents apart. In the last pair the otonal 49/48 and the
utonal 50/49 are in effect one pitch, and so are the neutral thirds 49/40
and 60/49. That is why the 18-note step is the last: within the prime limit
7 there are no further pairs, and at this point otonal and utonal can no
longer be told apart by ear.

**Series.** Every note of the 7-note otonal core is a harmonic of the F a
fifth below the tonic, and the tonic is harmonic 12, identity 3, not the
fundamental. D (9/8) and B (15/8) are harmonics 27 and 45 of that F; the
other later notes are not in the series. In Partch's terms the core is the
Otonalities on 1/1 and 4/3 put together; his own Otonalities and
Utonalities are chords.

**Sources.** The arithmetic and harmonic means go back to Archytas; Zarlino
(1558) applied them to the fifth to get the major and minor triads, as told
for the present day in Kyle Gann's *The Arithmetic of Listening* (2019); the
diamond is Meyer's (*The Musician's Arithmetic*, 1929) and otonality and utonality are Partch's
(*Genesis of a Music*, 1949); the mirror is the harmonic dualism of
Hauptmann, von Oettingen and Riemann. The scales, the extension of the
diamond, the chord rule and the narrow triads with their names are the
author's own.
