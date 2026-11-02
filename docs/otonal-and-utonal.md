# The otonal and utonal scales and their chords

## Overview

Otonal and utonal are the application's two default scales. Both are given
here on C; they can be built on any tonic. In their 7-note form, otonal on C
is C D♯ E F G A A♯↑ (1/1 7/6 5/4 4/3 3/2 5/3 7/4) and utonal on C is
C E𝄫 E♭ F G A♭ B𝄫 (1/1 8/7 6/5 4/3 3/2 8/5 12/7), each the other turned upside
down. Three things set them apart from the major and minor scales they
resemble:

1. **Every chord is a slice of one harmonic series.** The chord on a degree
   is the series read upward from that degree, note by note, with nothing
   skipped; so every chord is as consonant as the series itself, and the
   basic chord is the harmonic seventh 4:5:6:7 rather than the triad 4:5:6.
2. **Richer harmony.** Beside the major and minor triads, which divide the
   fifth, the scales hold the narrow major and narrow minor triads, 6:7:8 and
   21:24:28, which divide the fourth, and the diatonic scale's wolf fifth on
   the second degree (D–A = 40/27) does not occur.
3. **Nothing drifts.** All notes of a scale are harmonics of one fundamental
   (undertones of one, for utonal), so every interval is fixed, every
   progression returns exactly to its starting pitch, and the comma pumps of
   5-limit just intonation (I–vi–ii–V–I losing 81/80 per cycle) cannot occur.
   The one comma in the system lies between the two scales and is handled when
   switching.

How the scales are constructed is in the technical details at the end.

**Notation.** Notes are spelled as in meantone: 7/4 is the augmented sixth
A♯, 7/6 the augmented second D♯, 8/7 the diminished third E𝄫, 12/7 the
diminished seventh B𝄫. An arrow after a note raises or lowers it by one step
of 53-EDO (a syntonic comma); the other tunings ignore it. So 7/4 is A♯↑,
since 53-EDO's plain A♯ is 26/15, and 16/9 is B♭↓, 16/15 is D♭↓, 24/13 is
B↓. **Degrees** are named by letter, as in the diatonic scale, so that IV is
always the fourth and V the fifth: I C, II D, III E, IV F, V G, VI A, VII B,
with the note's accidental attached. Chords are written lowest note first; a
proportion in fractions is the same chord as the whole numbers beside it;
intervals are between neighbouring notes, bottom to top.

## The 7-note scales

Otonal is harmonics 12, 14, 15, 16, 18, 20 and 21 of the F a fifth below the
tonic. Two of its notes are the 4th harmonic of their own series inside it:
C (harmonics 4–7 of C are C E G A♯↑) and F (harmonics 4–7 of F are
F A C D♯). These two harmonic seventh chords are the scale's parents; every
degree lies in one of them, and C in both. The chord of a degree is its
parent's series read upward from the degree to the degree's octave, taking
every harmonic that is a note of the scale and stopping at the first that is
not.

### C otonal

| Degree | Note | Chord | Ratios | Proportion | Intervals | Contains |
|---|---|---|---|---|---|---|
| I | C | C E G A♯↑ C′ | 1 5/4 3/2 7/4 2 | 4:5:6:7:8 | 5:4 6:5 7:6 8:7 | major C E G, narrow major G A♯↑ C |
| I | C | C D♯ F G A | 1 7/6 4/3 3/2 5/3 | 6:7:8:9:10 | 7:6 8:7 9:8 10:9 | narrow major C D♯ F, F G A; 11 is not a note |
| ♯II | D♯ | D♯ F G A | 7/6 4/3 3/2 5/3 | 7:8:9:10 | 8:7 9:8 10:9 | |
| III | E | E G A♯↑ C′ | 5/4 3/2 7/4 2 | 5:6:7:8 | 6:5 7:6 8:7 | septimal diminished E G A♯↑; 9 is not yet a note |
| IV | F | F A C D♯′ F′ | 4/3 5/3 2 7/3 8/3 | 4:5:6:7:8 | 5:4 6:5 7:6 8:7 | major F A C, narrow major C D♯ F |
| V | G | G A♯↑ C | 3/2 7/4 2 | 6:7:8 | 7:6 8:7 | narrow major; 9 is not yet a note |
| VI | A | A C D♯ F′ G′ A′ | 5/3 2 7/3 8/3 3 10/3 | 5:6:7:8:9:10 | 6:5 7:6 8:7 9:8 10:9 | septimal diminished A C D♯ |
| ♯VI | A♯↑ | A♯↑ C | 7/4 2 | 7:8 | 8:7 | dyad; 9 is not yet a note |

### C utonal

Utonal is the mirror: undertones 12 to 21 of the G a twelfth above the tonic,
with parents C E𝄫 F A♭ (undertones 4–7 of C′) and G B𝄫 C E♭ (undertones 4–7
of G′). Each chord is the mirror of the otonal chord on the mirrored degree —
the same set of notes reflected — voiced, like the otonal chords, from the
degree upward. (Read downward from the degree it is a run of undertones;
voiced upward it is that run rotated, so its proportion may jump once.)

| Degree | Note | Chord | Ratios | Proportion | Intervals | Mirror of |
|---|---|---|---|---|---|---|
| I | C | C E𝄫 F A♭ C′ | 1 8/7 4/3 8/5 2 | 1/8:1/7:1/6:1/5:1/4 | 8:7 7:6 6:5 5:4 | C E G A♯↑ C′ |
| I | C | C E♭ F G B𝄫 | 1 6/5 4/3 3/2 12/7 | 1/12:1/10:1/9:1/8:1/7 | 6:5 10:9 9:8 8:7 | C D♯ F G A |
| 𝄫III | E𝄫 | E𝄫 C′ | 8/7 2 | 1/7:1/4 | 7:4 | A♯↑ C |
| ♭III | E♭ | E♭ F G B𝄫 C′ E♭′ | 6/5 4/3 3/2 12/7 2 12/5 | 1/10:1/9:1/8:1/7:1/6:1/5 | 10:9 9:8 8:7 7:6 6:5 | A C D♯ F′ G′ A′ |
| IV | F | F C′ E𝄫′ | 4/3 2 16/7 | 1/12:1/8:1/7 | 3:2 8:7 | G A♯↑ C |
| V | G | G B𝄫 C′ E♭′ G′ | 3/2 12/7 2 12/5 3 | 1/8:1/7:1/6:1/5:1/4 | 8:7 7:6 6:5 5:4 | F A C D♯′ F′ |
| ♭VI | A♭ | A♭ C′ E𝄫′ F′ | 8/5 2 16/7 8/3 | 1/10:1/8:1/7:1/6 | 5:4 8:7 7:6 | E G A♯↑ C′ |
| 𝄫VII | B𝄫 | B𝄫 E♭′ F′ G′ | 12/7 12/5 8/3 3 | 1/14:1/10:1/9:1/8 | 7:5 10:9 9:8 | D♯ F G A |

The dyad on A♯↑ (and its mirror on E𝄫) is the scale's growing edge: the 7th
harmonic's next neighbour is the 9th, which the next step supplies as D. Until
then A♯↑ can take its parent read cyclically, A♯↑ C E G′ (7:8:10:12), which
skips nothing that exists.

## The 11-note scales

The next step of divisions (see the technical details) adds four notes to
each scale:

- otonal: D 9/8 (harmonic 27 of F, 9 of C), D♭ 13/12 (13 of F), C𝄫 11/6
  (11 of F) and B 15/8 (45 of F, 15 of C): C D♭ D D♯ E F G A A♯↑ C𝄫 B
- utonal, the mirrors: B♭↓ 16/9, B↓ 24/13, C𝄪 12/11 and D♭↓ 16/15:
  C D♭↓ C𝄪 E𝄫 E♭ F G A♭ B𝄫 B♭↓ B↓

The 11 and 13 complete F's harmonic series from 1 to 16, and the chords read
by the same rule now run further; where a chord changes from its 7-note form
the reason is a harmonic that has become a note.

### C otonal

| Degree | Note | Chord | Ratios | Proportion | Note |
|---|---|---|---|---|---|
| I | C | C E G A♯↑ C′ | 1 5/4 3/2 7/4 2 | 4:5:6:7:8 | unchanged |
| I | C | C D♯ F G A C𝄫 C′ | 1 7/6 4/3 3/2 5/3 11/6 2 | 6:7:8:9:10:11:12 | the 11 is now a note |
| ♭II | D♭ | D♭ D♯ E F | 13/12 7/6 5/4 4/3 | 13:14:15:16 | 17 is not a note |
| II | D | D E | 9/8 5/4 | 9:10 | dyad; 11 of C is not a note |
| ♯II | D♯ | D♯ F G A C𝄫 C′ D♭′ D♯′ | 7/6 4/3 3/2 5/3 11/6 2 13/6 7/3 | 7:8:9:10:11:12:13:14 | F's series is unbroken from 7 to 14 |
| III | E | E G A♯↑ C′ D′ E′ | 5/4 3/2 7/4 2 9/4 5/2 | 5:6:7:8:9:10 | the 9 is now a note |
| IV | F | F A C D♯′ F′ | 4/3 5/3 2 7/3 8/3 | 4:5:6:7:8 | unchanged |
| V | G | G A♯↑ C D′ E′ | 3/2 7/4 2 9/4 5/2 | 6:7:8:9:10 | the 9 is now a note; 11 is not |
| VI | A | A C D♯ F′ G′ A′ | 5/3 2 7/3 8/3 3 10/3 | 5:6:7:8:9:10 | unchanged |
| ♯VI | A♯↑ | A♯↑ C D E′ | 7/4 2 9/4 5/2 | 7:8:9:10 | the 9 is now a note; 11 is not |
| 𝄫I′ | C𝄫 | C𝄫 C′ D♭′ D♯′ E′ F′ | 11/6 2 13/6 7/3 5/2 8/3 | 11:12:13:14:15:16 | 17 is not a note |
| VII | B | B C′ | 15/8 2 | 15:16 | dyad; 17 is not a note |

### C utonal

| Degree | Note | Chord | Ratios | Proportion | Mirror of |
|---|---|---|---|---|---|
| I | C | C E𝄫 F A♭ C′ | 1 8/7 4/3 8/5 2 | 1/8:1/7:1/6:1/5:1/4 | C E G A♯↑ C′ |
| I | C | C C𝄪 E♭ F G B𝄫 C′ | 1 12/11 6/5 4/3 3/2 12/7 2 | 1/12:1/11:1/10:1/9:1/8:1/7:1/6 | C D♯ F G A C𝄫 C′ |
| 𝄭II | D♭↓ | D♭↓ C′ | 16/15 2 | 1/15:1/8 | B C′ |
| 𝄪I | C𝄪 | C𝄪 G A♭ B𝄫 B↓ C′ | 12/11 3/2 8/5 12/7 24/13 2 | 1/22:1/16:1/15:1/14:1/13:1/12 | C𝄫 C′ D♭′ D♯′ E′ F′ |
| 𝄫III | E𝄫 | E𝄫 A♭ B♭↓ C′ | 8/7 8/5 16/9 2 | 1/14:1/10:1/9:1/8 | A♯↑ C D E′ |
| ♭III | E♭ | E♭ F G B𝄫 C′ E♭′ | 6/5 4/3 3/2 12/7 2 12/5 | 1/10:1/9:1/8:1/7:1/6:1/5 | A C D♯ F′ G′ A′ |
| IV | F | F A♭ B♭↓ C′ E𝄫′ | 4/3 8/5 16/9 2 16/7 | 1/12:1/10:1/9:1/8:1/7 | G A♯↑ C D′ E′ |
| V | G | G B𝄫 C′ E♭′ G′ | 3/2 12/7 2 12/5 3 | 1/8:1/7:1/6:1/5:1/4 | F A C D♯′ F′ |
| ♭VI | A♭ | A♭ B♭↓ C′ E𝄫′ F′ A♭′ | 8/5 16/9 2 16/7 8/3 16/5 | 1/10:1/9:1/8:1/7:1/6:1/5 | E G A♯↑ C′ D′ E′ |
| 𝄫VII | B𝄫 | B𝄫 B↓ C′ C𝄪′ E♭′ F′ G′ B𝄫′ | 12/7 24/13 2 24/11 12/5 8/3 3 24/7 | 1/14:1/13:1/12:1/11:1/10:1/9:1/8:1/7 | D♯ F G A C𝄫 C′ D♭′ D♯′ |
| 𝄭VII | B♭↓ | B♭↓ A♭′ | 16/9 16/5 | 1/9:1/5 | D E |
| ↓VII | B↓ | B↓ G′ A♭′ B𝄫′ | 24/13 3 16/5 24/7 | 1/26:1/16:1/15:1/14 | D♭ D♯ E F |

### The 9 and the 15

D and B are the two new notes whose next harmonic is not a note: D needs the
11th harmonic of C (11/8) and B the 17th (17/16). The 11/8 is never produced
by the construction; the 17/16 arrives at the next step. Both notes are
multiples of 3 (9 = 3·3, 15 = 3·5), and both have chords built of fourths
and fifths instead, the one kind of interval that stacks on itself in these
scales:

| Scale | Degree | Chord | Ratios | Proportion |
|---|---|---|---|---|
| otonal | II | D G C′ | 9/8 3/2 2 | 9:12:16 (two fourths) |
| otonal | II | D G B | 9/8 3/2 15/8 | 3:4:5 |
| otonal | VII | B E′ A′ | 15/8 5/2 10/3 | 9:12:16 (two fourths) |
| otonal | VII | B D′ G′ | 15/8 9/4 3 | 5:6:8 |
| utonal | 𝄭VII | C F B♭↓ | 1 4/3 16/9 | 9:12:16 |
| utonal | 𝄭VII | D♭↓ F B♭↓ | 16/15 4/3 16/9 | 1/5:1/4:1/3 |
| utonal | 𝄭II | E♭ A♭ D♭↓′ | 6/5 8/5 32/15 | 9:12:16 |
| utonal | 𝄭II | F B♭↓ D♭↓′ | 4/3 16/9 32/15 | 1/8:1/6:1/5 |

The 11 and the 13 have no fourth or fifth in the scale (13/12·3/2 and
11/6·4/3 are not notes); their chords are their windows above, clusters of
seconds running up F's series.

## Tuning

Played live, recorded or typed, and after `apply-just-intonation-region`, each
note of either scale sounds at its exact ratio to the tonic
([user-interface.md](user-interface.md#just-intonation)). In 31-EDO each note
of the 11-note scales has its own step (otonal 0 4 5 7 10 13 18 23 25 27 28,
utonal 0 3 4 6 8 13 18 21 24 26 27), so one correction per step tunes a scale
exactly; in 53-EDO the mirror is step n ↔ 53 − n and the two scales share only
C, F and G. Without correction, 31 and 53-EDO come within 6 cents of every
7-limit ratio; 12 and 19-EDO only approximate the septimal ones, and neither
can hold the 11-note scales.

## Technical details

**Construction.** Both scales are built from the tonic by dividing intervals
at their arithmetic and harmonic means. Dividing the octave C–C′ gives G (the
arithmetic mean, 3/2) and F (the harmonic mean, 4/3). After that each step
divides, for every note added in the previous step, its interval to the
nearer of C and C′, and keeps the results that stand a superparticular
interval ((n+1)/n) from either tonic. Otonal keeps the arithmetic means,
utonal the harmonic means, and each is the mirror (r → 1/r) of the other. The
scales grow 3 → 7 → 11 → 15 → …, four notes a step from the second step on;
from the fourth step the new notes lie within 110 cents of a tonic.

**Series.** Every otonal note is a harmonic of the F a fifth below the tonic,
and every utonal note an undertone of the G a twelfth above it, at every
step: the arithmetic mean of two harmonics of F is a harmonic of F. The
tonic is harmonic 12 of F, identity 3, not the fundamental. In Partch's terms
each scale is one Otonality or Utonality; his own Otonalities and Utonalities
are chords, and these scales are the author's own.

**Chords.** A degree's chord is its parent's harmonic series read upward from
the degree to the degree's octave, including every harmonic that is a note
and stopping at the first that is not; the mirror gives utonal. The parents
are the harmonic seventh chords on C and on F, i.e. the two notes that are
the 4th harmonic of a series inside the scale. Consequences: every chord
contains both a 7 and an 8 before any 9, so the supermajor third 9:7 never
occurs as a step; the familiar triads are the first three notes of the
chords (4:5:6 major, 6:7:8 narrow major, 5:6:7 septimal diminished, and
their mirrors); the last note of each step has only a dyad until the next
step supplies its neighbour.

**Means and triads.** The major triad 4:5:6 and minor triad 10:12:15 divide
the fifth at its arithmetic and harmonic means; the narrow major 6:7:8 and
narrow minor 21:24:28 do the same for the fourth; and the divisions of the
thirds give the 9-, 11- and 13-triads (8:9:10, 10:11:12, 12:13:14 and their
mirrors), each named for the odd number its mean introduces.

**Sources.** The means are Zarlino's (1558), told for the present day in Kyle
Gann's *The Arithmetic of Listening* (2019); otonality and utonality are
Partch's (*Genesis of a Music*, 1949); and the mirror is the harmonic dualism
of Hauptmann, von Oettingen and Riemann. The scales, the construction, the
chord rule and the narrow triads with their names are the author's own.
