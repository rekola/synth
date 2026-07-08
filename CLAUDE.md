# synth — microtonal tracker / synthesizer

Tracker-style music production system with microtonal notes (31-TET and just
intonation). Terminal UI (notcurses), ALSA audio output, songs stored as XML.
Formerly developed as the `syna/` subdirectory of the private `personal` repo;
full history was preserved when it was extracted into this repository.

## Build

```sh
cmake -B build
cmake --build build -j
```

Produces `build/musiceditor`.

Dependencies (Ubuntu): `libnotcurses-dev libfftw3-dev libfmt-dev
libsndfile1-dev libasound2-dev` plus CMake and a C++17 compiler.

## Run

```sh
./build/musiceditor songs/demo3.xml   # open a song
./build/musiceditor                   # start with a new empty song
```

Needs a real terminal (notcurses full-screen UI) and an ALSA output device.
Options: `--samplerate N`, `--mono | --stereo | --surround`, `--demo [n]`.
**Space** toggles playback, Ctrl-Q quits, Ctrl-N creates a new song.
`docs/commands.txt` lists the pattern effect commands (slides, vibrato, …).

Instruments are resolved from a General MIDI SoundFont, discovered
automatically (`findDefaultSoundFont()` in `Controller.cpp`): a project-local
`data/FluidR3_GM.sf2` override first, then well-known GM fonts by name in
`~/.local/share/{soundfonts,sounds/sf2}`, `/usr/share/soundfonts` and
`/usr/share/sounds/sf2` (where Ubuntu's alternatives-managed `default-GM.sf2`
lives), then the largest `.sf2` in those directories. On Ubuntu,
`fluid-soundfont-gm` provides the preferred FluidR3_GM.sf2. Without any
SoundFont, `genericInstrument` songs play silence. `data/` is gitignored.

## Layout

- Root `*.cpp/*.h` — engine and UI. Key classes: `Controller` (application
  logic), `Song`/`Pattern`/`Track` (song model), `Player` (sequencer),
  `AlsaAudio` (output), `TerminalUI`/`PatternEditor`/`HierarchyView`
  (notcurses UI), `Tuner`/`Tuning` (microtonal pitch math),
  `OscilatorVoice`/`GenericInstrument`/`SoundFont` (synthesis).
- `effects/` — audio effects (reverb, chorus, delay, compressor, …).
- `HRFT.{cpp,h}` — ambisonic binaural mixer; currently **excluded from the
  build**: it predates the current `Mixer` interface and its SOFA data files
  and `libspatialaudio-dev` are missing. `Player` falls back to `BasicMixer`
  for `mixer="hrft"` songs.
- `songs/` — example/test songs (XML, hand-editable).
- `docs/` — note-number tables for various EDOs, key bindings, MIDI notes.
- `tools/` — helper scripts (e.g. `minimal_edo.pl`).
- `todo.txt` — long-lived idea backlog, not a list of in-progress work.
- `tinyxml2.{cpp,h}` and `effects/MVerb.h` are vendored third-party code; do
  not reformat or refactor them.

## Conventions

- C++17, no exceptions ethos in audio path; state objects (`*State.h`) are
  separated from song model objects so playback state can be reset cheaply.
- The build enables many `-Werror=` flags plus `-Wsign-conversion`; new code
  must compile warning-clean.
- "Oscilator" (single l) is the established spelling in this codebase; keep it
  for consistency.
