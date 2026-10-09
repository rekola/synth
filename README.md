# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)

![Screenshot of Live View](https://raw.githubusercontent.com/rekola/synth/refs/heads/main/assets/screenshot1.png)

A microtonal multiparadigm music production system, which combines live sequencer with a traditional tracker.

## Features

- Live sequencer with sample and note based clips
- Microtonal (12/19/31/53-EDO)
- SoundFont2
- Headless mode (for art installations or escape rooms)
- Ambisonic Bus
- Emacs-style buffers and keybindings
- Works out of the box (Basic instruments are included and no low-latency requirements)
- Keyboard driven
- Launchpad support

# Background

The Application draws on several traditions.

- **Tracker.** Music is written as rows in a grid, one column per voice. The
  first known tracker is Ultimate Soundtracker (Obarski, 1987): four channels,
  each row a note, a sample and an effect. Scream Tracker, FastTracker 2 and
  Impulse Tracker added a volume column and lettered effect commands, and
  Renoise (2002) several note columns per track and a delay column. The
  Application's pattern editor follows the later trackers; see
  [docs/commands.md](docs/commands.md).
- **Live sequencer.** Live View holds a clip launcher: a grid of clips, one
  column per track, launched on the bar and grouped into scenes; see
  [docs/launchpad.md](docs/launchpad.md) and [docs/scenes.md](docs/scenes.md).
- **Pad controllers and samplers.** The Launchpad's buttons partly follow
  Novation's layouts, and the 4x4 grid of drum pads was first used in the
  Akai MPC (1988); see
  [docs/drums-and-sequencer.md](docs/drums-and-sequencer.md).
- **FM synthesis.** The FM instruments use John Chowning's frequency
  modulation (1973). A few of them are approximations of individual factory
  voices of the Yamaha DX7 (1983), the synthesizer that made FM widespread,
  standing in for a handful of General MIDI programs. See
  [docs/fm.md](docs/fm.md).
- **Emacs.** In the terminal interface each song is a buffer, as in Emacs:
  several can be open and switched between, with selection, kill and yank, and
  the M-x prompt. That is only the terminal interface; other interfaces are
  planned and will follow their own conventions. See
  [docs/terminal.md](docs/terminal.md).

# Scales and Tunings

- **Tunings.** A song is tuned to 12, 19, 31 or 53 equal divisions of the
  octave (EDO); a new song starts in 31-EDO, which Adriaan Fokker revived (organ
  built 1950 for Teylers Museum, Haarlem). Drum tracks have no tuning: a
  note is a General MIDI drum. Notes are spelled with sharps, flats, double sharps and double flats,
  and a tuning can spell the same pitch in several ways. In 12-EDO, E♯ is F. In
  31-EDO, E♯ and F are different pitches, but D𝄪 and F𝄫 are the same one. Note numbers for each tuning
  are in `docs/`, for example [31edo_note_numbers.txt](docs/31edo_note_numbers.txt).
- **Scales.** A song has a key and an optional scale: major (the Ionian
  mode), minor (the Aeolian mode), otonal or utonal. Each is written once as
  note names, so it is correct in every tuning. Otonal and utonal borrow
  Partch's terms (1949), but they are seven-note scales whose intervals
  were derived by the methods in Kyle Gann's *The Arithmetic of Listening*
  (2019). The scale is used by the sequencer, not by the Launchpad's
  keyboard.

## Pianos and Just Intervals

Sampled pianos can't play microtonal chords in tune; use a synthesized piano
such as `piano.electric.tine` instead.

The otonal and utonal scales contain septimal intervals (7:6, 7:4, 8:7,
12:7) that traditional scales lack. Two notes sounding together beat unless
the partials they share line up, and these intervals share high partials:
7:4 meets at the lower note's 7th partial, 12:7 at its 12th. A piano
string's partial *n* is about 866·B·n² cents sharp (Fletcher, 1964; B is
10⁻⁴ to 10⁻³), so at B = 4·10⁻⁴ 7:4 is 11 cents off and 12:7 33 cents, and
repitching a sample keeps that error. Melodies are unaffected, as beating
needs simultaneous notes.

The FM electric pianos (`piano.electric.*`, [docs/fm.md](docs/fm.md)) put partial *n* at
n + (n−1)·ε, using an FM modulator at 1+ε times the carrier, so shared
partials beat at most ε times the fundamental (ε = 0.001: a quarter of a hertz at
middle C). What remains is the tuning's own error, such as 31-EDO's 4:3 being
5.2 cents wide; removing that needs partials moved onto the tuning's steps
(Sethares, *Tuning, Timbre, Spectrum, Scale*), which FM can't do.

The acoustic piano (`<additive preset="piano"/>`, also `piano.acoustic.grand`
when the SoundFont has no piano; [docs/additive.md](docs/additive.md)) does that: every partial of every
note sits on the song's tuning, with a stretch bounded at the same ε, so shared
partials of the tuning's own intervals, septimal ones included, meet exactly.

# Terminal support

The terminal UI works in any terminal notcurses supports, but a modern one is
better. Terminals with the Kitty keyboard protocol (kitty, foot, WezTerm,
Ghostty) report key releases, which keyboard note entry needs to know how long
a note is held, so they can play musical chords (several notes at once); other
terminals can't. Terminals with Kitty graphics or Sixels get pixel graphics:
the spectrum analyzer and the DirAC heatmap in the scopes are drawn as real
pixels instead of braille cells, and without pixel support they fall back to
braille. See [docs/terminal.md](docs/terminal.md) for terminal-specific setup.

Known to work are kitty, xterm and GNOME Terminal. The full table is in
[docs/terminal.md](docs/terminal.md#supported-terminals).

# Launchpad support

A connected Novation Launchpad (Mini MK3 / X) becomes a drum rack or isomorphic
keyboard, a step sequencer and a clip launcher. See
[docs/launchpad.md](docs/launchpad.md) for the layout, colors and every button.

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
* Just tuning
* Continuous MIDI recording with retroactive capture

## Minor Missing Features

* DirAC heatmap marker overlay for every active spatial object, not just track positions — track azimuth/elevation markers, plus Granular Cloud grains and other shared-bus-effect taps (FDNReverb, MultiTapDelay)
* DC Filter and Soft Clipping
* 5.1 or 7.2 modes in addition to binaural headphone mode
* Legato voicing mode
* Custom drum kits without SoundFont: map individual percussion keys to arbitrary instruments instead of one whole SF2 kit preset.
* Binary storage for samples and cover art
* Instrument editor
* Track effect editor
* Allocation-free and realtime-priority renderer
* New recordings are initially drafts and can then be either merged or promoted to a clip
* Ambient textures (waves, campfire, thunder, rain etc.)
* Launch modes and Follow actions
* Emacs features:
    - Kill-ring rotation (yank-pop / M-y)
