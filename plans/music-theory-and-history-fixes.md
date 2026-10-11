# Music theory and history fixes

Findings from a review of README.md, docs/ and the code comments behind
them. The 53-EDO notation problems were fixed separately (53-EDO spelling,
the note table, the chain-of-fifths claim); what is left is below. Part 1 is
plain corrections; part 2 needs a decision first; part 3 records what was
checked and found right, so it need not be checked again.

## 1. Corrections

1. **Two wrong ratios in the note tables.** `docs/31edo_note_numbers.txt`
   step 9 lists 16:3; it is 16:13 (359.5 cents, the step is 348.4).
   `docs/19edo_note_numbers.txt` gives G 5:3; G is 3:2 (5:3 is A).
2. **Partch.** The glossary ("named after Partch's overtone and undertone
   series but are not his much longer scales"), the README ("borrow Partch's
   terms (1949) but are not his longer scales") and `Scale.h` have it the
   wrong way round. Partch's Otonality and Utonality are six-note *chords*,
   built from the odd numbers 1, 3, 5, 7, 9, 11 over one fundamental (or
   under one top note); his long scale has 43 notes and is neither. These
   scales are otonal and utonal in the general sense (every note a harmonic
   of one fundamental, or an undertone of one top note), using 15 and 21,
   which Partch's tonalities do not. Say that. The wording to use is in 2.1.
3. **Just intonation, glossary.**
   - "Intervals tuned that way have no beating between their partials":
     true only of sounds with harmonic partials (README's own piano section
     is the exception). Add the condition.
   - "Every EDO only approximates these ratios": except the octave.
   - "They have no D to argue over, so one fixed pitch per note serves", and
     "the 7-note otonal and utonal scales do not need [adaptive just
     intonation]": the scales have no D, but they still hold a 10:9. In the
     otonal scale G-A is 10:9 (3/2 to 5/3), and the chord tuner, which
     tunes every note above the chord's lowest, picks 9:8 for that step, so
     a chord with G in the bass and A above it puts A a comma (21.5 cents)
     away from its fixed pitch. The utonal scale has the same with E♭-F. In
     31-EDO the tuner also picks 14:13 for E-F and D♯-E, and 13:10 for F-A𝄰,
     where the fixed pitches give 16:15, 15:14 and 21:16 (8.9 to 16.6 cents
     apart). `plans/just-tuning.md` checked seven chords, none with these
     pairs. Soften the claim to what holds: every interval between two notes
     of the scale is a simple ratio with one fixed pitch per note, and the
     chord tuner reproduces those pitches for the chords that hold the tonic
     (the triads and tetrads). Whether the tuner should change is 2.3.
4. **FM electric pianos (README, Pianos and Just Intervals).** "Put partial
   n at n + (n−1)·ε" holds for the upper sidebands only. A lower sideband
   folds back to n + (n+1)·ε, so each partial is a pair 2ε apart (half a
   hertz at middle C), which beats inside one note whenever the index makes
   the lower sidebands audible. And not every layer has ε = 0.001:
   `piano.electric.fm` uses 1.002 (ε = 0.002) and a 0.51 clang layer whose
   sidebands are inharmonic on purpose (docs/fm.md). State the ε per layer
   and the folded sidebands, or say "close to" instead of "at".
5. **bus_effects.md, GranularCloud defaults.** "±15¢ would be below the
   threshold of perception": a pitch difference of 5 to 10 cents is
   noticeable, and grains detuned against each other beat. Give the reason
   the default is ±40¢ without the threshold claim.
6. **Fokker's organ (README, drums-and-sequencer.md).** Built 1950 by the
   firm of Pels to Fokker's design and installed at Teylers Museum,
   Haarlem; it has been in the Muziekgebouw aan 't IJ, Amsterdam, since
   about 2009. Add where it is now. 31-EDO itself goes back to Huygens
   (*Novus cyclus harmonicus*, 1691), who related it to quarter-comma
   meantone; Fokker revived it. Name Huygens.
7. **Fokker organ and Archiphone have keys, not pads**
   (drums-and-sequencer.md, "Where it comes from" and "Pad colors").
8. **Glossary, Groove: Renoise's groove.** "Four per-line amounts that repeat
   every four lines": each of the four sliders delays the second line of a
   pair, so the pattern repeats every eight lines (at four lines per beat).
9. **README, tracker history.**
    - "Scream Tracker, FastTracker 2 and Impulse Tracker added a volume column
      and lettered effect commands": the volume column and lettered commands
      came with Scream Tracker 3 (1994) and Impulse Tracker; FastTracker 2
      kept the hex digit commands of the Amiga trackers and used letters only
      for its additions. Name Scream Tracker 3.
    - Ultimate Soundtracker as "the first known tracker" is the usual
      account; Soundmonitor (Chris Hülsbeck, 1986, C64) is often named as its
      precursor. Mention it or keep "usually counted the first".
    - Renoise "added several note columns per track and a delay column": not
      verified that Renoise was first with several note columns. Say it has
      them rather than that it added them, unless a source is found.

## 2. Decisions needed

1. **Describing the otonal and utonal scales.** Keep the names (they are the
   identifiers in song files and code, and accurate in the general sense),
   but describe them by what they are. A draft for the README and glossary:

   > Two 7-note scales that mirror each other around the tonic. **Otonal**
   > (C D♯ E F G A A𝄰 = 1/1 7/6 5/4 4/3 3/2 5/3 7/4): all seven are
   > harmonics (12, 14, 15, 16, 18, 20, 21) of one low F, which is what
   > Partch called otonal. It holds the 4:5:6:7 chord C E G A𝄰 and pure
   > major triads on C and F. **Utonal** (C E𝄫 E♭ F G A♭ B𝄫 =
   > 1/1 8/7 6/5 4/3 3/2 8/5 12/7) is the otonal scale turned upside down:
   > all seven are undertones of one high G. It holds the 1/7:1/6:1/5:1/4
   > chord E𝄫 F A♭ C and pure minor triads on C and F.

   The point a reader would otherwise miss: the otonal scale is not the
   harmonic series of its own tonic, but of the note a fifth below it.
   Names that were considered and rejected: harmonic/subharmonic (clashes
   with harmonic minor), overtone/undertone scale (already the acoustic
   scale and its inversion), septimal major/minor ("septimal minor third"
   is 7:6, which is in the otonal scale). "7-limit otonal" in prose is
   possible.
2. **The Gann attribution.** "Their intervals were derived by the methods in
   Kyle Gann's *The Arithmetic of Listening* (2019)": the book exists, but
   whether it derives these scales could not be checked. Cite the chapter, or
   reword ("in the manner of").
3. **The chord tuner and the scales (1.3).** Either accept the drift and
   document it, or prefer, for an interval above the bass, the ratio the
   key-relative pitches already give when it is within the error cap (G-A
   would then stay 10:9 when both are in the scale). That changes
   `correctionCentsInChord()`; it needs tests for the major-scale ii chord
   (which must still get its 10:9 D) and for the otonal and utonal pairs
   above.

   The scale and the corrections: today a correction depends on the key and
   the chord, never on the scale, so `set-song-scale` changes no note and
   needs neither to retune nor to clear them. If the tuner starts reading
   the scale (this option, or just intonation applied automatically when
   notes are played and recorded), changing the scale must retune every note
   that carries a correction, the way transposing already does.
4. **31-EDO tunes B as 13:7.** The step chooser takes the ratio with the
   smallest numerator times denominator within 20 cents, which makes
   31-EDO's step 28 (B, the major seventh) 13:7 (height 91) instead of 15:8
   (height 120), 12 cents flat of the step and 16.6 cents below 15:8.
   19-EDO is the same. A note tuned from the key (any chord's bass, so a G/B
   chord, or a melody note) gets it. `docs/31edo_note_numbers.txt` says
   15:8. Options: prefer the lowest prime limit within the cap (5 before 7
   before 11 and 13), or weight height by prime. Check what it does to every
   step of all four tunings before choosing.
5. **6/8 and the beat (time_signatures.md).** The rows-per-beat rule is
   16/d, so 6/8 has six beats of 2 rows, and the doc says 3/4 and 6/8 "differ
   in the beat only". Conventionally 6/8 is two beats of a dotted quarter
   (6 rows each), and 3/4 and 6/8 differ in grouping (3 x 2 against 2 x 3);
   the same for 9/8 and 12/8. Either count compound meters (numerator a
   multiple of 3 above 3, denominator 8 or 16) in dotted beats, which
   changes the metronome and the beat accents, or keep the plain rule and
   say in the doc that 6/8 is counted in eighths, as some counting does.

## 3. Checked and right

- Chowning's FM paper (1973), the DX7 (1983), the MPC60 and its 4x4 pads
  (1988), the MPC's 50-75% swing range and 67% triplet swing.
- Partch's *Genesis of a Music* (1949); Fokker, "Equal Temperament and the
  Thirty-one-keyed Organ", *The Scientific Monthly* 81 (1955); Fonville on
  Johnston's notation (1991).
- Piano inharmonicity: 866·B·n² cents (Fletcher 1964) and the 11 and
  33 cent figures for 7:4 and 12:7 at B = 4·10⁻⁴.
- 31-EDO's 4:3 5.2 cents wide; four pure fifths 21.5 cents above 5:4.
- The ii chord as 10:12:15; 12-EDO's chooser staying 5-limit plus 7:5 and
  15:14; 31-EDO reaching 7:6 and 11:10.
- additive.md: the free-free bar modes (1 : 2.756 : 5.404 : 8.933, from
  cos x cosh x = 1), the marimba's 4:1, the hammer contact-time exponent
  (p−1)/(p+1), the pluck's sin(nπβ)/n² and the unison beat rates.
- The double decay and aftersound of coupled unison strings.
