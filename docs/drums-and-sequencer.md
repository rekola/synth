# Drums and sequencer

How a connected Novation Launchpad plays drums and scales and edits a clip as
steps. Session view, the buttons and the other modes are in
[launchpad.md](launchpad.md).

## Where it comes from

Nothing here is invented from scratch; each part follows a convention and says
where it differs.

- **Drum pads.** A 4x4 block of 16 pads is the standard drum surface. Akai's
  MPC samplers (from 1988) made it so, and the drum rack in Ableton Live shows
  16 of its 128 notes at a time in the same shape. Following that, the rack
  here is the bottom-left 4x4 of the grid, holding General MIDI notes 36-51 in
  order, left to right and then bottom to top, the usual default window of a
  drum rack. Only that one bank is offered: other banks, and the order they
  would come in, are left out until a layout is known exactly.
- **Scale keyboard.** Ableton Push's in-key layout is the model: a scale
  degree per column and a fourth per row. Here the scale is the song's, in any
  of the supported equal divisions of the octave, and plays as major when none
  is set.
- **Step editing.** Push splits its grid between a playing surface and a
  sequencer. Here the split is four rows and four rows, with 32 steps. Push
  chooses the sound by holding its pad; here the last pad pressed stays
  selected, so one hand is enough. A pitched note lasts one step. Chords
  (several held notes) are not supported yet.
- **Pad colours.** Pitched pads are coloured by consonance, which is our own
  choice (see Pad colors below); the 31-EDO organ built for Adriaan Fokker and
  the Archiphone coloured pads by distance from the diatonic scale instead.

## Playing

Note mode (96) shows a playing surface that follows the track:

- **Percussion track**: the bottom-left 4x4 block of pads is a General MIDI
  drum rack: notes 36-51 in order, left to right and then bottom to top, so
  the bottom row is kick, side stick, snare and clap, the second row starts
  at 40 (electric snare) and the top row ends at 51 (ride cymbal). The rest of
  the grid is dark.
- **Pitched track**: an in-key scale keyboard. Pad (0,0) is the tonic at the
  device's octave, each column is the next scale degree and each row up is a
  fourth (three degrees) higher, so a seven-note scale lines up the same way
  from row to row. The scale is the song's (`Song::getScale()`); with none
  chosen it plays as major. 91/92 shift the octave while a clip is open.
  Microtonal scales (`otonal`/`utonal`) give the same layout in 31/53-EDO.

## Pad colors

LED coloring originally followed the notational convention of Adriaan
Fokker's 31-EDO organ (built 1950 for Teylers Museum, Haarlem) and the
later Archiphone: each pad was colored by its *distance from the song's
diatonic scale* (tonic / diatonic degree / sharp / flat / diesis /
accidental), generalizing the idea that 31-EDO's chromatic notes split
into two musically distinct kinds (a full chromatic step vs. a
quarter-tone-ish shading) that a simple black/white keyboard can't
distinguish.

That scheme was replaced with a different organizing principle: coloring
by *consonance* rather than by scale-degree distance. Standard microtonal
note names (sharps, flats, double-flats, ...) are built from a fixed,
12-tone-shaped chain of fifths, which stops matching musical intuition
well once an EDO is fine enough that the interesting structure is no
longer "how many fifths from the tonic" but "how consonant is this
interval, period." The new scheme instead recursively factors the octave
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
12/19/31/53-EDO.

## Step grid

A percussion or pitched track's clip can be edited as steps. Hold 91 and press
a Session pad to open that pad's clip; the pad resolves on release. Every
device then switches to Note mode with the grid split in two: the bottom four
rows stay the playing surface (the drum rack, or the lower four rows of the
scale keyboard), and the top four rows are 32 steps, left to right and then
bottom to top.

The steps show one sound at a time - the pad you pressed last (the kick, or
the tonic, until you press one; it lights white). Press a step to set or clear
that sound there; a pitched note lasts one step. Pressing a playing pad both
selects it and sounds it, and records it as usual while capture is armed.
Each connected device shows its own 32-step page of a longer clip; 93 and 94
scroll the pages together. A lone press of 95 closes it.
