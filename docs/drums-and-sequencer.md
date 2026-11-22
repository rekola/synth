# Drums and sequencer

How a connected Novation Launchpad plays drums and scales and edits a clip as
steps. Live View, the buttons and the other modes are in
[launchpad.md](launchpad.md).

## Where it comes from

- **Drum pads.** A 4x4 block of 16 pads is the standard drum surface, first
  used in the Akai MPC (1988). The rack here is the bottom-left 4x4 of the
  grid, holding General MIDI notes 36-51 in order, left to right and then
  bottom to top, the usual default window of a drum rack. Only that one bank
  is offered: other banks, and the order they would come in, are left out
  until a layout is known exactly.
- **Pad colours.** Pitched pads are coloured by consonance, which is our own
  choice (see Pad colors below); the 31-EDO organ built for Adriaan Fokker and
  the Archiphone coloured their keys by distance from the diatonic scale instead.

## Playing

Note mode (96) shows a playing surface that follows the track:

- **Percussion track**: the bottom-left 4x4 block of pads is a General MIDI
  drum rack: notes 36-51 in order, left to right and then bottom to top, so
  the bottom row is kick, side stick, snare and clap, the second row starts
  at 40 (electric snare) and the top row ends at 51 (ride cymbal). The rest of
  the grid is dark.
- **Pitched track**: an isomorphic keyboard, with the song's key at the anchor
  pad at the device's octave.
  - **53-EDO** (the default) has a lattice for the extended diamond gamut: a step
    right is 5 steps (about 16/15) and a row up 12 (7/6), so the fourth runs along
    the diagonal and a chord keeps its shape wherever it is played. The bottom 4
    rows hold all 21 notes of otonal, the 3 borrowed ones included, and the 8 rows
    the tonic and its octave. The tonic is
    on the pad in column 4 of the bottom row; a utonal song has the same lattice
    with the tonic at column 3 of row 3, and it also holds all 21 notes of utonal in the 4 rows. Every pad plays, those of no note
    of the scale as the nearest 53-EDO step. The pads are colored by the step of the
    scale's generation their note comes in (Pad colors, below).
  - **Other tunings** keep the whole-tone/semitone keyboard: fixed intervals of the
    song's tuning, the pads colored by how consonant their interval to the tonic is.
    A layout of the extended diamond for them is on the roadmap (README).
  The song's scale does not change the playing keyboard, except to anchor the tonic.
  - **While a clip is open for editing** (the step view, below) the 4 playing rows
    are the song's scale in order instead: the degrees, tonic first, left to right and
    then bottom to top, continuing into the next octave (32 pads: otonal's 18 notes
    and the first 14 of the octave above). The scale is the song's own, so the
    borrowed notes are not in it, and with no scale every step of the tuning. A
    percussion track has its drum rack as ever.

## Pad colors

**53-EDO.** A pad is colored by the step of the scale's generation its note comes
in ([extended-diamond.md](extended-diamond.md), The steps): the tonic and its
octave first and most prominent (yellow), then the fourth and the fifth (two close
ambers), the rest of the 7-note core (blue), the 15-note step (violet), the
18-note step (muted magenta), a borrowed note (muted cyan), and a pad that is no
note of the scale grey. Prominence is the saturation, since the pad's lightness
shows how loud its note sounds. The rest of this section is the other tunings.

LED coloring originally followed the notational convention of Adriaan
Fokker's 31-EDO organ (built by Bernard Pels, at Teylers Museum, Haarlem,
from 1950, now in the Muziekgebouw aan 't IJ, Amsterdam) and the
later Archiphone: each pad was colored by its *distance from the song's
diatonic scale* (tonic / diatonic degree / sharp / flat / diesis /
accidental), generalizing the idea that 31-EDO's chromatic notes split
into two musically distinct kinds (a full chromatic step vs. a
quarter-tone-ish shading) that a simple black/white keyboard can't
distinguish.

That scheme was replaced with a different organizing principle: coloring
by *consonance* rather than by scale-degree distance. Microtonal note
names (sharps, flats, double-flats, ...) are built from a fixed, 12-tone-shaped
set of seven letters (a chain of fifths, or in 53-EDO a just major scale),
which stops matching musical intuition
well once an EDO is fine enough that the interesting structure is no
longer how many fifths a note is from the tonic, but how consonant its
interval to the tonic is in itself. The new scheme instead recursively factors the octave
the way classical interval theory does - `2/1 = 4/3 * 3/2` (the octave's
simplest factor pair is the fourth and fifth), then `3/2 = 6/5 * 5/4` (the
fifth's own simplest factor pair is the minor and major third), and so on
- reusing that same factoring operation, approximated proportionally, at
every deeper level. This reaches every pitch class (no leftover
"everything else" bucket) within 4-6 levels for all four supported EDOs.

Color encodes the resulting tree two ways: hue drifts away from a shared
starting point by a shrinking amount at each level (so a pitch stays
hue-close to its harmonic neighborhood, however deep the recursion goes),
and saturation fades with depth (so more distant/complex notes read as
more muted) - both channels survive the idle-brightness remap that only
lightness gets overridden by; tonic keeps a fixed, deliberately dissimilar
hue (yellow) so it always pops out.

The classification is purely a function of the EDO and key (see
`LaunchpadLayout::computeConsonanceLevels`), so it applies unchanged to
12/19/31/24/53-EDO.

## Step grid

A percussion or pitched track's clip can be edited as steps. In Live View, hold 91
and press a pad to open the corresponding clip; the pad resolves on release.
The device you used then switches to Note mode (any other device keeps its
mode) with the grid split in two: the bottom four
rows stay the playing surface (the drum rack, or the lower four rows of the
isomorphic keyboard), and the top four rows are 32 steps, left to right and then
bottom to top.

The steps show one sound at a time - the pad you pressed last (the kick, or
the tonic, until you press one; it lights white). Press a step to set or clear
that sound there; a pitched note lasts one step. One sound is selected at a
time, so chords (several held notes) are not supported yet. Pressing a playing pad both
selects it and sounds it, and records it as usual while capture is armed.
On a pitched track, 91 and 92 move the sound the steps edit one scale degree
up or down (the song's scale, `set-song-scale`; with none chosen, every step of the tuning),
so a degree can be picked without pressing its pad. For a clip longer than 32
steps, 93 and 94 scroll the window. A lone press of
95 closes it.
