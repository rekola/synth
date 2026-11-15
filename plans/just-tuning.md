# Just intonation: the Launchpad keyboard

Every pitched note in 12/19/31/53-EDO can carry a cent correction toward just
intonation, stored in its local fx (`+hh`/`-hh`). Adaptive just intonation
(tuning against what is sounding) is dropped: each note gets one fixed ratio to
the key from its scale's gamut (`scaleCorrectionsFor()`), written by
`apply-just-intonation-region` and applied to live notes.

Left: the Launchpad keyboard for these scales (a decision, below).

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
| Note | C | E𝄫 | D♯ | E♭ | E | F | G | A♭ | A | B𝄫 | A♯↑ |

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

## Open

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
