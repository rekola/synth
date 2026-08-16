# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)

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
keyboard to any EDO via a best-fifth generator (`src/launchpad/LaunchpadLayout.h`).

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

# Third-party code

This project is MIT-licensed (`LICENSE`). It vendors a small amount of
third-party source under `third_party/` (currently `tinyxml2`, zlib
license, and PocketFFT, BSD-3-Clause - the FFT backend behind
`src/dsp/RealFFT.h`, replacing FFTW/GPL) and dynamically links against several
permissively/LGPL-licensed system libraries. See `THIRD_PARTY_LICENSES.md`
for the full picture, or run `musiceditor --licenses` to print it.

# Roadmap / missing functionality:
1. Undo/redo
2. Effect-command interpretation during playback (slide, glide, vibrato, fade in/out, tremolo — currently editable and stored but not audible)
3. Kill-ring rotation (yank-pop / M-y)
4. Exchange-point-and-mark (C-x C-x)
5. DirAC heatmap marker overlay for every active spatial object, not just track positions — track azimuth/elevation markers, plus Granular Cloud grains and other shared-bus-effect taps (FDNReverb, MultiTapDelay)
