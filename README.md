# synth

A Microtonal Synth: Tracker style music production system with microtonality.

# Features

- Microtonal (31-TET)
- Launchpad support

# Principles:

## User can start creating music instantly

- No low latency requirements
- Basic instruments are immediately available
    1. If there is no SoundFont, basic instruments (such as piano) are provided by the built in FM synthesis
      
## Keyboard driven

Everything can be done using keyboard without mouse

# Launchpad support

A connected Novation Launchpad (Mini MK3 / X / Pro MK3) becomes an
isomorphic note-entry grid, its layout generalizing the 12edo Wicki-Hayden
keyboard to any EDO via a best-fifth generator (`LaunchpadLayout.h`).

LED coloring follows the notational convention of Adriaan Fokker's 31-EDO
organ (built 1950 for Teylers Museum, Haarlem) and the later Archiphone:
each pad is colored by how it relates to the song's diatonic scale, not
just whether it's "in" or "out" of it - 31-EDO in particular splits its
chromatic notes into two musically distinct kinds (a full chromatic step
vs. a quarter-tone-ish shading) that a simple black/white keyboard has no
way to distinguish.

- **Tonic** - bright green
- **Diatonic degree** (the other 6 scale notes) - bright white
- **Sharp** (a full chromatic step above a scale degree) - dim red
- **Flat** (a full chromatic step below a scale degree) - dim amber
- **Diesis** (a quarter-tone-ish note between scale degrees - 31-EDO's
  characteristic microtonal color) - medium blue
- **Accidental** (ambiguously sharp-or-flat, e.g. a 12edo black key) -
  dim magenta

The classification is purely a function of the EDO and key (see
`LaunchpadLayout::classifyPad`), so it applies unchanged to 12/19/31/53-EDO.

# Roadmap / missing functionality:
1. Undo/redo
2. Effect-command interpretation during playback (slide, glide, vibrato, fade in/out, tremolo — currently editable and stored but not audible)
3. Kill-ring rotation (yank-pop / M-y)
4. Exchange-point-and-mark (C-x C-x)
