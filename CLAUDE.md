# synth — microtonal tracker / synthesizer

Tracker-style music production system with microtonal notes (12/19/31/53-TET).
Terminal UI (notcurses), ALSA audio output, songs stored as XML.
Formerly developed as the `syna/` subdirectory of the private `personal` repo;
full history was preserved when it was extracted into this repository.

Conceived as an amalgam of Emacs (keybinding philosophy — mark/point
selection, kill/yank, M-x) and Renoise (tracker workflow and pattern-editor
concepts), with microtonal features added. When a UI decision doesn't
already have a clear precedent in this codebase, check how Emacs and/or
Renoise handle the equivalent situation before inventing something new —
and if this software's behavior differs from Renoise's in some area, that
should be a deliberate choice (e.g. filling a gap Renoise's own users have
long requested), not an accident.

## Build

```sh
cmake -B build
cmake --build build -j
```

Produces `build/musiceditor`.

Dependencies (Ubuntu): `libnotcurses-dev libfftw3-dev libfmt-dev
libsndfile1-dev libasound2-dev` plus CMake and a C++17 compiler.
`libmysofa-dev` is optional (binaural ambisonic decoding, `SYNTH_ENABLE_BINAURAL`,
auto-detected) — without it, `--ambisonic` still works via the cardioid
stereo decoder fallback.

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
Options: `--samplerate N`, `--stereo | --ambisonic [order]`, `--demo [n]`.
`--ambisonic [order]` renders through an ambisonic bus (ACN/SN3D, AmbiX
convention) instead of the plain stereo pan path, up to 2nd order (`order`
1 or 2 — a hard ceiling, not a stepping stone to 3rd; `kAmbisonicOrder` in
`AmbisonicEncoding.h`), decoded to binaural (HRIR-convolved, via libmysofa,
when `SYNTH_ENABLE_BINAURAL` is on and a SOFA file resolves) or a cheap
cardioid stereo matrix otherwise — see `AmbisonicEncoding.h`/
`AmbisonicDecoders.h`. The binaural decoder's virtual speaker layout
depends on order: 1st order (4 channels, W/Y/Z/X only) keeps an 8-speaker
cube — a 12-speaker icosahedron wouldn't add anything decoding from just 4
basis functions; 2nd order (9 channels) moves to a 12-speaker icosahedron,
which is what actually exploits the 5 additional degree-2 basis functions
for finer spatial resolution (`AmbisonicBinauralMixer.cpp`). There is no
`--mono`: it was never a useful device-output mode; `ChannelConfiguration::MONO`
survives only as an internal value voices/leaf instruments reduce to before
constructing themselves (`reduceForPositionalGroup`), regardless of mode.
**Space** toggles playback, Ctrl-Q quits, Ctrl-N creates a new song,
**Ctrl-K** opens the M-x command minibuffer (reliable on any terminal; see
below for why it exists alongside Esc-x/Alt-x).
`docs/commands.txt` lists the pattern effect commands (slides, vibrato, …).

Pattern editor selection uses Emacs keybindings: **C-SPC** (or **C-b**, see
below) sets the mark (selection start), **C-w** kills (cuts) the marked
block, **M-w** copies it, **C-y** yanks (pastes) the clipboard at the
cursor, **C-g** cancels the selection. The selection is a rectangular
row×track block; within a single track it's further scoped to note
columns (voices) — see below.

There's always a region to act on, even with no mark set: it degenerates
to just the single note the cursor is currently on
(`PatternEditor::getEffectiveSelectionBounds`) — `kill-region`/`kill-ring-save`
never say "No selection" anymore, they just act on that one note. Marking
and moving the cursor (rows, or sideways through note columns within one
track) extends the region from there the usual way.

Because the effective region always exists, it's also always shown —
`PatternEditor::renderRow` no longer has a separate "current column"
highlight; that color (`styles.highlight_fg_color`/`highlight_bg_color`,
bright green) was folded into the region highlight instead, so the
degenerate (unmarked) case visually looks exactly like the old
single-cell cursor highlight, and a real/widened mark shows the same
color across its whole extent. The per-character underline for
EFFECT/VELOCITY/DELAY columns (indicating which hex digit `C-+`/`C--`-style
subcol editing is about to touch) is untouched, still driven by the exact
cursor cell regardless of the region's extent. Velocity/delay's own bright
colors (tuned for contrast against the normal dark background) switch to
the region's dark foreground when inside it, matching the note column,
since bright-on-bright would otherwise be unreadable. When the cursor is
on the effect column, the region always widens to every note column of
that track/row regardless of any mark — an effect command applies to the
whole row, so there's no such thing as a partial effect-column selection.

`getEffectiveSelectionBounds()` must be computed *after* `render()` commits
`current_cursor` from `new_cursor`, not before — it reads `current_cursor`,
so computing it too early shows the region lagging one frame behind actual
cursor movement (most visible when crossing a track boundary). Similarly,
`kill-region`'s column-scoped path doesn't blindly reset the cursor to
column 0 (only a whole-track kill does that) — but killing a track's only
remaining note in its *last* voice slot shrinks `num_subtracks_` (derived
from the widest row left in the pattern), which can silently reinterpret a
stale column index as a different column (e.g. the effect column) rather
than simply going out of bounds — `getNoteNumber()`-based clamping (not a
raw index bounds check) catches this and falls back to the last surviving
voice.

Killing/copying/yanking while the cursor is on the effect column
(`SelectionBounds::includes_command`, set by `getEffectiveSelectionBounds`)
also captures/clears/restores the row's effect `Command`, matching the
region's visual widening described above — otherwise the row would *look*
fully selected while `kill-region` silently left the `Vxx`/`Uxx`/etc. text
untouched. `transpose-region-up`/`-down` deliberately never touch `Command`
even in this case (`Command.h` has no numeric/transposable semantics).
`copyPatternBlockNotes`/`clearPatternBlockNotes`/`pastePatternBlockNotes`
(`PatternBlockOps.h`) take an `include_command` parameter for this; the
effect column's own character validation stays permissive (any letter, not
just hex `a-f`) since `docs/commands.txt`'s mnemonic commands (`U`/`D`/`G`/
`V`/`I`/`O`/`T`/`M`) use letters outside the hex range — only the
velocity/delay nibble-entry path was tightened to strict `0-9a-f`,
matching Renoise's own hex-entry convention.

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

Keybinding dispatch is centralized, Emacs-style (v1, partial): `KeyChord.h`/
`Keymap.h`/`CommandRegistry.h` provide a chord→command-name→callable lookup,
and `UIElement::dispatchCommand()` runs it. Widgets populate their own
`keymap_`/`commands_` in their constructor (see `PatternEditor`'s 5 selection
commands and `UI`'s `quit`/`new-song`/`toggle-playing`) and call
`dispatchCommand(input)` early in their `offerInput()` override; anything
not bound falls through to the widget's existing manual `offerInput` logic
unchanged. `StatusLine`'s M-x minibuffer still calls
`Controller::sendCommand()`, which now falls back (via
`Controller::setCommandFallback()`, wired once in `UI::initialize()`) to
`UI::executeCommand()` — checks the active element's registry, then UI's own
— for any name it doesn't recognize itself, so M-x can invoke both
Controller-level commands (`save-song`, `add-filter`) and the per-widget ones
(`set-mark`, `kill-region`, `transpose-region-up`/`-down`, …) through the
same path. Not yet migrated: `StatusLine`, `HierarchyView`/`InstrumentList`
(dead code anyway), and Ctrl-L (deliberately left manual — it needs to keep
falling through to `StatusLine` so an unrelated keypress after `ESC` still
cancels a half-typed M-x sequence). `StatusLine`'s M-x detection accepts
three ways in: the two-step `ESC` then `x` sequence, a single Alt/Meta-
modified `x` event (some terminals merge them), and **Ctrl-K**, a plain
control byte that works everywhere — needed because on GNOME Terminal
(VTE) neither of the other two ever fires at all (confirmed: ESC is
silently dropped rather than played back as a literal keystroke as
notcurses's own docs describe; see `docs/known_bugs.md`). This
is the same fix pattern as Ctrl-B for Ctrl-SPC above. `UIPlane::showReader()`
now takes an optional prompt string and draws it *before* creating the
reader's (opaque) child plane, offsetting the reader past it — the prompt
used to be drawn via a separate `setMessage()` call *after* `showReader()`,
which silently no-oped since `setMessage()` defers to `pending_message`
whenever `readerActive()` is already true, so the "M-x " prompt was never
actually visible even when the minibuffer genuinely opened and worked.

`transpose-region-up`/`transpose-region-down` (Ctrl+Shift+Up/Down) transpose
the effective region (same "always something to act on, even unmarked"
rule as above — see `getEffectiveSelectionBounds`) and never clear the
mark, so repeated presses keep working on the same block. To transpose a
whole track or pattern, select it first — there's no separate "no mark"
whole-pattern fallback anymore.

A track with multiple simultaneous note columns (chords/polyphony —
`VisibleTrackInfo::num_subtracks_`, derived from however many notes
actually appear in any visible row, not a fixed cap) can have its
selection scoped to just some of those note columns: a fresh mark starts
on the single note column the cursor is on; moving sideways widens/narrows
across the track's note columns. To select the whole track, widen across
all of them. `PatternBlockOps::{copy,clear,transpose,paste}PatternBlockNotes`
are the single-track, note-range-scoped siblings of the whole-track
functions used for this.

The FFT spectrum/volume-meter charts (`Chart`/`TerminalChart`/
`TerminalPixelChart`, `Chart.h`/`TerminalUI.cpp`) render via real pixel
graphics (sixel/Kitty graphics/iTerm2, whichever the terminal negotiates)
when `notcurses_check_pixel_support()` reports support, falling back to
`ncplot`'s braille dots otherwise — chosen once at startup via a small
factory in `TerminalUI::initialize()`. `TerminalChart`'s underlying `ncplot`
widget takes ownership of (and destroys) whatever `ncplane` it's given, so
on resize it's given a fresh disposable child plane rather than reusing the
chart's own; giving it the chart's own plane instead would destroy the
chart's screen real estate the next time the plot gets torn down (confirmed
via a standalone reproduction — resizing a plane after destroying its
`ncdplot` segfaults). Both a stale-geometry-on-resize bug and the pixel
renderer's chunked-frame verification were confirmed with a pty+notcurses
test harness (drive real notcurses through a pty, answer its capability
queries, feed keystrokes as raw bytes or Kitty CSI-u sequences) rather than
by inspection alone.

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
- `AmbisonicEncoding.h` — ambisonic encode/decode math (SN3D gains up to
  2nd order via `AmbisonicGains`/`computeAmbisonicGains`, per-voice
  gain-interpolated encoder, stereo decode/re-encode helpers) shared by
  every ambisonic-aware node. `AmbisonicDecoders.h` — the master-bus
  `Mixer` subclasses (`AmbisonicStereoMixer`, always available;
  `AmbisonicBinauralMixer`, libmysofa-gated). `MixerFactory.{h,cpp}` picks
  between `BasicMixer`/these two, given the process-wide `ChannelConfiguration`
  and `Controller`-level `MixerType` setting — used by both `Player` and
  `OfflineRenderer`/`--render` so they exercise identical mixer-selection
  logic. (A prior, incompatible ambisonic attempt, `HRFT.{cpp,h}`, predated
  the current `Mixer`/`SampleData` interfaces and was never in the build;
  deleted rather than revived.)
- `SampleData.h`'s `Channel` enum (`Mono`/`Left`/`Right`/`W`/`Y`/`Z`/`X`/
  `Acn4`..`Acn8`/`SendA`/`SendB`) names a buffer's raw channel indices by
  *presence*, not by an explicit table: a channel's raw index is however
  many other present channels come earlier in the enum's declaration
  order (`SampleData::hasChannel`/`getChannel`). `ChannelConfiguration`
  itself stays completely ignorant of `SendA`/`SendB` — they're layered
  onto any configuration by whoever constructs a `SampleData` (the plain
  `ChannelConfiguration`-based constructor never marks them; the
  vector-of-`Channel` constructor does, e.g. a leaf voice building
  `{Mono, SendA}`). `regularChannelsFor(config)` returns the "regular"
  (non-send) channel list a `ChannelConfiguration` implies, for building
  that vector. `SampleData::mixNamed()` is `mix()`'s sends-tolerant
  sibling — same exact-match/mono-broadcast rules, but a send present on
  only one side is silently ignored (rather than asserting) instead of
  requiring both sides to match exactly.
- `SendA`/`SendB` are user-configurable per-`InstrumentTrack` amounts
  (`sendA`/`sendB` XML attributes, `InstrumentTrack::getSendA()`/
  `getSendB()`), threaded down through `Track::playNote(...)`'s shared
  signature to every leaf voice (`InstrumentVoice::getSendA()`/`getSendB()`)
  — any instrument type can send, not just SoundFont. A `SoundFontVoice`
  additionally combines the track's knob with its own SF2 region's
  `reverbEffectsSend`/`chorusEffectsSend` generator data (parsed in
  `SoundFont.cpp`'s `tsf_region`/`genMetas` table, generators 15/16 —
  additive-then-clamped via `SoundFontVoice::totalSendA()`/`totalSendB()`,
  mirroring SF2's own generator-merge convention). `TrackState::renderChildren`/
  `InstrumentTrackState::render` decide an accumulator's exact shape by
  rendering every active child/voice *first*, then checking the real
  results' `hasChannel(SendA/SendB)` — not a separate non-rendering
  prediction, since the rendered output already answers the question.
  `InstrumentTrackState::render(frames, instruments, context)`'s chunked
  loop (new voices can trigger mid-block) defers the shape decision the
  same way: it collects each chunk's `(offset, SampleData)` first, then
  builds the final accumulator from their union and places each chunk via
  `SampleData::assignNamed()` (assign()'s sends-tolerant sibling, mirroring
  mixNamed()) — so a voice that starts mid-block with a send not seen
  earlier in the same block is captured immediately, not just next block.
  `PositionalMixer::encode` handles a leaf voice's optional trailing
  `SendA`/`SendB` by straight-summing them (no spatial gain-encoding,
  since a send isn't a positional signal), asserting the voice's channel
  count is exactly `1 + sendCount()`. Sends do reach each `Mixer`
  subclass's own accumulator (via `mixNamed()`, so a track's send-carrying
  output never trips an exact-channel-count assert) but every `Mixer`'s
  `encode()` deliberately never reads them — unprocessed sends there would
  just sound bad without real reverb/chorus DSP consuming them first.
- That DSP is `SongState`'s `SendBusProcessor` (`SendBusProcessor.h`/`.cpp`),
  not anything inside the `Mixer` hierarchy: `SongState::render()` sums
  `SendA`/`SendB` off every top-level track's own rendered output (each
  already correctly summed within its own subtree) into two persistent
  mono accumulators, always runs them through `SendBusProcessor::process()`
  (even when both are silent, so the reverb tail/chorus modulation state
  stay continuous across blocks - the same reasoning as
  `AmbisonicBinauralMixer`'s overlap-add tail), encodes the resulting
  stereo wet signal into the bus's own shape (`encodeStereoAsPoints` for
  ambisonic, direct for stereo, downmixed for mono), and hands it to the
  mixer via one more `mixer.accumulate()` call - no changes needed to
  `Mixer`, `OfflineRenderer.cpp`, or `Player.cpp`. `SendBusProcessor` owns
  one shared `MVerb<float>` (fed by `SendA`, fixed reasonable parameters,
  `MIX` fixed to 1.0 - fully wet, since dry/wet balance is already
  controlled upstream by each track's own `sendA` amount) and one shared
  `effects/ChorusEngine.h`/`.cpp` instance (fed by `SendB`, `decorrelate =
  true`, `mix = 1.0`). `ChorusEngine` is a multi-voice, LFO-modulated,
  linearly-interpolated delay-line chorus that never mixes across
  channels (each channel's wet signal comes only from that channel's own
  delayed content) - the same engine also replaced the per-track `Chorus`
  effect's old hand-rolled single-voice implementation (`effects/Chorus.h`/
  `.cpp`, XML attributes `voices`/`rate`/`delay`/`depth`/`mix`), with
  `decorrelate = false` there so a channel with no signal (e.g. the silent
  side of a hard-panned source) stays silent - width is never invented
  where the input didn't have any, only synthesized from an initially-
  identical duplicated-mono signal (the shared bus's case).
- `songs/` — example/test songs (XML, hand-editable).
- `docs/` — note-number tables for various EDOs, key bindings, MIDI notes;
  `known_bugs.md` tracks open, not-yet-fixed bugs (as opposed to `todo.txt`'s
  long-lived feature/idea backlog).
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
