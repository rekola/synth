# synth

[![Linux x86-64](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-x86_64.yml)
[![Linux ARM64](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml/badge.svg)](https://github.com/rekola/synth/actions/workflows/ci-linux-arm64.yml)

![Screenshot of Session View](https://private-user-images.githubusercontent.com/6755525/660292924-cc8e0c7f-6c65-4844-af52-765658d019a7.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3OTA1OTkzMDksIm5iZiI6MTc5MDU5OTAwOSwicGF0aCI6Ii82NzU1NTI1LzY2MDI5MjkyNC1jYzhlMGM3Zi02YzY1LTQ4NDQtYWY1Mi03NjU2NThkMDE5YTcucG5nP1gtQW16LUFsZ29yaXRobT1BV1M0LUhNQUMtU0hBMjU2JlgtQW16LUNyZWRlbnRpYWw9QUtJQVZDT0RZTFNBNTNQUUs0WkElMkYyMDI2MDkyOCUyRnVzLWVhc3QtMSUyRnMzJTJGYXdzNF9yZXF1ZXN0JlgtQW16LURhdGU9MjAyNjA5MjhUMTIzNjQ5WiZYLUFtei1FeHBpcmVzPTMwMCZYLUFtei1TaWduYXR1cmU9MzljOGExNTNhNTU3YjEyM2E2MDJlMzg5N2E1OTM0NGVkMjdhZWQyM2RhNzFhOWU2ODVlNzFiOGRlZWU1YTJmNyZYLUFtei1TaWduZWRIZWFkZXJzPWhvc3QmcmVzcG9uc2UtY29udGVudC10eXBlPWltYWdlJTJGcG5nIn0.lGIy1dEvSZTFJEUGGy3a8-BkllA8NvsiCE-IhXsz1NE)

A microtonal multiparadigm music production system, which combines live sequencer with a traditional tracker.

# What synth is

synth is not a copy of any one product. It draws on several traditions, and
each document in `docs/` says where its own feature comes from and where it
differs.

- **Tracker.** Music is written as rows in a grid, one column per voice. The
  first known tracker is Ultimate Soundtracker (Obarski, 1987): four channels,
  each row a note, a sample and an effect. Scream Tracker, FastTracker 2 and
  Impulse Tracker added a volume column and lettered effect commands, and
  Renoise (2002) several note columns per track and a delay column. synth's
  pattern editor follows the later trackers; see
  [docs/commands.md](docs/commands.md).
- **Live sequencer.** Session view, with clips launched on the bar and scenes,
  follows Ableton Live; see [docs/launchpad.md](docs/launchpad.md) and
  [docs/scenes.md](docs/scenes.md).
- **Pad controllers and samplers.** The Launchpad's buttons follow Novation,
  and the drum pads and step sequencer the Akai MPC (1988) and Ableton Push;
  see [docs/drums-and-sequencer.md](docs/drums-and-sequencer.md).
- **Emacs.** Selection, kill and yank, and the M-x prompt; see
  [docs/terminal.md](docs/terminal.md).

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

A connected Novation Launchpad (Mini MK3 / X) becomes a drum rack or in-key
scale keyboard, a step sequencer and a Session-view clip launcher. See
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
* Tempo control
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
