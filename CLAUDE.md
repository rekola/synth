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
./build/musiceditor songs/demo3.xml                    # open a song
./build/musiceditor                                    # start with a new empty song
./build/musiceditor --render out.wav songs/demo3.xml   # headless render to WAV
```

`--render` needs no terminal or audio device: it renders the song offline
(plus the effect/release tail until silence, capped at 10 s) and exits — use
it to verify audio changes and to regression-test songs.

## Tests

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`synth_engine` (song model, playback, instruments, effects — everything
except the notcurses UI and ALSA output) is a separate static library so
`tests/synth_tests` can link it without a display or audio device. Tests are
plain functions registered with `TEST(name) { ... }` (see
`tests/TestFramework.h`) and use `CHECK`/`CHECK_NEAR`; no external test
framework dependency. `tests/RenderTests.cpp` renders small fixture songs
from `tests/fixtures/` through the same `renderSongOffline()` used by
`--render` and asserts properties of the output (pan symmetry, channel
isolation, no NaN/Inf) — this is how stereo/pan regressions get caught.

Build with `-DSYNTH_ENABLE_SANITIZERS=ON` to enable ASan+UBSan for the whole
project; useful for chasing memory bugs (e.g. `SampleData`'s copy-assignment
leak was confirmed this way).

Needs a real terminal (notcurses full-screen UI) and an ALSA output device.
Options: `--samplerate N`, `--mono | --stereo | --surround`, `--demo [n]`.
**Space** toggles playback, Ctrl-Q quits, Ctrl-N creates a new song.
`docs/commands.txt` lists the pattern effect commands (slides, vibrato, …).

Pattern editor selection uses Emacs keybindings: **C-SPC** (or **C-b**, see
below) sets the mark (selection start), **C-w** kills (cuts) the marked
block, **M-w** copies it, **C-y** yanks (pastes) the clipboard at the
cursor, **C-g** cancels the selection. The selection is a rectangular
row×track block — moving the cursor normally while marked extends it; it
always carries every column (note, velocity, delay, effect command) for
each selected cell.

`C-SPC` doesn't register on every terminal: its legacy encoding is a
literal NUL byte, which notcurses's input decoder silently drops instead of
turning into a keystroke (confirmed with the `notcurses-input` diagnostic
tool) — it only works via the modern Kitty keyboard protocol (kitty, foot,
wezterm, ghostty, …). GNOME Terminal (Ubuntu's default) doesn't support
that protocol, so `C-SPC` does nothing there. `C-@` doesn't help either —
it's byte-for-byte identical to `C-SPC` (both mask down to NUL), not a
distinct keystroke. Use **C-b** ("begin selection", already envisioned for
this in `todo.txt`) instead — an ordinary control byte that works on any
terminal.

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
