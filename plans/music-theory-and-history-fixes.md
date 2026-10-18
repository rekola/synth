# Music theory: open decisions

What is left of the music theory and history review once its corrections
were made (README, glossary, the note tables, drums-and-sequencer.md,
bus_effects.md). Each item needs a choice before it is built.

## 1. 31-EDO tunes B as 13:7

The step chooser (`model/JustIntonation.cpp`) takes the ratio with the
smallest numerator times denominator within 20 cents, which makes 31-EDO's
step 28 (B, the major seventh) 13:7 (height 91) instead of 15:8 (height 120):
12 cents flat of the step and 16.6 cents below 15:8. 19-EDO is the same. A
note tuned from the key (any chord's bass, so a G/B chord, or a melody note)
gets it. `docs/31edo_note_numbers.txt` says 15:8.

Rules tried on every step of the four tunings:

- Lowest prime limit first: wrong. 12-EDO's thirds become 32:27 and 81:64.
- 7-limit first: B becomes 15:8 and 12-EDO is unchanged, but 31-EDO loses
  11:8, 11:9 and 13:8 for 48:35, 49:40 and 49:30.
- Simplicity and closeness together, log2(height) + error / 10 cents
  (recommended): B becomes 15:8 in 19 and 31-EDO, and the rest barely moves.
  - 12-EDO: step 1 15:14 -> 16:15.
  - 19-EDO: 1 22:21 -> 28:27, 2 13:12 -> 14:13, 3 (D) 9:8 -> 10:9,
    10 10:7 -> 13:9, 18 21:11 -> 27:14.
  - 31-EDO: 1 33:32 -> 45:44, 2 21:20 -> 22:21, 3 14:13 -> 15:14,
    4 11:10 -> 12:11, 22 13:8 -> 18:11, 28 13:7 -> 15:8, 30 35:18 -> 39:20.
  - 53-EDO: 1 55:54 -> 64:63, 2 33:32 -> 36:35, 3 25:24 -> 26:25,
    5 15:14 -> 16:15, 52 55:28 -> 63:32.

  user-interface.md's "31-EDO reaches 7/6 and 11/10" becomes 7/6 and 12/11,
  and the just-intonation tests need their expected ratios checked.

## 2. 6/8 and the beat

The rows-per-beat rule is 16/d, so 6/8 has six beats of 2 rows, and
time_signatures.md says 3/4 and 6/8 "differ in the beat only".
Conventionally 6/8 is two beats of a dotted quarter (6 rows each), and 3/4
and 6/8 differ in grouping (3 x 2 against 2 x 3); the same for 9/8 and 12/8.

Options: count compound meters (numerator a multiple of 3 above 3,
denominator 8 or 16) in dotted beats, which changes the metronome and the
beat accents; or keep the plain rule and say in the doc that 6/8 is counted
in eighths here.
