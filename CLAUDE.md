# synth — microtonal tracker / synthesizer

Tracker-style music production system with microtonal notes (12/19/31/53-EDO).
Terminal UI on cellui's own terminal backend (`lib/cellui`), an optional GTK
window, ALSA/PipeWire audio output, songs stored as XML. Formerly the `syna/`
subdirectory of the private `personal` repo; full history was preserved when
it was extracted.

Conceived as an amalgam of Emacs (keybinding philosophy — mark/point
selection, kill/yank, M-x), classic step-sequencer trackers (pattern-editor
concepts, tracker workflow), and live sequencers (clip launching, live-style
performance), with microtonal features added. When a UI decision doesn't
already have a clear precedent in this codebase, check how that lineage
handles the equivalent situation before inventing something new - and if this
software's behaviour differs from it in some area, that should be a deliberate
choice, not an accident.

## Where the detail lives

This file holds only what every session needs. Detail loads when you touch
the relevant files:

- `lib/cellui/CLAUDE.md` — the UI toolkit's internals (planes, widgets,
  input dispatch, the terminal/GTK/Win32 backends, charts, images).
- `src/ui/CLAUDE.md` — the interactive UI: pattern editor, selection model,
  keymaps, command palette, views, clip grid, arrangement grid.
- `src/launchpad/CLAUDE.md` — every Launchpad gesture, mode and LED.
- `.claude/rules/song-model.md` — `Song`, the document/undo journal, clips,
  Live View playback semantics, scenes and bars.
- `.claude/rules/audio-engine.md` — devices, the ambisonic bus and binaural
  decoders, `AudioBuffer` channels, sends, the send bus, effects, instruments.
- `.claude/rules/windows.md`, `.claude/rules/macos.md` — the ports.
- `docs/` — user-facing documentation; `glossary.md` defines the project's
  own terms (row, swing, groove, tuning correction, ...) - check it before
  using one of those words in new code or UI text, and add to it when
  introducing a term. `known_bugs.md` tracks open, not-yet-fixed bugs.
  `user-interface.md` (shortcuts), `commands.md` (pattern effect commands),
  `launchpad.md`, `devices.md`, `headless.md`, `spatial.md`, `scenes.md`,
  `time_signatures.md`, `drums-and-sequencer.md`, `building.md`.

## Build

```sh
cmake -B build
cmake --build build -j
```

Produces `build/synth`. Dependencies (Ubuntu): `libfmt-dev libsndfile1-dev
libasound2-dev libunistring-dev libsoundtouch-dev libcairo2-dev` plus CMake,
pkg-config, git and a C++20 compiler. No terminal library: cellui writes
escape sequences itself. `libsoundtouch-dev` is not optional (pitch-preserving
time-stretch of sample clips; a resample fallback would shift pitch, a
correctness defect this codebase won't ship). FFT is vendored PocketFFT
(`third_party/pocketfft/`). canvas (`rekola/canvas`, 2D drawing for the
pixel scopes) is fetched at configure time at a pinned commit
(`lib/cellui/CMakeLists.txt`); bump the pin when canvas changes.

Optional, auto-detected, and cmake says when each isn't used:

- `libpipewire-0.3-dev` (`SYNTH_ENABLE_PIPEWIRE`) — only lists the audio
  server's own inputs/outputs for the device pickers; without it the lists
  fall back to ALSA sound cards.
- `libgtk-4-dev` (`SYNTH_ENABLE_GTK`, GTK 4.10+, Linux only) — the window
  backend (`lib/cellui/src/gtk/`) and `--ui raster`. `cellui_gtk_tests`
  needs a display, so ctest runs it under `xvfb-run` (plus `xdotool`) and
  skips it where neither is there.
- `libmysofa-dev` (`SYNTH_ENABLE_BINAURAL`) — binaural ambisonic decoding;
  without it the cardioid stereo decoder is used.

`SYNTH_BUILD_INTERACTIVE` (default ON) selects the full `synth` over the
render-only one (`src/main_render.cpp`); `SYNTH_BUILD_TERMINAL_UI` (default
ON) adds the interactive UI. Windows and macOS builds: `docs/building.md` and
the rules files above.

## Run

```sh
./build/synth songs/demo3.xml                    # open a song
./build/synth                                    # open songs/welcome.xml, or a fresh empty song
./build/synth --render out.wav songs/demo3.xml   # headless render to WAV
```

- `--ui auto|terminal|raster|headless`. `auto` (default) tries the GTK window
  when the build has it and a display is there, else the terminal; the
  choice is made once at start (`cellui/BackendChoice.h`). The e2e scripts
  pass `--ui terminal`.
- `--ui headless` (`HeadlessUI`, `src/ui/`): no screen; plays the song,
  keeps a Launchpad working, prints timestamped status to stderr; `--autoplay`
  starts the transport, `--daemon` (`--log-file`/`--pid-file`) detaches.
  `docs/headless.md`; `tools/e2e/verify_headless.py`.
- `--render` needs no terminal or audio device: renders offline (plus the
  release tail until silence, capped at 10 s) and exits — use it to verify
  audio changes and to regression-test songs.
- Other options: `--samplerate N`, `--stereo`, `--ambisonic [order]`,
  `--legacy-binaural`, `--view live|arrangement`, `--keymap gui|terminal`,
  `--playback-device`/`--capture-device`/`--midi-input`, `--list-devices`,
  `--null-audio`, `--licenses`. `--help` lists them all (`printUsage()` in
  `main.cpp`, shared ones in `printCommonOptionsUsage()`, used by
  `main_render.cpp` too) — **add a line there with a new option.**
- Devices are chosen at runtime (command palette / Devices menu) and saved in
  `~/.config/synth/devices.conf`, never in a song (`docs/devices.md`).
- With no file, `songs/welcome.xml` is opened (cwd-relative, then the
  installed copy, else an empty buffer). The UI starts in Live View on the
  clip grid; a new buffer defaults to 53-EDO. SIGINT/SIGTERM/SIGHUP end any
  UI mode cleanly.

## Tests

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- `synth_engine` (song model, playback, instruments, effects — everything but
  the UI and device backends) is a separate static library so
  `tests/synth_tests` links it without a display or audio device. Tests are
  plain functions, `TEST(name) { ... }` with `CHECK`/`CHECK_NEAR`
  (`tests/TestFramework.h`); no external framework.
- `tests/RenderTests.cpp` renders fixture songs from `tests/fixtures/` through
  the same `renderSongOffline()` as `--render` and asserts properties of the
  output (pan symmetry, channel isolation, no NaN/Inf) — how stereo/pan
  regressions get caught.
- `tests/InteractiveUITests.cpp` drives the real `InteractiveUI` on
  `cellui::test::TestBackend` (no tty) and asserts on what it shows; a modal's
  checks run in `UIFixture::inModal()`.
- A test fails for a command without a description: add a line to
  `ui/CommandDescriptions.cpp` with every new command.
- cellui's own tests: `cmake -S lib/cellui -B build-cellui`; its gallery is a
  golden file in `lib/cellui/tests/golden/` (`CELLUI_UPDATE_GOLDEN=1`).
- e2e: `tools/e2e/*.py` (see that directory's `README.md`) drive the terminal
  UI on a pty and simulated Launchpads. Wait with `Screen.wait()`, never
  `time.sleep()` — an unread pty blocks the UI thread.
- Pre-commit hook (`.githooks/pre-commit`, enable once with `git config
  core.hooksPath .githooks`): clang-formats changed lines of staged `.cpp`/
  `.h` under `src/` and `tests/` and re-stages (never whole files — existing
  code isn't uniformly formatted; a file with unstaged changes aborts the
  commit), then runs clang-tidy (`.clang-tidy`, `bugprone-*`) on staged
  `.cpp`, reading `build/compile_commands.json`.
- `-DSYNTH_ENABLE_SANITIZERS=ON` builds with ASan+UBSan.
- `SYNTH_VERIFY_CONTENT=1` makes the UI report a song write that bypassed
  `Song::Edit`; `SYNTH_VERIFY_DOCUMENT=1` replays every transaction to check
  its undo record.

## Layout

- `src/main.cpp`, `src/Controller.{cpp,h}` — above the split; `Controller`
  depends on nearly everything.
- `src/model/` — persisted song data (`Song`, `Arrangement`, `Pattern`,
  `Track`, `Clip`, `Note`, `Command`, ...) and `src/doc/` — the document
  the song lives in (nodes, journal, undo).
- `src/state/` — cheaply-resettable playback state mirroring the model
  (`*State.h`); `src/playback/` — `Player` (sequencer), its event vocabulary,
  `ClipPlayer` (clip launching), `MessageQueue`.
- `src/instruments/` — synthesis and instrument resolution; `src/effects/` —
  per-track effects; `src/bus/` — the shared send bus; `src/dsp/` —
  dependency-free DSP blocks (`bus/` depends on `dsp/`, never the reverse);
  `src/ambisonic/` — spatial encode/decode and the `Mixer` hierarchy.
- `src/audio/` — ALSA/PipeWire/WASAPI output and capture, device settings,
  `AudioBuffer`, `OfflineRenderer`; `src/midi/` — MIDI input and ports.
- `src/ui/` — toolkit-agnostic UI plumbing (`UI`, `UIElement`, commands,
  selection types) plus `InteractiveUI`, `HeadlessUI` and the app's widgets.
- `src/launchpad/` — Launchpad hardware I/O and layout.
- `src/cli/` — options shared by `main.cpp` and `main_render.cpp`.
- `src/util/` — small, dependency-free helpers used from everywhere.
- `lib/cellui/` — the UI toolkit, its own CMake project; nothing in it
  includes `src/` (a ctest enforces this). Headers `cellui/X.h`, namespace
  `cellui`.
- `tests/`, `songs/` (example songs, hand-editable XML), `docs/`, `tools/`
  (helper scripts, `e2e/`, `pacing/`, `windows/`), `plans/` (design notes,
  deleted once done).
- `third_party/` — vendored code, one subdirectory per library with its own
  licence/provenance (tinyxml2, pocketfft, fast_float). **Never reformat or
  refactor a vendored file.** `THIRD_PARTY_LICENSES.md` is the canonical
  licence list; `--licenses` prints it from a header generated at build
  time — update the `.md`, not the header.

## Invariants

Things that are easy to get wrong without the scoped files loaded:

- Every song is rendered through an ambisonic bus (ACN/SN3D, order 1-3, 3 by
  default). There is no plain-stereo mixer, no `--mono`, no 4th order.
  `--stereo` only forces the cardioid decoder over binaural.
- Every write to the song model happens inside a `Song::Edit` scope, one per
  user action; the scope bumps the version and publishes the audio thread's
  copy when the outermost one closes. A write outside any scope is a bug.
- The audio thread never reads the live model: it reads the immutable
  `PlaybackContent` that `Song::publishContent()` builds. State that outlives
  a block must not point into the tracks.
- A `Track` reference is valid only until that track is edited; look it up
  again by internal id afterwards. Change a track with
  `Song::editTrack(id, fn)`.
- Score reads return values, never references into storage; never bind a
  reference to an element of a view's result.
- A bar number is never `row / rows_per_bar`: use `BarGrid`
  (`Song::getBarsAt()` on the UI thread, `getArrangementBars()` for the
  arrangement).
- Numbers in text never go through the locale: `float_text::parse()`/
  `format()` (`util/FloatText.h`), never `strtof`, `atof`, `sscanf`,
  `std::to_string(float)`, `snprintf("%g")`, an `istringstream`, or
  tinyxml2's `SetAttribute(name, float)`.
- Commands (`CommandRegistry` names, run by both a key and M-x) live in
  `UI::initializeCommands()`, not `InteractiveUI`, unless they need a
  terminal-only prompt or widget. Pattern-editor selection/copy-paste is the
  permanent exception and stays in the widgets (`PatternEditor`, `ClipGrid`).
- There are two keymaps: the terminal one (Emacs: **C-x C-c** quits, **M-x**
  opens the palette, **C-SPC**/**C-w**/**M-w**/**C-y** mark/kill/copy/yank)
  and the desktop one (Ctrl+Q, Ctrl+Shift+P, Ctrl+X/C/V). Where Emacs has a
  convention for an action, follow it on the terminal keymap. **Space**
  toggles playback on both; **Tab** (`toggle-view`) flips views.
  `docs/user-interface.md` has the table.
- Live playback changes (launches, stops, scene changes) apply on the next
  bar, never immediately; a launch never toggles.
- Silence is structural: a buffer with nothing sounding carries no channels,
  so no stage tests samples for silence.

## Conventions

- C++20, no exceptions ethos in audio path; state objects (`*State.h`) are
  separated from song model objects so playback state can be reset cheaply.
- The build enables many `-Werror=` flags plus `-Wsign-conversion`; new code
  must compile warning-clean.
- A chord is three or more notes sounding together. A key with its modifiers
  is a *shortcut* (`KeyStroke` in code), several in a row a *key sequence*,
  never a chord (`docs/glossary.md`).
- A command only a pad controller dispatches is named with a `pad-` prefix
  (`pad-next-track`), not `launchpad-`: other devices can use it too. A pad
  gesture whose keyboard equivalent is copy/kill/yank gets no command of its
  own (`docs/user-interface.md`).
- Where a well-known convention exists in the lineage this project draws on,
  prefer it to inventing something new, and where a document records the
  lineage (`docs/drums-and-sequencer.md`, the README), keep it accurate. Add
  a layout or number only when it is known exactly - leave it out rather than
  add a subtly wrong version.
- No layout or styling tests (where something sits, how tall it is, what
  colour it has): they only restate the code. The exception is complex layout
  that keeps breaking, such as the `PatternEditor` heading; test behaviour
  (what a key, a click or a song change does) as usual.
- Every colour lives in `StyleProvider`, so one colour is one constant.
- Comments: keep them short (a one-liner covers most cases). Don't point at
  something outside the code to explain the code - state the reasoning
  directly instead of citing: a `plans/*.md` file (deleted once done),
  `todo.txt` (being phased out), a specific third-party software product
  (describe the convention generically; open-source software such as Emacs,
  obsolete software, and hardware may be named), or a specific keybinding
  when the binding is declared elsewhere (name the command instead and let
  the binding site be the only place the key appears).
- A new option gets a line in `printUsage()`; a new command gets a line in
  `ui/CommandDescriptions.cpp`; a new term goes in `docs/glossary.md`.

## Git and sessions

- Cloud sessions: commit and push finished work to the session's branch
  without being asked, including plans and changes the user wants to test by
  ear - the cloud has no other way to show files. A "don't commit" in a
  prompt means no pull request and no merge; it never means leave work
  uncommitted in the cloud. On a desktop session the user's own "don't
  commit" stands as written.
- Branches, cloud sessions only: once a branch's pull request is merged, it
  is finished. Start the next change on a new branch off the latest default
  branch (a fresh name, e.g. `claude/<topic>`) - never push more commits onto
  a merged branch, even when the session names it as the one to develop on.
  Check whether the PR is merged before committing follow-up work. On a
  desktop session the user creates and switches branches; stay on the one
  that is checked out.
- Never link to the chat/session that produced a change (no
  `Claude-Session:` trailer, no claude.ai/code URL) in commit messages, PR
  descriptions, or comments.
- Pull requests: when pushing more commits to a branch that has an open PR,
  update the PR description in the same step if the push changes what it
  says - a push updates the diff but never the description.
