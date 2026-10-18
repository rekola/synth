# Where a note sits: the spatial mode

A track has a position (azimuth, elevation, distance) and an extent, its
half-width in meters (`-Wxx`, `docs/commands.md`). Each note is placed around
that position by the track's **spatial mode**, `spatial="auto|ring|arc"` on the
`<track>` element in the song file (written only when it is not `auto`; there is
no command or menu for it yet).

- **`auto`** (default): each instrument picks its own. A General MIDI drum kit
  uses its key table (every drum has a place, with a little jitter per hit); the
  piano family, the mallet instruments (glockenspiel, vibraphone, marimba,
  xylophone, tubular bells), harp and timpani use the arc; everything else uses
  the ring. A track with no extent puts every note at its position.
- **`ring`**: each note column has a slot on a spiral, so the notes of a chord
  stand apart. Works on any instrument.
- **`arc`**: the note's pitch along the extent, low to high. A SoundFont preset
  runs over the key range it is mapped to; any other instrument, and a drum kit,
  over A0 to C8. Works on any instrument.

## The spiral

A slot depends on its own number alone (the note column the note is played
in), never on how many notes sound with it, so a voice never moves when another
is added. Slot 0 is the track's position. Slot k is turned k golden angles
(137.5 degrees) round and sits at sqrt(k/6) of the extent, which is the rim from
slot 6 on. The same chord always lands on the same points, so an arpeggio keeps
each column at its own point. Two notes in one column are not two voices (the
second retriggers the first); in different columns they are, even on a kit.

## SoundFont pan

A SoundFont region's own pan is an azimuth offset in every mode and on every
instrument, a kit and the arc families included, so a preset stored as two
hard-panned regions keeps its width at each slot.

An oscillator array's own cloud (`spread`) stays centred on the note's slot.
