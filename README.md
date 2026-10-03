# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)

![Screenshot of Session View](https://private-user-images.githubusercontent.com/6755525/660292924-cc8e0c7f-6c65-4844-af52-765658d019a7.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3OTA1OTkzMDksIm5iZiI6MTc5MDU5OTAwOSwicGF0aCI6Ii82NzU1NTI1LzY2MDI5MjkyNC1jYzhlMGM3Zi02YzY1LTQ4NDQtYWY1Mi03NjU2NThkMDE5YTcucG5nP1gtQW16LUFsZ29yaXRobT1BV1M0LUhNQUMtU0hBMjU2JlgtQW16LUNyZWRlbnRpYWw9QUtJQVZDT0RZTFNBNTNQUUs0WkElMkYyMDI2MDkyOCUyRnVzLWVhc3QtMSUyRnMzJTJGYXdzNF9yZXF1ZXN0JlgtQW16LURhdGU9MjAyNjA5MjhUMTIzNjQ5WiZYLUFtei1FeHBpcmVzPTMwMCZYLUFtei1TaWduYXR1cmU9MzljOGExNTNhNTU3YjEyM2E2MDJlMzg5N2E1OTM0NGVkMjdhZWQyM2RhNzFhOWU2ODVlNzFiOGRlZWU1YTJmNyZYLUFtei1TaWduZWRIZWFkZXJzPWhvc3QmcmVzcG9uc2UtY29udGVudC10eXBlPWltYWdlJTJGcG5nIn0.lGIy1dEvSZTFJEUGGy3a8-BkllA8NvsiCE-IhXsz1NE)

A microtonal multiparadigm music production system, which combines live sequencer with a traditional tracker.

# Features

- Launchpad support and Live arrangement
- Sample and note based clips
- Microtonal (12/19/31/53-EDO)
- Emacs-style buffers and keybindings
- SoundFont2
- Headless mode (for art installations or escape rooms)
- Ambisonic Bus

# Principles:

## User can start creating music instantly:

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
`src/dsp/RealFFT.h`) and dynamically links against several
permissively/LGPL-licensed system libraries. See `THIRD_PARTY_LICENSES.md`
for the full picture, or run `synth --licenses` to print it.

# Roadmap / missing functionality:

The major things that are missing are the following:

* A proper name
* GUI
* Undo/redo
* Lighting control (DMX/ArtNet)
* Tempo control
* Just tuning

## Minor Missing Features

* DirAC heatmap marker overlay for every active spatial object, not just track positions — track azimuth/elevation markers, plus Granular Cloud grains and other shared-bus-effect taps (FDNReverb, MultiTapDelay)
* DC Filter and Soft Clipping
* 5.1 or 7.2 modes in addition to binaural headphone mode
* Legato voicing mode
* Custom drum kits without SoundFont: map individual percussion keys to arbitrary instruments instead of one whole SF2 kit preset.
* Time signatures
* Binary storage for samples and cover art
* Instrument editor
* Track effect editor
* Allocation-free and realtime-priority renderer
* New recordings are initially drafts and can then be either merged or promoted to a clip
* Aftertouch filtering and aggregating
* Continuous MIDI recording with retroactive capture
* Ambient textures (waves, campfire, thunder, rain etc.)
* Launch modes and Follow actions
* Emacs features:
    - Kill-ring rotation (yank-pop / M-y)
