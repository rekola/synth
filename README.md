# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)

A microtonal multiparadigm music production system, which combines live sequencer with a traditional tracker.

## Features

- Launchpad support and live arrangement
- Sample and note based clips
- Microtonal (12/19/31/53-EDO)
- Emacs-style buffers and keybindings
- SoundFont2
- Headless mode (for art installations or escape rooms), see [docs/headless.md](docs/headless.md)
- Ambisonic bus (binaural or stereo output)

## Principles

### The user can start creating music instantly

- No low latency requirements
- Basic instruments are immediately available: without a SoundFont, a
  built-in fallback oscillator plays every instrument

### Keyboard driven

Everything can be done with the keyboard, without a mouse.

## Building

Needs a C++17 compiler and CMake. On Ubuntu:

```sh
sudo apt install cmake g++ libnotcurses-dev libnotcurses++-dev libfmt-dev \
  libsndfile1-dev libasound2-dev libunistring-dev libsoundtouch-dev
cmake -B build
cmake --build build -j
```

`libmysofa-dev` is optional (binaural decoding); `fluid-soundfont-gm`
provides the preferred General MIDI SoundFont.

## Running

```sh
./build/synth songs/demo3.xml                    # open a song
./build/synth                                    # open songs/welcome.xml
./build/synth --render out.wav songs/demo3.xml   # render to WAV, no terminal or audio device
./build/synth --headless --autoplay songs/demo3.xml
```

The terminal UI needs a real terminal and an ALSA output device. Space
toggles playback, C-x C-c quits and M-x opens the command prompt. Run the
tests with `ctest --test-dir build --output-on-failure`.

## Launchpad support

A connected Novation Launchpad (Mini MK3 / X / Pro MK3) becomes an isomorphic
note-entry grid and a Session-view clip launcher. See
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
* Undo/redo
* Lighting control (DMX/ArtNet)
* Just tuning

### Minor missing features

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
