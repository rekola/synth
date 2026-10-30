# 53-EDO spelling and changing a song's tuning

53-EDO note names follow Ben Johnston's just-intonation notation (README,
"53-EDO notation"): the natural notes are the just major scale, a sharp or flat
is 25:24 (3 steps), an arrow a syntonic comma (1 step), a double sharp or flat
35:32 (7 steps). The requirements: every accidental is one Unicode symbol, and
a spelling means roughly the same interval in 31 and 53-EDO (C-E is a major
third in both). The otonal scale spells its 7:4 as A𝄰, because 53-EDO's A♯ is
26:15 (42 steps); the other tunings ignore the arrow.

## 1. Open: yanking notes between tunings

`set-song-tuning` converts a song by pitch (`convertSongTuning()`). Yanking
notes from a buffer in one tuning into one in another still copies note
values, not pitches. Decide whether a yank converts the same way.

## 2. Open: double sharp and flat in 53-EDO, 35:32 or two sharps

Now 35:32 (7 steps), the interval 31-EDO's double sharp is (4 steps,
154.8 cents against 155.1). With it E𝄫 is 8:7 and B𝄫 12:7 in both tunings, so
the utonal scale is one spelling and a 31-EDO utonal song renames to 53-EDO
correctly.

The alternative, two 25:24 sharps (6 steps, 141 cents), is what "double"
says and what Johnston's notation gives. It was tried in an earlier revision
of the 53-EDO PR and dropped, because it breaks exactly that: E𝄫 becomes 11
steps (about 15:13) and B𝄫 42 (26:15). The scales would then have to be given
as ratios rather than names, and a 31-EDO utonal song would no longer rename
to 53-EDO. Now that `set-song-tuning` converts by pitch, renaming matters
less, which could reopen this.

## 3. Open: should 31-EDO spell 7:4 with an arrow?

31-EDO displays step 25 as A♯. A song saved in 31-EDO therefore stores the
otonal 7:4 as A♯, even if it was typed A𝄰, and renaming it to 53-EDO gives
26:15. Displaying step 25 as A𝄰 in 31-EDO would keep it.

Against: arrows mean nothing in 31-EDO (it has no comma step), the display is
one name per step, so every A♯ would show the arrow, including where it is an
augmented sixth; and it is not the only note that renames badly. Reading each
31-EDO display name as 53-EDO, the ones off by more than half a 53-EDO step
(11.3 cents):

| 31-EDO step | Name | Ratio in 31-EDO | Renamed to 53-EDO | Ratio kept? |
|---|---|---|---|---|
| 3 | D♭ | 16:15 | 6 steps, +19.7 c | no, 16:15 is D𝄭 (5) |
| 9 | D𝄪 | 11:9, 16:13 | 16 steps, +13.9 c | 16:13 only; 11:9 is 15 |
| 12 | E♯ | 21:16, 13:10 | 20 steps, -11.7 c | 13:10 only; 21:16 is E𝄰 (21) |
| 15 | F♯ | 7:5 | 25 steps, -14.6 c | no, 7:5 is F𝄰 (26) |
| 16 | G♭ | 10:7 | 28 steps, +14.6 c | no, 10:7 is G𝄭 (27) |
| 25 | A♯ | 7:4 | 42 steps, -16.8 c | no, 7:4 is A𝄰 (43) |
| 26 | B♭ | 9:5 | 45 steps, +12.4 c | yes (31-EDO's B♭ is a wide 9:5) |

D (+10.2 c) is just inside the limit: 31-EDO's D lies between 9:8 and 10:9.
So arrows in 31-EDO's display would be needed at least for D♭, F♯, G♭ and A♯,
and the D♭ one (16:15, the diatonic semitone) is not septimal at all. Since
`set-song-tuning` converts by pitch, the display question is only about how
31-EDO reads, not about conversion.

## 4. Considered: 41-EDO for the large tuning

41-EDO was weighed as a smaller alternative to 53-EDO and dropped (its
draft table is gone). It has the same notation problems: its fifth is pure
(+0.5 cents), so four fifths make a Pythagorean third a comma (1 step)
above 5:4, D-A in a just spelling is again 40:27, and the A♯ of a just
spelling is 12:7, not 7:4. It is weaker where these scales need it: 5:4 is
5.8 cents off (53-EDO: 1.4), and the otonal and utonal scales come within
6.3 cents (53-EDO: 4.8, 31-EDO: 6.0). Its strengths are fewer steps and
consistency up to the 15-odd-limit (53-EDO: 9), which would matter for
11- and 13-limit harmony.
