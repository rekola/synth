# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)
[![Windows x86-64](https://github.com/rekola/synth/actions/workflows/ci-windows-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-windows-x86_64.yml)

![Screenshot of Live View](https://raw.githubusercontent.com/rekola/synth/refs/heads/main/assets/screenshot1.png)

A microtonal, multiparadigm music production system that combines a live sequencer with a traditional tracker.

## Features

- Live sequencer with sample and note based clips
- Microtonal: 12, 19, 31 or 53 equal divisions of the octave (EDO), with just intonation
- SoundFont 2 (General MIDI) instruments
- Headless mode (for art installations or escape rooms)
- 3D spatial audio (ambisonics), heard binaurally on headphones
- Emacs-style buffers and keybindings
- Works out of the box: basic instruments are built in and no low-latency audio setup is needed
- Keyboard driven
- Launchpad support

## Background

The application draws on several traditions.

- **Tracker.** Music is written as rows in a grid, one column per voice. Chris
  Hülsbeck's Soundmonitor (1986, Commodore 64) looked like a tracker before
  the name existed: numbered steps as rows, a column for each of the
  computer's three voices, every value in hex, scrolling up the screen as it
  played. It is thought to have inspired Ultimate Soundtracker (Karsten
  Obarski, 1987, Amiga), usually counted the first tracker: four channels,
  each row a note, a sample and an effect. Scream Tracker 3 and FastTracker 2
  (both 1994) added a volume column, Scream Tracker 3 and Impulse Tracker
  lettered effect commands, and Renoise (2002) has several note columns per
  track and a delay column. The application's pattern editor follows the later
  trackers; see [docs/commands.md](docs/commands.md).
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
- **Emacs.** The keymap the terminal interface uses follows Emacs: each song
  is a buffer, several can be open and switched between, with mark and point
  selection, kill and yank, and the M-x prompt for any command by name. A
  graphical interface is planned that defaults to the usual desktop, DAW and
  tracker keys, with the Emacs keymap still available. See
  [docs/user-interface.md](docs/user-interface.md).

## Scales and Tunings

- **Tunings.** A song is tuned to 12, 19, 31 or 53 equal divisions of the
  octave (EDO); a new song starts in 31-EDO, which Christiaan Huygens described
  in 1691 and Adriaan Fokker revived: his 31-tone organ, built by Bernard Pels,
  stood in Teylers Museum, Haarlem, from 1950 and is now in the Muziekgebouw
  aan 't IJ, Amsterdam. Drum tracks have no tuning: a
  note is a General MIDI drum. Notes are spelled with sharps, flats, double sharps and double flats
  (plus arrows in 53-EDO, below), and a tuning can spell the same pitch in several ways. In 12-EDO, E♯ is F. In
  31-EDO, E♯ and F are different pitches, but D𝄪 and F𝄫 are the same one. Note numbers for each tuning
  are in `docs/`, for example [31edo_note_numbers.txt](docs/31edo_note_numbers.txt).
- **Notation.** In 12, 19 and 31-EDO the note names are a chain of fifths: the seven
  natural notes C D E F G A B are the usual seven consecutive fifths
  (F C G D A E B), and each further fifth up adds a sharp, each fifth down a
  flat (F♯ is the fifth above B, B♭ the fifth below F). The tuning only decides how
  many steps the fifth and the sharp are. In 31-EDO the fifth is 18 steps, so
  seven fifths less four octaves make a sharp of 2 steps (C♯ = 2, C𝄪 = 4,
  D♭ = 3, D = 5): the notation of the meantone family, in which four fifths
  make a major third (E = 10 steps, near 5:4). A flat is the mirror image, so
  C♯ and D♭ differ by one step. In 12-EDO the sharp is 1 step and the chain
  closes on itself, which is why E♯ is F there. See Fokker, "Equal Temperament
  and the Thirty-one-keyed Organ", *The Scientific Monthly* 81 (1955).
- **53-EDO notation.** 53-EDO is not a meantone tuning: four of its fifths
  make the Pythagorean third 81:64 (18 steps), a comma above 5:4 (17 steps).
  So that C-E is a 5:4 major third there as well, its note names follow Ben
  Johnston's just-intonation notation instead of a chain of fifths. The
  natural notes are the just major scale, made of the three major triads
  F A C, C E G and G B D: C D E F G A B = 1/1 9/8 5/4 4/3 3/2 5/3 15/8 =
  steps 0 9 17 22 31 39 48. A sharp or flat is the chromatic semitone 25:24
  (3 steps), and an arrow raises or lowers by the syntonic comma 81:80
  (1 step), where Johnston writes + and −. A double sharp or flat is 35:32
  (7 steps), the interval 31-EDO's double sharp is, rather than two 25:24
  sharps (6 steps): with it E𝄫 is 8:7 and B𝄫 12:7 in both tunings. Each
  accidental is a single symbol: 𝄮 𝄯 (natural up/down), 𝄰 𝄱 (sharp up/down),
  𝄬 𝄭 (flat up/down). D-A is therefore 40:27, a comma short of a fifth, and
  the fifth above D is A𝄮. The other tunings have no separate comma step and
  ignore the arrows, so a 53-EDO spelling still names the right note in
  31-EDO. See John Fonville, "Ben Johnston's Extended Just
  Intonation: A Guide for Interpreters", *Perspectives of New Music* 29/2
  (1991); the table is [53edo_note_numbers.txt](docs/53edo_note_numbers.txt).
- **Scales.** A song has a key and an optional scale: major (the Ionian
  mode), minor (the Aeolian mode), otonal or utonal. Each is written once as
  note names: major and minor come out meantone in 12, 19 and 31-EDO and just
  in 53-EDO. The scale decides which notes the Launchpad's step view steps
  through; it does not change the playing keyboard.
- **Otonal and utonal.** Two 7-note scales that mirror each other around the
  tonic. **Otonal** (C D♯ E F G A A𝄰 = 1/1 7/6 5/4 4/3 3/2 5/3 7/4): all seven
  are harmonics (12, 14, 15, 16, 18, 20, 21) of one low F, which is what
  Harry Partch called otonal (*Genesis of a Music*, 1949). It holds the 4:5:6:7
  chord C E G A𝄰 and pure major triads on C and F. **Utonal** (C E𝄫 E♭ F G A♭
  B𝄫 = 1/1 8/7 6/5 4/3 3/2 8/5 12/7) is the otonal scale turned upside down:
  all seven are undertones of one high G. It holds the 1/7:1/6:1/5:1/4 chord
  E𝄫 F A♭ C and pure minor triads on C and F. Partch's own otonalities and
  utonalities are six-note chords; these scales are the application's own,
  inspired by Kyle Gann's *The Arithmetic of Listening* (2019) and its account
  of the arithmetic and harmonic means. They are spelled so that 31 and 53-EDO
  both come within 6 cents of every ratio; 12 and 19-EDO only approximate the
  septimal ones. The 7:4 is A𝄰 because 53-EDO's A♯ is 26:15; the other
  tunings read it as A♯.

### Pianos and Just Intervals

Sampled pianos can't play microtonal chords in tune; use a synthesized piano
such as `piano.electric.tine` instead.

The 7-note otonal and utonal scales contain septimal intervals (7:6, 7:4, 8:7,
12:7) that traditional scales lack. Two notes sounding together beat unless
the partials they share line up, and these intervals share high partials:
7:4 meets at the lower note's 7th partial, 12:7 at its 12th. A piano
string's partial *n* is about 866·B·n² cents sharp (Fletcher, 1964; B is
10⁻⁴ to 10⁻³), so at B = 4·10⁻⁴ 7:4 is 11 cents off and 12:7 33 cents, and
repitching a sample keeps that error. Melodies are unaffected, as beating
needs simultaneous notes.

The FM electric pianos (`piano.electric.*`, [docs/fm.md](docs/fm.md)) keep their
partials on or very near whole-number multiples of the note. A layer whose
modulator is at a whole-number ratio to the carrier is exactly harmonic. A layer
at 1+ε times the carrier (ε = 0.001 or 0.002) puts partial *n* at n + (n−1)·ε
and, where the lower sidebands fold back, also at n + (n+1)·ε, so shared
partials beat at most a few times ε times the fundamental: under a hertz at
middle C for ε = 0.001. `piano.electric.fm` also has a strike layer at 0.51
times the carrier whose partials are off the harmonics on purpose. What remains
is the tuning's own error, such as 31-EDO's 4:3 being 5.2 cents wide, which
tuning corrections on the notes can remove.

The acoustic piano (`<additive preset="piano"/>`, also `piano.acoustic.grand`
when the SoundFont has no piano; [docs/additive.md](docs/additive.md)) puts partial *n* at
n + (n−1)·ε as well, using harmonic partials with a bounded stretch (ε = 0.003
in its `piano` preset).

## Terminal support

The terminal UI works in any terminal notcurses supports, but a modern one is
better. Terminals with the Kitty keyboard protocol (kitty, foot, WezTerm,
Ghostty) report key releases, which keyboard note entry needs to know how long
a note is held, so they can play musical chords (several notes at once); other
terminals can't. Terminals with Kitty graphics or Sixels get pixel graphics:
the spectrum analyzer and the DirAC heatmap in the scopes are drawn as real
pixels instead of braille cells, and without pixel support they fall back to
braille. See [docs/user-interface.md](docs/user-interface.md#terminal) for terminal-specific setup.

Known to work are kitty, xterm and GNOME Terminal. The full table is in
[docs/user-interface.md](docs/user-interface.md#supported-terminals).

## Launchpad support

A connected Novation Launchpad (Mini MK3 / X) becomes a drum rack or isomorphic
keyboard, a step sequencer and a clip launcher. See
[docs/launchpad.md](docs/launchpad.md) for the layout, colors and every button.

## Third-party code

This project is MIT-licensed (`LICENSE`). It vendors a small amount of
third-party source under `third_party/` (currently `tinyxml2`, zlib
license, and PocketFFT, BSD-3-Clause - the FFT backend behind
`src/dsp/RealFFT.h`) and dynamically links against several
permissively/LGPL-licensed system libraries. See `THIRD_PARTY_LICENSES.md`
for the full picture, or run `synth --licenses` to print it.

## Roadmap / missing functionality

The major things that are missing are the following:

* A proper name
* GUI
* Lighting control (DMX/ArtNet)
* Adaptive just intonation, which tunes each note against what sounds with it in any track
* Continuous MIDI recording with retroactive capture

### Minor Missing Features

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
