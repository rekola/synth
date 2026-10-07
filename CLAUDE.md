# synth — microtonal tracker / synthesizer

Tracker-style music production system with microtonal notes (12/19/31/53-EDO).
Terminal UI (notcurses), ALSA audio output, songs stored as XML.
Formerly developed as the `syna/` subdirectory of the private `personal` repo;
full history was preserved when it was extracted into this repository.

Conceived as an amalgam of Emacs (keybinding philosophy — mark/point
selection, kill/yank, M-x), classic step-sequencer trackers (pattern-editor
concepts, tracker workflow), and live sequencers (clip launching/live-
style performance), with microtonal features added. When a UI decision
doesn't already have a clear precedent in this codebase, check how that
lineage handles the equivalent situation before inventing something new -
and if this software's behavior differs from it in some area, that should
be a deliberate choice (e.g. filling a gap that lineage's own users have
long requested), not an accident. Emacs is cited by name throughout this
codebase's own keybinding comments (see Conventions below for the limits
on that).

## Build

```sh
cmake -B build
cmake --build build -j
```

Produces `build/synth`.

Dependencies (Ubuntu): `libnotcurses-dev libnotcurses++-dev libfmt-dev
libsndfile1-dev libasound2-dev libunistring-dev libsoundtouch-dev` plus
CMake and a C++17 compiler. The C++ bindings (`ncpp/NotCurses.hh`, what
`main.cpp`/`TerminalUI.cpp` include) ship in `libnotcurses++-dev`, a
separate package `libnotcurses-dev` does not pull in as a dependency —
both are required. `libunistring-dev` backs `src/util/Utf8.h`'s
grapheme-cluster-aware UTF-8 truncation/width helpers — its runtime half
(`libunistring5`) is already pulled in transitively by `libnotcurses-dev`,
but the headers/link package isn't, so it still needs installing
explicitly. `libsoundtouch-dev` backs `src/audio/TimeStretcher.h`'s
pitch-preserving time-stretch (a `SampleTrack` clip whose own recorded
tempo disagrees with the song's current one, `SampleTrackState::
triggerClip()`) — not optional the way `libmysofa-dev` is below, since a
degraded resample-based fallback would also shift pitch, a correctness
defect this codebase won't ship. FFT support (the live spectrum analyzer,
MagLS binaural precomputation) is via vendored PocketFFT
(`third_party/pocketfft/`) — no separate FFT library package needed.
`libpipewire-0.3-dev` is optional too (`SYNTH_ENABLE_PIPEWIRE`,
auto-detected, and cmake says so when it isn't used): it only backs
`src/audio/AudioDevices.cpp`'s listing of the audio server's own inputs and
outputs for the device pickers (below); selecting one doesn't need it, and
without it the lists fall back to the machine's sound cards (one entry per
card input/output via the ALSA control API - not `snd_device_name_hint()`'s
dozens of virtual PCMs per card); the dialog title always names which list
it is, "(PipeWire)" or "(ALSA sound cards)".
`libmysofa-dev` is optional (binaural ambisonic decoding,
`SYNTH_ENABLE_BINAURAL`, auto-detected) — without it, `--ambisonic` still
works via the cardioid stereo decoder fallback.

## Run

```sh
./build/synth songs/demo3.xml                    # open a song
./build/synth                                    # open songs/welcome.xml, or a fresh empty song if that's missing
./build/synth --render out.wav songs/demo3.xml   # headless render to WAV
```

`--headless` runs without the terminal UI (`HeadlessUI`, `src/ui/headless/`):
it plays the song, keeps a Launchpad working, and prints timestamped status
lines to stderr, plays MIDI input live and records samples; `--autoplay` starts the transport, `--daemon` (with optional
`--log-file`/`--pid-file`) detaches it. SIGINT/SIGTERM/SIGHUP end any UI
mode's main loop cleanly (`util/ShutdownSignal.h`, `UI::shouldClose()`).
See `docs/headless.md`; `tools/e2e/verify_headless.py` covers it.

**Audio and MIDI devices** (`docs/devices.md`): the output, the input and one
MIDI source are chosen at runtime (M-x `select-playback-device`/
`select-capture-device`/`select-midi-input`, the Devices menu - each opens a
modal list, `UI::showChoiceDialog()`, rendered by `tui/ChoiceDialog` over the
toolkit-agnostic `ui/ChoiceList.h`; the commands live in `UI`, so a GUI
backend only supplies the dialog) and saved in
`~/.config/synth/devices.conf` (`audio/DeviceSettings.h`, machine-wide, never
part of a song). `--playback-device`/`--capture-device`/`--midi-input`
override one run without touching the file; `--list-devices` prints the
choices. An audio name is `""`/`default`, `pw:<node.name>` (a PipeWire node -
stable across sessions, unlike its numeric id) or a raw ALSA PCM name.
A `pw:` device is opened through ALSA's own pipewire plugin with the node
named in a private one-PCM config (`openPcm()`, `AlsaAudio.cpp`), so period
sizes, polling, delay queries and xrun recovery stay the one ALSA path for
every device. `Controller::setCaptureDevice()`/`setPlaybackDevice()` save the
choice and push `SET_CAPTURE_DEVICE`/`SET_PLAYBACK_DEVICE`: the audio thread
owns the PCM handles, so `Player` makes the switch and rebuilds its poll set
(`devices_changed_`). A switch that can't happen - the node is gone, the
rate or block size can't match the running song, capture is in use - logs why
and keeps the current device. MIDI is read on the UI thread, so
`UI::selectMidiInput()` connects it there instead, never through an event; a
chosen source that is unplugged is reconnected when its port reappears
(`AlsaAudio::recordMIDI()`). A saved device that is missing at startup falls
back to the default, with the choice kept. A node name that doesn't exist is
rejected up front (`playbackDeviceExists()`/`captureDeviceExists()`) because
the server silently substitutes the default input for an unknown one.
Capturing a sink's monitor isn't supported for the same reason (naming a sink
also falls back to the default input). A missing ALSA sequencer no longer
stops audio from starting - MIDI is just off.
Identical labels are told apart by where the device is plugged in
(`DeviceLabels.h`: a node's `device.bus-path`, looked up on its device object,
cut down to the USB port; the node name if none), numbered as a last resort.
`tools/e2e/verify_device_selection.py` drives the pickers against a real
PipeWire (and skips without one).

`--render` needs no terminal or audio device: it renders the song offline
(plus the effect/release tail until silence, capped at 10 s) and exits — use
it to verify audio changes and to regression-test songs.

With no file given, `main.cpp` opens `songs/welcome.xml` by default -
resolved cwd-relative first (running from the source tree), then from
wherever `make install` put it (`InstallPaths.h.in`, baked in at configure
time from `CMAKE_INSTALL_PREFIX`), falling back to a fresh empty buffer if
neither is there. The UI starts in Live View with the clip grid
focused, not straight into note entry (`--view arrangement` starts in
Arrangement view, on the arrangement overview), and a brand new buffer
defaults to 31-EDO tuning.

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

A pre-commit hook (`.githooks/pre-commit`) works on the staged `.cpp`/`.h`
files under `src/` and `tests/`: it auto-formats them per `.clang-format`
and re-stages the result (changed lines only - the existing code isn't
uniformly formatted, so whole files are never reformatted; a file with
unstaged changes aborts the commit instead), then runs clang-tidy per
`.clang-tidy` (the `bugprone-*` checks minus the noisy ones) on the staged
`.cpp` files, aborting on a finding; that reads `build/compile_commands.json`
(any `cmake -B build` writes it). Enable it once per clone with
`git config core.hooksPath .githooks`.

Build with `-DSYNTH_ENABLE_SANITIZERS=ON` to enable ASan+UBSan for the whole
project; useful for chasing memory bugs (e.g. `AudioBuffer`'s copy-assignment
leak was confirmed this way).

Needs a real terminal (notcurses full-screen UI) and an ALSA output device.
Options: `--samplerate N`, `--stereo`, `--ambisonic [order]`,
`--legacy-binaural`, `--view live|arrangement`. Every song is always rendered through an
ambisonic bus (ACN/SN3D, AmbiX convention) — there is no plain-stereo-pan
mode at all any more, and `ChannelConfiguration::STEREO` doesn't exist as a
type (see `ChannelConfiguration.h`); `--ambisonic [order]` just sets the
order, up to 3rd (`order` 1-3 — a hard ceiling, not a stepping stone to
4th; `kAmbisonicOrder` in `AmbisonicEncoding.h`) — omitting `--ambisonic`
entirely, or giving it with no explicit number, both default to the
highest supported order (3), not 1. The bus is decoded to binaural
(HRIR-convolved, via libmysofa, when `SYNTH_ENABLE_BINAURAL` is on and a
SOFA file resolves) or a cheap cardioid stereo matrix otherwise — see
`AmbisonicEncoding.h`/`AmbisonicDecoders.h`. `--stereo` no longer selects a
different channel-configuration type — it forces the cardioid decoder
(`MixerType::AMBISONIC_STEREO`) even when binaural would otherwise be
available, the same toggle `toggle-mixer-type` flips at runtime; the
ambisonic order itself is unaffected by `--stereo`.

Binaural decoding has two implementations, chosen by
`Controller::getUseLegacyBinaural()` (`MixerFactory.cpp`): by default,
`AmbisonicMagLSDecoder` (`AmbisonicMagLSDecoder.h`/`.cpp`) — a
magnitude-least-squares decoder that precomputes one fixed HRIR-equivalent
filter pair per ambisonic channel (2·(order+1)² of them — 32 at order 3),
solved once at load time against the *entire* measured HRTF grid (not a
small speaker subset): phase-accurate least-squares below a 1.5kHz
transition frequency (preserving ITD), magnitude-only above it with phase
propagated from the previous frequency bin's own reconstructed response
(avoiding comb-filtering from raw phase discontinuities), plus a
diffuse-field covariance constraint so direction-averaged energy matches
the true measured set. `--legacy-binaural` instead selects the older
`AmbisonicBinauralMixer` — a "virtual loudspeaker" decoder: decode the bus
to a small set of directions (`speakerDirectionsFor()`, order-dependent —
1st order/4 channels keeps an 8-speaker cube, a 12-speaker icosahedron
wouldn't add anything decoding from just 4 basis functions; 2nd order/9
channels moves to a 12-speaker icosahedron, exploiting the 5 additional
degree-2 basis functions; 3rd order/16 channels moves to a 26-point
Lebedev grid), max-rE-weighted (`maxReGainsPerDegree()`/
`maxReReferenceCosine()`/`acnDegree()`, `AmbisonicEncoding.h`), each
speaker convolved against a measured HRIR pair and summed to stereo. Both
share `SofaFileResolver.h`'s `findDefaultSofaFile()` for locating the SOFA
file (project-local `data/` override first, then
`~/.local/share/sofa/default.sofa`, matching `findDefaultSoundFont()`'s own
resolution-order precedent below) and both fall back to the cardioid
decoder if none resolves. max-rE weighting is exclusive to the legacy
rig — MagLS has no notion of discrete speaker feeds to weight, its
diffuse-field constraint plays the equivalent low-order-truncation-error
role instead. The legacy rig's `gain_trim_` (per-instance output-level
scalar) is derived from the loaded HRIR set's own measured filter energy
(L2 norm, not RMS — convolution output power scales with total filter
energy, not amplitude alone) times a single calibrated constant
(`kGainTrimTarget`, `AmbisonicBinauralMixer.cpp`) — deliberately
data-derived so swapping in a louder/quieter SOFA file (e.g. the
KU100-based `HRIR_L2702.sofa` vs. an older MIT KEMAR set) can't silently
reintroduce clipping the way a purely geometric constant once did.
`dsp/RealFFT.h` (real-signal r2c-forward/c2r-inverse, PocketFFT-backed —
see the FFT backend note below) is what MagLS's precomputation uses for
its per-channel frequency-domain solve, via a plain `RealFFT<float>`
instance — the same class `dsp/SpectrumAnalyzer.h` builds on for
`Player.cpp`'s live spectrum analyzer (which layers ring-buffer
accumulation and dB conversion on top; MagLS uses `RealFFT` directly).
There is no `--mono` flag either: it was never a useful device-output mode;
`ChannelConfiguration::MONO` (conceptually 0th-order ambisonics — a single
omnidirectional/W channel, `numberOfChannels() == 1`) survives only as an
internal value voices/leaf instruments and nonlinear per-track effects
(Chorus/Distortion) reduce to before constructing themselves
(`reduceForPositionalGroup`/`reduceForEffect`), plus one synthetic
top-level test exercising a channel-generic effect loop directly
(`render_mono_with_compressor_does_not_read_out_of_bounds`) — every mixer,
`MONO` included, still ultimately decodes to a 2-channel stereo device
signal (`ChannelConfiguration::getDeviceChannels()` is unconditionally 2;
`decodeToStereo()` broadcasts a MONO/W-only bus equally to both channels
rather than asserting on it). `BasicMixer` (the old plain-stereo-pan/
raw-N-channel mixer) was retired entirely along with `STEREO`; every mixer
`MixerFactory` builds now is `AmbisonicStereoMixer` or
`AmbisonicBinauralMixer`.
**Space** toggles playback, **C-x C-c** quits (Emacs's own
save-buffers-kill-terminal binding - there is no separate Ctrl-Q quit
shortcut, deliberately: this codebase follows Emacs keybindings, not
one-off shortcuts, wherever Emacs already has a convention for the
action), Ctrl-N creates a new song, **Alt-x** opens the M-x command
minibuffer (see below for the mechanism that makes it work reliably on
any terminal, ESC-then-x included). Ctrl-K is `PatternEditor`'s own
kill-row (Emacs's own C-k, kill-line, repurposed the same way), not an
M-x trigger.
`docs/commands.md` lists the pattern effect commands (slides, vibrato, …),
split into **Implemented** (`ZBxx` pattern break; `0Pxx` azimuth set;
`0Lxx` Volume set; the real-time
`Y`-namespace commands `YLxx`/`YRxx` (azimuth slide left/right) and
`YMxy`/`YAxy`/`YBxy`/`YZxy` (Volume/Send A/Send B/azimuth, each with an
explicit glide duration) - see `SongState.h`'s command-handling loop) and
**Planned**
(accepted/stored but currently no-ops at playback time). A command's own
first character is either `Z` (global, not track-scoped), `Y` (this
engine's own reserved namespace), or this track's own chain-position
digit - always `0` today, with `-` accepted as a typed
synonym for it (`Command::updateData()`); `docs/commands.md`'s own
"Source" column names where each second-character letter actually came
from - an existing tracker's own command reference, or this codebase's
own choice - checked deliberately rather than invented blind.

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

In Arrangement view, a region acts on the notes it shows: per selected
track, whatever supplies that track at the region's anchor row (the mark,
or the cursor with no mark) - a placed clip, the Launchpad-focused clip, or
the track's background - and its rows stop where another content takes
over (`PatternSource::sourceRows()`, `ArrangementRegionGrid`). A yank writes
into whatever the cursor is on and stops where that ends. A region's
effect commands always go to the track's background, where recorded
automation lives; playback reads a row's commands from there first and
then from a placed clip's own pattern, so a clip's own command wins on a
conflict (`SongState::applyRowCommands()`, `docs/commands.md`).

Because the effective region always exists, it's also always shown —
`PatternEditor::renderRow` has no separate "current column" highlight;
the region highlight is the cursor, so the degenerate (unmarked) case is
a single-cell cursor and a real/widened mark shows the same color across
its whole extent. Every widget's cursor uses the same colors
(`StyleProvider`), all leaning cyan so they never read as the neutral
grey bar/beat highlighting: dark on bright grey (`highlight_fg_color`/
`highlight_bg_color`) while focused, plain text on a faint grey
(`highlight_unfocused_bg_color`) otherwise - an unfocused widget still
shows where its edits land (`ClipGrid` is the exception: unfocused, it shows no
cursor at all). A colored cell (a clip, an arrangement
instance) brightens toward `cursor_tint_color` instead of taking either
grey. A marked row - the pattern editor's cursor row (in Arrangement
view, the transport's row), the clip grid's scene row, the arrangement
grid's playing row - takes one tint (`StyleProvider::cursorRowTint()`).
In Live View each pattern editor column marks only its own position,
in that tint, on its own line (`PatternSource::positionRow()`) - a
playing track's playhead, a stopped one's position - so no column shows
two marked rows. Every color lives in `StyleProvider`, so one color
is one constant. The theme is `TerminalUI`'s own `styles_`: widgets get
it in `render()`, and every `UIPlane` carries it too (`getStyles()`,
handed down to child planes), so plane-level drawing - the charts, the
M-x selector, readers - uses the same instance. The per-character underline for
EFFECT/VELOCITY/DELAY columns (indicating which hex digit `C-+`/`C--`-style
subcol editing is about to touch) is untouched, still driven by the exact
cursor cell regardless of the region's extent. Velocity/delay's own bright
colors (tuned for contrast against the normal dark background) switch to
the region's foreground when inside it, matching the note column,
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
fully selected while `kill-region` silently left the `1Vxx`/`0Uxx`/etc.
text untouched. `transpose-region-up`/`-down` deliberately never touch
`Command` even in this case (`Command.h` has no numeric/transposable
semantics). `copyPatternBlockNotes`/`clearPatternBlockNotes`/
`pastePatternBlockNotes` (`PatternBlockOps.h`) take an `include_command`
parameter for this; the effect column's own character validation stays
permissive (any letter, not just hex `a-f`) since `docs/commands.md`'s
two-character mnemonics (`0U`/`0D`/`0G`/`1V`/`1I`/`1O`/`1T`/`ZB`) use
letters outside the hex range in their first two characters — only the
velocity/delay nibble-entry path was tightened to strict `0-9a-f`; a
mnemonic's own trailing hex-digit argument (e.g. `YLxx`'s slide amount)
stays permissive too, parsing a non-hex character as digit 0 rather than
rejecting it (`Command::getAzimuthSlidePerTick()`).

`C-SPC` doesn't register on every terminal: its legacy encoding is a
literal NUL byte, which notcurses's input decoder silently drops instead of
turning into a keystroke (confirmed with the `notcurses-input` diagnostic
tool) — it only works via the modern Kitty keyboard protocol (kitty, foot,
wezterm, ghostty, …). GNOME Terminal (Ubuntu's default) doesn't support
that protocol, so `C-SPC` does nothing there. `C-@` doesn't help either —
it's byte-for-byte identical to `C-SPC` (both mask down to NUL), not a
distinct keystroke. Use **C-b** instead — an ordinary control byte that works on any
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
same path. Not yet migrated: `StatusLine`, `OutlineView`, and
`InstrumentList` (dead code anyway). `StatusLine`'s M-x detection is down
to a single check now, an Alt/Meta-modified `x` event - it no longer needs
its own two-step `ESC`-then-`x` state machine, since `TerminalUI.cpp`'s
`EscapeSequenceCoalescer` (`EscapeCoalescer.h`) already folds a bare `ESC`
followed, arbitrarily later, by `x` into a single Alt-modified event
before it ever reaches `StatusLine`, the same as a terminal that sends
physical Alt-x as one event directly; see that header's own top comment
for why the wait has no deadline (modeled on Emacs's own indefinite
ESC-as-Meta-prefix behavior, "ESC-" indicator included) rather than a
short timing window. `UIPlane::showReader()`
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

The FFT spectrum (`SpectrumMeter`, `ui/tui/SpectrumMeter.h`) draws its
log-frequency bars through `LevelMeter.h`'s braille sub-cell renderer, the
same one the volume meters use. When `notcurses_check_pixel_support()`
reports support (sixel/Kitty graphics/iTerm2, whichever the terminal
negotiates), `TerminalUI.cpp`'s `TerminalPixelSpectrumMeter` overrides
`barCount()`/`drawBars()` to blit one bar per pixel column onto the
meter's own plane instead; the choice is made once at startup in
`TerminalUI::initialize()`. Both the spectrum and the DirAC heatmap show in
Arrangement view's scope row and in Live View's left column, stacked
under the full-height outline panel, each below a title bar (only while the
outline is shown and the terminal is tall enough); off-screen scopes are not updated.

Instruments are resolved from a General MIDI SoundFont, discovered
automatically (`findDefaultSoundFont()` in `Controller.cpp`): a project-local
`data/FluidR3_GM.sf2` override first, then well-known GM fonts by name in
`~/.local/share/{soundfonts,sounds/sf2}`, `/usr/share/soundfonts` and
`/usr/share/sounds/sf2` (where Ubuntu's alternatives-managed `default-GM.sf2`
lives), then the largest `.sf2` in those directories. On Ubuntu,
`fluid-soundfont-gm` provides the preferred FluidR3_GM.sf2. Without any
SoundFont, `instrument` songs don't go silent - `InstrumentProvider` always
constructs one built-in fallback instrument regardless of whether a SoundFont
loaded (currently a single generic sawtooth `Oscillator`, doing duty as a
one-size-fits-all default for every unresolved name), and `getInstrumentByName()`
returns it for any name with no better match. The plan is to grow this into
real per-path fallback instruments instead of one shared default - e.g.
resolving `piano.*` to an FM-synthesis piano rather than the plain oscillator
- but that doesn't exist yet; today there's just the one. `data/` is
gitignored.

## Launchpad

Optional hardware support (Novation Launchpad X/Mini MK3/Pro MK3, an ALSA
sequencer client auto-detected by device name -
`LaunchpadProtocol::modelFromDeviceName()`) - the terminal UI works fully
without one connected. `LaunchpadIO` owns the raw MIDI I/O (connect/
hotplug/SysEx); `LaunchpadManager` owns all per-device state and business
logic (note entry, grid-mode dispatch, Live View, drum-machine step
grid) and is Song/Controller-aware but UI-agnostic - it works the same
whether or not a terminal UI exists at all. `LaunchpadIO`'s own destructor
blanks every LED on every connected device (`clearAllLeds()`, an
all-black LED-lighting SysEx covering the grid plus every extra-button
index - `LaunchpadProtocol::allExtraButtonLedIndices()`) before closing
the ALSA connection, so quitting doesn't leave a Launchpad still showing
whatever Live View/step grid/etc. happened to be lit - not a
Programmer Mode exit (this codebase never actually leaves Programmer
Mode once entered), just going dark, a clearer and more predictable
"we're done" signal than whatever a device's own standalone light show
would otherwise resume showing.

- **`GridMode`** (`LaunchpadManager::GridMode`) - one of `NOTES`/
  `SEND_MAIN`/`PAN`/`SEND_A`/`SEND_B`/`DRAW`/`LIVE`/`CUSTOM`/`TEMPO`/
  `SWING`, mutually
  exclusive, purely per-device (`toggleGridMode()`), never tied to
  terminal UI focus - one connected Launchpad can sit in Live View
  while another stays on ordinary note entry. Defaults to `LIVE`.
  `CUSTOM` is deliberately generic ("customize whatever's assigned to
  this device") and has nothing built for it today - it shows a blank
  grid. `SEND_MAIN`/`PAN`/`SEND_A`/`SEND_B`, plus the
  track-picker overlay's three purposes (Stop Clip/Mute/Solo - see its
  own bullet below), together form Live's own **mixer submode radio
  group** (`DeviceState::mixer_mode`, off by default) - see the
  Extra-button layout bullet below for what the same seven buttons do
  while that submode is off, and how it's toggled; while it's on, only
  one of the seven is ever active at once (`toggleGridMode()`/
  `toggleTrackPicker()`/`inMixerFamily()` - pressing a different
  one always switches straight to it, even crossing between the fader-
  as-`GridMode` and picker-as-overlay mechanisms; pressing the one
  already active closes back to the plain Live grid). A press that
  switches to a genuinely different member always applies immediately
  (never waits for release to decide anything), but a real hold (>= 600ms,
  `kMixerHoldPreviewThreshold`) reverts back to whatever was showing right
  before that press once released - a momentary preview
  (`armMixerHoldPreview()`/`handleMixerFunctionRelease()`), matching the
  convention; a quick tap leaves the switch standing
  (sticky, the ordinary case). Only a press that's actually switching to a
  different member arms this - repressing the one already active (which
  closes it) never does, since there's nothing to preview-and-revert about
  turning the whole group off. Reachable only
  from `GridMode::LIVE` (a no-op from `NOTES`/`CUSTOM`/`DRAW`) - this
  is what keeps the fader column mapping (the first 8 root tracks) from
  ever disagreeing with the track-picker overlay's own column mapping
  (`live_.track_ids`, Live View's own filtered list); the two lists
  could differ, which used to read as the grid visibly "rotating"
  underneath the picker row whenever both happened to be showing
  together.
- **Extra-button layout** (raw CC, intercepted directly in
  `LaunchpadManager::handleRawButton()`/`UI::handleLaunchpadButtonEvent()`
  before any command-name resolution): 95/96/97 (Live/Note/Custom; the device labels 95 "Session" and 98 "Session Record", names kept in user-facing text, while the code stays neutral) plus DRAW are
  a true four-member exclusive group, not independent toggles - each of
  95/96/97's presses selects that mode unconditionally, even pressing the
  one already active, so the only way to leave a mode is selecting a
  *different* one of the four; DRAW is the one member not reached by a
  plain press (see its own bullet below), and the only way out of it is
  selecting one of 95/96/97. 98 is Session Record (its own bullet
  below). 91/92/93/94 are move-row-up/down/pad-prev-track/pad-next-track
  (named commands, via `LaunchpadProtocol::commandForButton()`). 91 doubles
  as a held shift modifier for opening a Live-View clip's own step
  grid directly (see the drum machine bullet below) - its own ordinary
  meaning still fires on a plain tap, just deferred to release rather
  than press (`LaunchpadManager::handleShiftButton()`, `DeviceState::
  row_up_shift_held`) so a press that turns out to combine with a pad
  never has to be undone; 92/93/94 are unaffected, still firing
  immediately on press.
  19/89/79/69/59/49/39/29 (Record Arm/Volume/Pan/Send A/Send B/Stop Clip/
  Mute/Solo, plus Pro MK3 left-column twins 30/20 for Mute/Solo) are the
  real Launchpad X's own right-column "Track control" group, all eight
  sharing one dispatch, keyed on Live's own mixer submode (`GridMode`'s
  own comment): **off** (the default) - each launches a whole scene
  instead (`LaunchpadManager::triggerSceneRow()`, `row = (cc_number - 19)
  / 10` - the classic Launchpad right-column convention, matching every
  visible track's own clip at that row simultaneously, through the same
  audition/assign (Record Arm) split an ordinary Live pad press
  already goes through) - **on** - each is the mixer radio group instead:
  Volume/Pan/SendA/SendB enter that fader `GridMode` (`toggleGridMode()`),
  Stop Clip/Mute/Solo/Record Arm open/retarget the track-picker overlay
  (`toggleTrackPicker()`) with their own purpose. Either way Mute/Solo no
  longer act on the currently-followed track directly the way
  "toggle-mute"/"toggle-solo" (`PatternEditor`'s own `commands_`, still
  reachable via keybinding/M-x) do, and Record Arm no longer reaches
  `Controller::isNoteCaptureArmed()`'s own per-current-track toggle at all
  - that's shift + CC98 instead (see its own bullet below), reachable
  from any `GridMode`. In `GridMode::NOTES` (step grid not showing) CC19 itself
  starts/stops note capture instead (red LED, bright while capturing; ends a
  sample take through the same command). **Shift** (CC91 held) turns all eight right-side
  buttons into labelled alternate functions, in every `GridMode`
  (`handleRawButton()`'s shift branch), following the Launchpad Pro MK3's own
  shift layer where it has one: Record Arm (CC19) is Undo and Mute (CC39, Pro
  MK3 CC30) Redo (both reserved - they only say "not implemented yet"), Solo
  (CC29, Pro MK3 CC20) is the metronome click ("toggle-metronome", a click per
  beat while the transport plays, accented on the bar -
  `Player::scheduleMetronome()`; its LED is amber, bright while on), Volume
  (CC89) is Duplicate, Pan (CC79) is Delete (its own bullet below), Send A
  (CC69) is Quantise (`endQuantize()`: held with a pad press,
  `quantizeClip()` snaps that clip's notes to the nearest row; a tap with no
  pad toggles `Song::getRecordQuantize()`, "toggle-record-quantize", resolved
  on release; its LED is red/green for off/on), Send B (CC59) opens the Tempo
  view and Stop Clip (CC49) the Swing view (their own bullet below); every
  button is taken. Draw is shift + CC97 instead. Their LEDs show only those
  functions while shift is held (Duplicate cyan, Draw purple, metronome amber,
  Quantise red/green, Tempo blue, Swing orange, Delete magenta - red is
  Quantise's own off state - Undo/Redo dim white). Shift + pad selects a clip
  without launching it (below).
  User-facing descriptions of every button live in `docs/launchpad.md`.
  95 ("Session") doubles as the
  mixer-submode toggle: a repeat press while already at the plain Live
  grid with nothing from the radio group active flips
  `mixer_mode`; any press otherwise just lands on (or stays on)
  that plain grid, closing an active fader/picker first if there was one.
  Live's own LED (95) reflects this three ways: dim green when not
  showing anything from the Live family at all, bright green while
  showing it in scene-launch (the default) submode, bright **orange**
  while showing it in mixer submode instead. While mixer submode is off,
  all eight of Record Arm/Volume/Pan/SendA/SendB/Stop Clip/Mute/Solo show
  a uniform dim white (`LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR`) rather than
  any mixer-mode hue, which would otherwise misleadingly suggest a fader/
  picker is one press away; once mixer submode is on, each shows its own
  hue, bright only for whichever one is currently active.
- **Session Record** (CC98, `handleRecordButton()`) - needs press and
  release, since the tap and the long hold mean unrelated things. A tap
  is `SessionPlayer::toggleOverdub()`: from the next bar, each armed
  track's playing clip (the followed track's if none is armed) is
  overdubbed in place - the clip keeps looping and the take's rows line
  up with its loop - and while any take is in flight a tap instead stops
  them at the next bar, leaving the clips playing (nothing playing: a
  status message). A pad press never overdubs by accident: on an armed
  track a populated slot only launches, an empty one starts a fresh
  take. A take records raw timing - each note's sub-row offset goes in
  its delay (`SessionPlayer::rawStep()`) - unless `Song::getRecordQuantize()`
  is on, which snaps presses/releases to the nearest row
  (`quantizedStep()`). A long hold is Capture MIDI, a stub that only says so. With shift
  held, CC98 is the arrangement's own Record Arm instead
  (`"toggle-record-arm"`, `Controller::isNoteCaptureArmed()`, which makes
  a Live pad press write the clip into the arrangement). Its LED is
  bright red while anything records (`record_arm_led_on`), dim red
  otherwise.
- **DRAW mode** (shift + Custom) - a plain per-pad coloring toy,
  independent of Song/Track state. Shift + Custom enters it from any
  `GridMode` (same exclusive-group rule as Live/Note/Custom - only
  one of 95/96/97 leaves it), or blanks the canvas if DRAW is already
  showing.
- **Duplicate** (shift + Volume) - held for as long as Volume stays down:
  a Live pad press on a populated slot copies that clip into the slot
  below it, overwriting what is there (`duplicateClip()`,
  `ArrangementOps.h` - an independent copy under a fresh id; an overwritten
  clip's arrangement placements go with it). One hold can copy several
  clips. The terminal has no such command: a clip is duplicated by copying it in the clip grid and yanking it onto another slot (`docs/terminal.md`).
- **Select a clip** (shift + pad, `Controller::selectClipSlot()`) - moves the
  shared track cursor and the clip grid's cursor onto that slot, empty ones
  included, without launching or opening anything, so it is where the next
  recording or paste lands. Shift + Note (CC96) then opens (or closes) the
  selected clip for step editing, as the drum machine bullet below describes
  (`LaunchpadManager::selected_track_id_`).
- **Delete** (shift + Pan) - held for as long as Pan stays down, like
  Duplicate: a Live pad press deletes what its slot holds, one layer
  per press (`Controller::deleteClipSlot()`, `deleteClipOrStopButton()` in
  `ArrangementOps.h`) - a populated slot loses its clip (leaving an empty
  hole in place, so scene rows stay aligned), an empty one its stop button.
  Instant with the transport stopped or when the clip isn't sounding; a
  clip that is playing (or queued) on its track while the transport runs
  is never pulled out from under the playhead - `SessionPlayer::
  deleteClip()` stops the track at the next bar and `tick()` removes the
  clip once that has taken effect. The terminal's clip-grid `kill-region` goes
  through it too, after copying the clip to the clip grid's clipboard (the pad gesture never does). No undo or confirmation.
- **Tempo and Swing views** (`GridMode::TEMPO`/`SWING`; shift + Send B /
  shift + Stop Clip; Novation's Launchpad Pro MK3 views) - the value is
  drawn as a number on the pads (`LaunchpadLayout::renderNumber()`): the
  tens digit in white, centered, with the units digit beside it and a
  narrow 2-column hundreds digit before it in the view's colour (blue
  Tempo, orange Swing); the colour change is what lets the three touch
  with no margin, and a one-digit value has nothing white. CC91 /
  CC92 become the up / down arrows (CC91 is not shift there): a press
  steps once, a hold repeats after 400 ms every 100 ms
  (`tickNumberView()`, once per frame). Plain Send B / Stop Clip switch
  between the views without shift. Repeating the entry gesture leaves the
  view (back to `number_view_return_mode`),
  as does CC95/96/97; pads, CC93/94 and the rest of the right column do
  nothing there. Edits go through `Controller::setTempo()` (20-300 bpm,
  live: `SongState::applyTempo()` follows the song version) and
  `setSwing()` (50-75%, `Song::getSwing()`, `<song swing="">`: the second
  note of every eighth-note pair plays late, applied at playback to
  everything scheduled, `swing.h`); both are also the `tempo-increase`/
  `-decrease` and `swing-increase`/`-decrease` commands. Library rhythms
  carry a swing of their own (previewed with it; adopted by the song on
  Add to Song).
- **Note mode and the step view** - `GridMode::NOTES` is the playing
  surface, and what it shows follows the assigned track. A
  `PercussionTrack` gets a fixed 4x4 General MIDI drum rack in the
  bottom-left corner (`LaunchpadLayout::drumPadNoteForPad()`: notes 36-51 in
  order, left to right then bottom to top, the usual sampler/drum-rack
  default window - `docs/drums-and-sequencer.md`), the rest of the grid dark; there is no per-track drum list (lanes were removed - the
  rack is the one kit, and a `<lane>` element in an old song is ignored on
  load). A pitched track gets an in-key scale keyboard
  (`LaunchpadManager::resolveKeyboardNotes()`): pad (0,0) is the tonic at
  this device's octave, each column one degree of the song's scale up
  (`Song::getScaleDegreesWindow()`, `Scale::NONE` playing as major here),
  each row `kScaleRowStride` (3) degrees - a fourth - higher. The old
  isomorphic grid is no longer reachable from the Launchpad
  (`LaunchpadLayout::noteForPad()` and the consonance classification remain;
  the latter still colors the keyboard by pitch class from the tonic).

  While a clip is open for editing on a percussion or pitched track
  (`Controller::getFocusedClipTrackId()`, nothing recording a Live View
  take) the grid splits Push-style (`DeviceState::show_step_grid`): rows 0-3
  stay the playing surface, rows 4-7 are 32 steps of the *selected* sound
  (`LaunchpadLayout::stepForPad()`, left to right then bottom to top). The
  selected sound is the last pad pressed on the playing surface
  (`DeviceState::selected_step_note`, white; the first pad until one is
  pressed, reset on every clip open), identified in the pattern by value like
  any hit. A press on the surface selects and then falls through to ordinary
  note entry, so it sounds and records exactly as with no clip open; a
  press on a step toggles that sound there (`handleStepGridPadEvent()`),
  writing to the focused clip's own (live-linked) `Pattern` only - never the
  track's background Pattern, which has no pagination - and auditions what
  it just set. A pitched step also writes a note-off in the same column one
  row later, so it lasts a step. Several held notes (chords) are not
  supported; one sound is selected at a time.

  The window is `kStepWindow` (32) steps from `DeviceState::
  drum_edit_step_offset`; `resetStepGridView()` gives device i page i, and
  pad-prev-track/pad-next-track scroll every device together by `kStepGridScrollStep`
  (4). While the step view shows, move-row-up/-down shift a pitched track's
  octave instead (a drum rack has nothing to shift). The step view is opened
  two ways, both funneled through `Controller::toggleDrumClipFocus(track_id,
  clip_index)`: "toggle-record-arm" while the `ClipGrid` has focus on a
  `PercussionTrack` clip, and the Launchpad's CC91-held-as-shift + Live
  pad gesture (`LaunchpadManager::handleShiftButton()`/
  `handleLivePadEvent()`, `DeviceState::row_up_shift_pending_pad` - the
  pad half resolves on its own release, so an abandoned press never has to be
  undone), which also reaches a pitched track. Both halves light white while
  held. Opening forces every connected device into `NOTES` mode
  (`forceNotesModeOnAllDevices()`) and resets its page, octave and the
  preview clock (`preview_clock_`, separate from `SessionPlayer`'s), so every
  open starts from the same window and playhead. A lone CC95 press, or the
  same shift+pad on the same pad from the plain Live grid, closes it
  (`Controller::closeDrumClipFocus()`) and returns every device to Live
  View. Merely navigating the cursor onto a track, or recording into it,
  never shows the step view. In it, CC96 and the idle Session Record go
  fully dark (nothing left for them to do); CC91/92 are dark on a
  percussion track and CC93/94 go dark once the clip fits the devices
  connected.

- **Clips** (`Clip`, `src/model/Clip.h`; `Song::getClips(track_id)`/
  `addClip()`/`ensureClipAt()`, backed by `std::unordered_map<int,
  std::vector<Clip>> clips_by_track_`) - reusable, shareable content for
  one leaf track, outside any one arrangement position: a single `Pattern`
  (once a command can target any of a track's own parent tracks directly
  - see the Run section's own `docs/commands.md` discussion above for
  `Command`'s own chain-position digit, reserved for this but not
  implemented beyond the track's own chain position (`0`) yet - a clip
  will never need a second track's worth of content the way the
  arrangement's own per-track content does, so this is already shaped for that). A
  `SampleTrack`'s own clip carries raw audio instead, via one or more
  `SampleContent` layers (`Clip::getSampleLayers()`, a `std::deque` so an
  existing layer's own address survives a later append - `SongState.h`'s
  own render-time pending-sample-start path keeps a raw pointer to one
  alive across a render block) rather than a `Pattern` - overdubbing a
  `SampleTrack` clip (`Controller::beginSampleCapture()`) appends a new
  layer rather than replacing what's there, the same "always merges
  rather than replaces" precedent note-based overdub above already
  settled; a single-take clip (the overwhelming majority) has exactly one
  layer, and `getSampleContent()` is the layer-0 convenience every such
  call site still uses unchanged. What actually plays is
  `Clip::getMixedContent()` - layer 0 directly with at most one layer, or
  a cached, pre-mixed sum of every layer rebuilt off the audio thread
  (`Clip::rebuildMixedContent()`, `Clip.cpp`) once a take's audio is
  final, never computed live on a trigger. Editing a clip through any
  one of its placements (`ArrangementOps.h`'s `placeClipInstance()`/
  `resolveInstanceAt()`, see `ArrangementGrid` below) updates every other
  placement of it immediately - unlike a track's own inline arrangement
  Pattern, which is always an independent copy. A gap in the middle of a
  track's own clip list (a scene where this instrument is silent on
  purpose) is a real, empty `Clip` (`Clip::isEmpty()`), not a missing
  vector slot - `Song::ensureClipAt(track_id, index)` places/reuses a clip
  at an exact index, padding any earlier missing positions with fresh
  empty fillers as needed; a filler gets no id of its own until something
  actually gives it real content (the same "assign one if it doesn't
  already have one" convention `addClip()` itself follows). Every "is
  this slot populated" check reads content, not just bounds
  (`!clip.isEmpty()`), for exactly this reason - Live View's own
  per-pad LED/row display, `SessionPlayer::triggerClip()`'s
  fresh-take-vs-overdub decision, and `ClipGrid`'s own delete/rename/
  loop-toggle commands (a filler reads as "nothing here" the same as a
  genuinely out-of-bounds row - erasing one would shift every later
  clip's own index down, silently misaligning every other track's own
  scene rows against it). Persisted in a top-level `<clips>` element,
  sibling to `<tracks>`/`<arrangement>` (`<trackClips track="..."><clip
  id="..." name="..." loop="..." length="..."><pattern>...</pattern>
  </clip></trackClips>`, one `<trackClips>` per track; an empty filler
  round-trips as a `<clip>` with no `<pattern>` child at all, carrying
  `stop="false"` when the slot's stop button was removed - see below). A
  `SampleTrack` clip's `<clip>` holds one `<sample file="...">` child per
  layer instead, in take order - a single-layer clip (an old file
  included) is just the size-1 case of the same reader loop - each
  layer's own sidecar `.wav` named by `sampleSidecarPath()`'s per-layer
  suffix (`<clip-id>.wav` for layer 0, `<clip-id>_2.wav`/`_3.wav`/... for
  each later overdub). Authored
  in-app via `PatternEditor::copyToClip()` (extracts the current
  pattern-editor selection into a new clip), not hand-edited-XML-only.
- **Live View** (`GridMode::LIVE`, reached/left only via CC95/96/97/98,
  decoupled from terminal UI focus): rows are a track's own clip list
  (`Song::getClips(track_id)`), columns are the one shared cursor track
  every connected device follows (`fallback_track_index`, from
  `PatternEditor::getCursorTrackIndex()` - no per-device track-follow/
  detachment). Record Arm gates trigger-live (off) vs. assign-into-the-
  arrangement (on), the same "just play" vs. "store into the pattern"
  choice ordinary note entry already makes. Launched clips play inside
  the one transport, per track: a launch takes its track over from the
  arrangement (it plays the clip, looping or one-shot, while the other
  tracks follow the arrangement), a stop takes it over too (silent), and
  "back-to-arrangement"/"track-back-to-arrangement" hand it back. The
  audio thread owns this (`SongState::queueSessionChange()`, per-track
  `SessionTrackInfo`, `src/state/SessionTrackInfo.h`): each change is
  queued and applied on the first row of the transport's next bar
  (`Song::isBarStart(absolute_pos_)`) - even the first launch into
  silence waits for it, the live-sequencer convention; rewind to start
  from the top. Launched clips advance on a session clock - rows played,
  which a seek or pattern break doesn't move - and a taken-over track
  ignores its arrangement content and automation (pattern breaks
  aside), playing its clip's own notes and commands. Launching while the
  transport is stopped starts it (resuming every other paused clip too).
  The transport toggle only pauses - an interim choice (where an
  arpeggiator resumes, and what becomes of sustained voices, are still
  open): launched clips keep their clip and row, queued changes
  and taken-over tracks stay, and play resumes them where they were;
  voices do what the arrangement's already do (a sample track's stop,
  an instrument's are left as they are), and a note take survives while
  a sample take ends. Stopping clips stays its own action (the stop pads,
  the master column's stop row, back-to-arrangement). `SessionPlayer`
  (`src/playback/SessionPlayer.h`, Controller-owned, `Controller::
  getSessionPlayer()`) is the one place the Launchpad, the clip grid and
  the launch commands go through: it sends each change as a
  `QUEUE_SESSION_CHANGE` event with a sequence number and predicts it in
  `PlaybackInfo` until a snapshot has caught up (the same stale-snapshot
  rule as the edit position), and keeps the Live View takes, resolving
  them on bar rows it sees in the snapshots (`SessionPlayer::tick()`,
  once per UI frame). A finished take loops back on the bar its stop
  resolved on. Explicitly never the
  triggered clip's own loop length (that only decides where *it* loops,
  not when a pending change is allowed to interrupt it) and never
  immediate - an empty pad queues a stop (unless its stop button was
  removed - `Clip::hasStopButton()`: "toggle-stop-button" in the clip
  grid, or `kill-region` on an empty slot in the clip grid, which then shows no ⏹: launching that slot, alone or in its
  scene, leaves the track alone, armed or not),
  while repressing the active pad relaunches its clip from row 0 at the
  next boundary (a launch never toggles - the live-sequencer convention;
  a scene launch, the same press on every track, restarts a playing scene
  the same way); a stop releases the track's voices through their
  natural `stopNote()` tail once it actually
  takes effect (`InstrumentTrackState::stopAllVoices()`), not left
  ringing or hard-cut. Stopping
  a track this way (as opposed to a plain press retriggering/reassigning
  it) goes through the track-picker overlay - see its own bullet below.
  A pad's own identity-hue static color (`DeviceState::clip_colors`)
  gets a transport-state overlay (`clip_highlight`, `LaunchpadManager::
  ClipHighlight`) matching the convention -
  playing pulses and queued flashes a fixed green
  (`LAUNCHPAD_CLIP_GREEN_PALETTE_BRIGHT`/`_DIM`) regardless of that
  pad's own hue, via the LED-lighting SysEx's own hardware-driven
  flash/pulse lighting types (`LaunchpadProtocol::LightingType::FLASH`/
  `PULSE`) rather than a software brightness blend - the device animates
  it on its own internal clock once sent, so `refreshLeds()`'s existing
  send-only-on-change dedup means this never needs re-sending itself
  either. Those two lighting types only understand a fixed 128-entry
  palette, not arbitrary RGB, which is why they're a fixed green rather
  than each pad's own hue. An armed track (`Controller::isTrackArmed()`), or one
  recording or about to, switches its whole column from this green overlay to a red one instead
  (still `ClipHighlight`, four further states -
  `ARMED_EMPTY`/`RECORD_QUEUED`/`RECORDING`/`RECORD_STOPPING` - reached
  instead of, never alongside, the plain three, since a pad is always
  exactly one or the other): an empty slot shows static dim red, a
  press-to-record queues a red flash, an in-flight take pulses red, and
  pressing the pad being recorded into again (queuing a stop for just
  that take) flashes red the same way a fresh queue does. Reuses Stop
  Clip's own red hue rather than a fifth distinct color. A `SampleTrack`
  is the one exception to the *queued* half of this - real audio capture
  is a single, immediate, global (track_id, clip_index) target
  (`Controller::armThresholdRecording()`), never bar-quantized or fanned
  out to other simultaneously-armed tracks the way note recording above
  is, since there's only one real input stream to route through it - a
  press on an armed `SampleTrack`'s own row arms (or retargets) real
  capture right away (`SessionPlayer::triggerClip()`'s own
  SampleTrack branch: `Controller::armSessionTrackRecording()`/
  `armThresholdRecording()`, mirroring "toggle-record-arm"'s own
  Live-View-focused SampleTrack branch from the terminal), and
  pressing that same pad again cancels a still-idle arm
  (`LaunchpadManager::stopSampleTrackRecording()`) - never
  `Controller::trimSessionRecordingClip()`, the note-Pattern-specific
  finalize note-based Live recording uses, which would misread a
  SampleTrack take's own empty Pattern as "nothing was ever recorded" and
  reset its real audio length back to one bar.
- **Track-picker overlay** (`LaunchpadManager::toggleTrackPicker()`/
  `handleTrackPickerPadEvent()`/`isTrackPickerRow()`, `DeviceState::
  track_picker_active`/`track_picker_purpose`) - four purposes (Stop
  Clip/Mute/Solo/Record Arm, CC49/39/29/19 - see the Extra-button layout
  bullet above), all four members of Live's own mixer submode radio
  group (`GridMode`'s own comment covers the other four members, and the
  submode toggle itself); this bullet is about what those four purposes
  look like on the grid once the overlay is reachable at all.
  Live-View-only: pressing Stop Clip (CC49), Mute (CC39/Pro MK3 30),
  Solo (CC29/Pro MK3 20) or Record Arm (CC19) is a no-op from any other
  `grid_mode`, and every `grid_mode` reassignment site that moves off
  `LIVE` closes the overlay if it was open, so it can never be showing
  over anything else. This is what lets it light just the
  bottom grid row with one pad per selectable track and otherwise leave
  Live View's own rendering completely untouched - no dimming, and
  every row but the picker row still reaches Live View's own pad
  handling exactly as if the overlay weren't open (`isTrackPickerRow()`
  is what `UI::handleLaunchpadPadEvent()` uses to route only that one row
  here). Earlier revisions dimmed the rest of the grid and swallowed
  presses there, and could be opened from any `GridMode`; both were
  dropped once opening it over Send/Pan turned out to show the picker
  row's own track-column order (`live_.track_ids`, Live View's
  filtered list) alongside Send/Pan's *different* column order (the first
  8 root tracks) at once, which read as the grid "rotating" underneath
  the picker row - restricting both to Live View removes the only
  situation where the two mappings could ever disagree.
  The picker row's own pad colors don't use per-track identity color the
  way Live View's columns do: every pad in the row shares one hue
  naming which action is about to happen (red/Stop Clip, blue/Solo,
  yellow/Mute, red again/Record Arm - reusing Stop Clip's own hue rather
  than inventing a fifth, since the two purposes never show at once and
  red is already Record Arm's own long-established color elsewhere; the
  opener button's own LED matches whichever hue is showing), with
  brightness telling columns apart within that hue, keyed to that
  purpose's own already-armed state for that column's track - bright
  means "a clip is actually playing" (Stop Clip), "already soloed"
  (Solo), "*not* already muted" (Mute - a muted channel reads as dark,
  not lit, the same way a fader bottoming out does), or "already armed"
  (Record Arm). Picking a track there performs that
  button's own purpose (a Live-View-style quantized stop for Stop
  Clip; `Controller::toggleTrackMuted()`/`toggleTrackSolo()` for
  Mute/Solo; `Controller::toggleTrackArmed()` for Record Arm - pure
  per-track bookkeeping, the same for every track type) but deliberately
  leaves the overlay open - every purpose is
  a toggle, so several picks in a row can mute/solo/stop/arm a handful of
  tracks without reopening it each time; pressing the same opener button
  again is what closes it (with nothing picked), and pressing a
  *different* opener button while it's already open just retargets it to
  the new purpose
  (`toggleTrackPicker()`). Mute/Solo no longer act on the
  currently-followed track directly once triggered from a Launchpad this
  way - "toggle-mute"/"toggle-solo" (`PatternEditor`'s own `commands_`)
  are still reachable unchanged via keybinding/M-x.
- **`ArrangementGrid`** (`src/ui/tui/ArrangementGrid.h`/`.cpp`) - the terminal-
  side counterpart: an always-visible overview in the scope row - one row
  per bar of the arrangement, running one bar past where its content ends
  (`Song::getArrangementLength()`), columns = tracks, then one marking
  (›) each bar that has a locator somewhere in it. A placed clip instance renders as
  a colored block (that track's own identity color) spanning its own
  active length in bars, its leading bar showing a single hex digit - its
  ordinal position in that track's own clip list, the same index Live
  View's own rows address - resolved the same way real playback does
  (`ArrangementOps.h`'s `resolveInstanceAt()`). A bar with no active
  instance falls back to a page/empty-page glyph showing whether the
  background has anything there. No per-cell copy/paste - placing/moving
  clip content is `copy-to-clip`'s own job, from `PatternEditor`. Track
  selection is the one shared cursor Live View also follows; Enter
  moves the transport to the cursor's bar.
- **Views** (`UI::View`, `ARRANGEMENT`/`LIVE`) - how the active song is
  laid out, UI state independent of which buffer (song) is active; a
  buffer is just a song. Arrangement view: the scope row (with
  `ArrangementGrid`) plus `PatternEditor`. Live View: `ClipGrid`
  (`src/ui/tui/ClipGrid.h` - per-track clip slots, Sends, Direction), with
  `OutlineView` as a narrow panel on its left (shown by default,
  "toggle-outline"; the tree, with a button bar overlaid on its bottom rows
  while the cursor's row has any, details in a `?`
  popup that Escape closes at once - a widget can take a bare Escape via
  `UIElement::wantsBareEscape()` without it losing its Alt-prefix role),
  above `PatternEditor` (`TerminalUI::layout()`) - no scope row
  (Arrangement view's is optional too, "toggle-scopes"). The clip grid
  and the pattern editor share the current track; the clip grid marks the clip
  the pattern editor is editing for the cursor track
  with a pencil (✎) beside its loop icon (`ClipGrid::
  setTrackClipSource()`) - an icon, not a colour, so a Launchpad can show
  the same thing. The clip grid's cursor row is its own
  (`TerminalUI::syncLiveView()` shares only the track): it never
  moves a track's position, nor follows one - a track's position moves
  only in the pattern editor, or when a clip launched on it starts
  playing. Each track header ends in "◆IMS" (the ◆ double width):
  an orange ◆ while Live View has taken the track over from the
  arrangement, then Monitor (`LeafTrack::Monitor`, "cycle-monitor"), Mute, Solo. Monitor
  gates whether live-played input is heard (`Controller::
  isMonitoring()`): In always, Off never, Auto while the track is armed
  - and a note track also while nothing is armed, which is how pad,
  MIDI and keyboard note entry sound by default. Only live note-ons are
  gated (the Launchpad note grid and its record fan-out, MIDI and
  keyboard note entry), never clip playback or step-grid/lane edit
  auditions, and never a release. A monitoring SampleTrack hears the
  live audio input instead: `Controller::syncMonitoring()` (once per UI
  frame) sends `SET_TRACK_MONITORING`, `Player` keeps capture running
  and passes each captured block through a small `dsp::MonoFifo` to the
  next playback block, and `SampleTrackState::setMonitorInput()` plays
  it as the track's third fixed voice - through its position, sends and
  effects, unaffected by a transport stop. Only the active buffer hears
  the input.
  Each clip slot shows its transport/recording state the way its
  Launchpad pad does (`SessionPlayer::clipHighlight()`, the one
  source for both, `ClipHighlight`): a colored glyph in its icon's
  place - green for playing (▸) or queued (▹), dim green (▸) while the
  transport is paused, red for recording (●) or
  queued to record (○), dim red for an armed track's empty slot (○) or a
  take queued to stop (●). Terminal cells can't pulse, so the glyph's
  shape tells queued from running.
  The last column is the master's, scrolling with the tracks: its slots
  launch a whole scene (`LaunchpadManager::launchScene()`), its last row
  stops every track, and its Sends row shows its Send Main/A/B. Sends
  are every `Track`'s (`Track::getSends()`), not just a leaf's, so the
  master has the same parameters, stored and set the same way
  (`sendMain`/`sendA`/`sendB`, `Controller::setTrackSendMain()`/`A()`/
  `B()`): its Send Main is the song's dry mix level and its Send A/B the
  send bus's two returns, applied in `SongState::renderBlock()`; only
  leaf tracks have Mute/Solo/Monitor and Direction. Enter on a Sends row
  edits the three values. Every column has a vertical level meter
  beside its Sends/Direction rows (`LevelMeter.h`: one dB mapping, braille
  by default with sextants as an opt-in glyph set, `Ballistics` smoothing
  a block's RMS in the power domain so a low note doesn't ripple, bars
  shaded by height from green through orange to the clip red
  (`StyleProvider::meterColor()`), and a
  `PeakHold` marker floating above the bar in its right dot column). The
  pattern editor's one-cell track meters and the scope row's
  `ChannelMeter` (two channels per cell, `getChannelLoudness()`, a
  per-channel RMS) share all of it; the master's reads
  the master track's output - the dry mix plus the returns, after the
  master's levels (`SongState::getMasterMeterValue()`) - which each
  playback snapshot reports as the master's `TrackInfo`.
  "toggle-view" (Tab) flips between them, the live-sequencer convention;
  "arrangement-view"/"live-view"/"outline-view" select one directly
  (View menu). `PatternEditor` reads and writes through a `PatternSource`
  (`src/ui/PatternSource.h`): `ArrangementPatternSource` in Arrangement
  view (the arrangement's one timeline and placed clips, a single block of
  absolute rows, the transport as its cursor row),
  `ScenePatternSource` in Live View (clips directly, no locators).
  There each track has its own position - a clip (scene row) and a row
  in it: a playing track's is its playhead, which can't be moved (Up/Down
  say so on the status line), a stopped track's is wherever it was left,
  remembered per buffer. The cursor is the cursor track's position and
  every other column is shown relative to it (`ScenePatternSource::
  trackAddress()`), so one screen row can show a different clip in each
  column, and regions act on each track at its own rows (`PositionedSceneGrid`).
  Every track other than the cursor's shows its position on a line of
  its own (`ScenePatternSource::trackCursor()`, an offset from the cursor
  row, per buffer). Moving the cursor by hand moves every stopped track
  along by as many rows, each on its own line, so the highlighted rows
  move across still columns and the whole view scrolls only within
  `PatternEditor::kScrollMargin` rows of an edge (starting before the
  first clip, on blank rows, where needed - Arrangement view scrolls by
  the same margin, but never before its first row); a playing track's
  line stays put. As a track plays - the cursor track included - its
  line follows its playhead down the screen by the same margin rule,
  every other line staying where it is, and each line is held within the
  margin of an edge, its column scrolling under it there
  (`keepTrackLinesVisible()`). A looping playing clip is periodic: its line carries on
  forward through the loop point, the passes either side of the one it is in
  (the clip's previous and next repeats, `ScenePatternSource::isOtherLoopPass()`)
  dimmed - fading over a few hundred ms as the playhead crosses into a pass,
  and carrying no bar/beat accent or row tint; rows from before the launch
  are blank - the view moving with it (`takeCursorJump()`). Focusing a track moves the cursor row onto
  its line, every other line staying put; a track that stops stays where
  its playhead left it.
  Each column shows its own row numbers before its notes
  (`VisibleTrackInfo::row_number_width_`, counted into its first column;
  the shared gutter is only a margin here), its own bar/beat accents and
  dimming (a row outside its own clip), and draws blank where it has no
  row at all (before its first clip); its second heading line starts
  with the number of the clip it's in. Space toggles the
  one transport in both views. The mouse wheel
  scrolls the widget under the mouse without moving focus, and scrolls its
  view, never its cursor (so never the transport); Shift scrolls tracks
  sideways, and the next cursor move brings the view back.
  The left button picks cells: in `ClipGrid` a click moves the cursor
  and presses the slot like Enter does (a clip launches, the master's
  scene/stop-all slots fire; Sends rows only select), in `ArrangementGrid`
  it moves the cursor to the bar and track (Enter still commits), and in
  `PatternEditor` it moves the cursor to the cell (a click in a heading
  picks the track, one past the last track the locator slot) and clears the
  mark. Dragging in `PatternEditor` sets the mark at the press cell and
  extends the region to the pointer (`PatternEditor::handleMouse()`, which
  resolves x against the column spans `renderRow()` records, and moves rows
  the way Up/Down do, so a playing track's locked row stays put). A drag
  and a fresh press look alike to the widgets (the held button repeats its
  press), so each tells them apart by the release in between; `TerminalUI`
  keeps focus on the widget the press started in until then.
- **Scenes** (`Song::getSceneName()`/`getSceneTempo()`/`getSceneTimeSignature()`, `<scenes><scene name="" tempo="" timeSignature="3/4"/>...</scenes>`, by position like a track's clip list, no index stored) - a scene is a clip-list row shared by every track, with an optional name, tempo and time signature, shown in the clip grid's Master column and edited with F2 there; typed text goes through `scenename::extract()` (`SceneName.h`) ("Waltz 3/4 90 BPM" splits into name, signature and tempo, "0 BPM"/"0/4" clear them, text without one keeps the existing value). `SessionPlayer::launchScene()` sends them to the audio thread as one `QUEUE_SCENE_CHANGE` event, which `SongState::queueSceneChange()` applies on the bar the clips launch on (the first row played from a stopped transport): the tempo becomes the song tempo, the signature the running signature. Each rhythm-library template carries its signature (`RhythmPatternTemplate::time_numerator`/`time_denominator`), which Add to Song gives the scene the new clip lands in when that scene has none. Details and design decisions: `docs/scenes.md`.
- **Bars and time signatures** (`TimeSignature.h`, `BarGrid.h`, `Song`'s bar API; `docs/time_signatures.md`) - a row is a sixteenth, a signature n/d is n*16/d rows per bar and 16/d per beat (denominator 1/2/4/8/16). The song has one signature (`Song::getTimeSignature()`, `<song timeSignature="3/4">`, 4/4 unless set, `set-time-signature`) that the arrangement counts its bars in (`Song::getArrangementBars()`, a `BarGrid`: a signature counted from an origin row). A launched scene's signature is the *running signature* (`RunningBars`: signature plus origin row, the launch bar, saved as `transportTimeSignature`/`transportBarOrigin`) that overrides the song's from the origin until another scene or Back to Arrangement for every track (`SessionPlayer::returnAllToArrangement()`). The audio thread owns the running signature and the tempo a scene sets (`SongState`'s `pending_scene_`/`running_bars_`/`barsAt()`, applied in `advanceSessionTracks()` on the bar, sample-exact with the clip launches; its bar test, pattern break and `Player::scheduleMetronome()` read `barsAt()`); the UI's `Song` copies are mirrored from the snapshot by `Controller::mirrorSceneChange()` (once per `PlaybackInfo::getSceneSeq()`, so an older snapshot never overwrites a tempo edited since; `SongState` applies the song's own tempo only when the song's value changed). UI-thread consumers (Live take quantization and length, the position display, the info line) read `Song::getBarsAt()`; the arrangement grid, arrangement recording and clip placement read `getArrangementBars()`. A bar number is never `row / rows_per_bar` (`Song::getRowsPerBar()` is gone): use `BarGrid`. Live View accents come from the scene's own signature (`PatternSource::startsBar()`/`startsBeat()`).
- **Defaults**: a fresh session opens in Live View on the clip grid
  (`UI::setInitialView()`, the `--view` option) rather than straight into
  note entry, and `GridMode` defaults to `LIVE` on every
  connected device - see the Run section above for the matching
  `songs/welcome.xml`/31-EDO startup defaults.
- e2e coverage: `tools/e2e/verify_launchpad_live.py` (see that
  directory's own `README.md`) covers Live View's basic trigger/assign
  path, arming Record Arm for it via shift + CC98 (the legacy global
  `toggle-record-arm` - CC19 no longer reaches it while looking at
  Live View); `verify_launchpad_notecustom.py`/
  `verify_launchpad_draw_clear.py` cover CC96/CC97's own mode-switch
  and the shift + Solo gesture (DRAW mode entry/canvas-clear);
  `verify_launchpad_record_arm_picker.py` covers CC19's own Live-View
  meaning, the track-picker overlay's fourth purpose - presses CC95 a
  second time first to enter mixer submode (required before CC19 does
  anything at all, same as the other three), same reasoning as
  `verify_launchpad_mute_picker.py` below; `verify_launchpad_stopclip.py`
  covers the track-picker overlay's CC49 purpose above (open/pick/close,
  staying open across a pick, and a playing clip's picker pad showing
  the picker's static red rather than its playing pulse);
  `verify_launchpad_mute_picker.py` covers
  the CC39 purpose (bright/dim polarity, the overlay leaving Live
  View's own rendering untouched outside the picker row), and also exercises Live's own
  mixer-submode toggle (a second CC95 press) and its green/orange LED,
  since CC39 means nothing at all until that submode is on; Solo's own
  CC29 purpose reuses the identical mechanism but has no dedicated e2e
  script of its own yet. `verify_launchpad_scene_row.py` covers the
  scene-launch action the same eight buttons perform while mixer submode
  is off (`LaunchpadManager::triggerSceneRow()`, row = (cc_number - 19) /
  10) - every track's own clip at that row launches together off a single
  press, not just the first track in `live_.track_ids` - every track
  queues for the same next bar. `verify_launchpad_mixer_hold.py`
  covers the same eight buttons' own momentary hold-to-preview gesture
  (`armMixerHoldPreview()`/`handleMixerFunctionRelease()`) - a quick tap
  stays (sticky), a real hold reverts to whatever was showing before it
  once released. `verify_launchpad_live_automation.py` covers a fader
  move during a Live View take landing in the take's own clip
  (`recordFaderAutomationIfArmed()`) rather than the arrangement, which a
  taken-over track ignores. `verify_launchpad_sampletrack_record_arm.py` covers the
  SampleTrack twin of `verify_launchpad_record_arm_holes.py` - a
  Live-grid press on a SampleTrack armed via the track-picker overlay
  actually arming real audio capture, and a second press cancelling it -
  verified through the terminal `ClipGrid` widget's own text.
  `verify_launchpad_shift_stepgrid.py` covers
  CC91-held-as-shift's own gesture (see the drum machine bullet above) -
  opening a step-sequenced `PercussionTrack` clip's own step grid from
  Live View, and a lone CC95 press closing it again - verified the
  same terminal-text way (the "*" focus marker).
  `verify_launchpad_tempo_swing.py` covers the Tempo and Swing views
  (open, arrows, switch, leave) through the pads' LEDs.
  `verify_launchpad_shift_highlight.py` covers the same
  gesture's own LED feedback while held (both CC91 and the target pad
  lighting bright white before release). `verify_launchpad_paging_lockstep.py`
  covers the step grid's own pad-prev-track/pad-next-track page-shift gesture
  moving every connected device together rather than just whichever one
  was pressed - two simulated devices open a 3-page clip, confirm
  `resetStepGridView()`'s own device-order split put them on two
  different pages, then one pages forward once and both are confirmed to
  have scrolled together. `verify_launchpad_shift_stepgrid_pitched.py`
  covers the same gesture opening a *pitched* `InstrumentTrack`'s own
  clip - same "*" focus-marker verification as `verify_launchpad_shift_
  stepgrid.py` above.
  Real hardware and the simulators never mix (`LaunchpadIO::
  acceptsClient()`): every simulator's ALSA client name ends in `(e2e)`,
  which an ordinary `synth` skips, and every e2e script spawns `synth`
  with `SYNTH_LAUNCHPAD_NO_HARDWARE=1` (`tools/e2e/harness.py`'s own
  `spawn()`), which skips real hardware instead. The scripts wait with
  `Screen.wait()`, never `time.sleep()` - an unread pty blocks `synth`'s
  UI thread, Launchpad I/O included (see that directory's `README.md`).

## Layout

- `src/` — all engine and UI source, split by topic. `src/main.cpp` and
  `src/Controller.{cpp,h}` sit directly in `src/` rather than in any of
  the directories below (`Controller` is application logic that
  legitimately depends on nearly every other module - song model,
  state, playback, instruments, ambisonic mixer selection, UI wiring -
  so it sits above the split rather than being forced into one slice of
  it). The topic directories:
  - `src/model/` — the persisted song data: `Song`/`Arrangement`/`Pattern`/
    `Track` (`Song` holds one `Arrangement`, a single timeline keyed by
    absolute row - `<arrangement>` in the file - whose length is where its
    content ends (`Song::getArrangementLength()`), holding three kinds of
    per-track content - the arrangement layer's own
    instance events (which `Clip`, if any, starts at a given row, or an
    explicit stop - the primary way content reaches the arrangement, see
    `Clips`/`ArrangementOps.h` below; a looping clip plays on until the
    track's next event), a directly-inline `Pattern` — a
    track's own note/command content, no `track_id` in it anywhere, always
    an independent copy unlike a `Clip`'s own shared content — and a
    `SampleTrack`'s own merged background audio bed), the song's
    locators (`Song::getLocators()`, named markers keyed by absolute row,
    shown in Arrangement view's locator column)
    and their value types (`Note`, `Command`, `SendLevels`, …).
  - `src/state/` — the parallel, cheaply-resettable playback-state
    objects (`*State.h`) mirroring the model objects above.
  - `src/playback/` — `Player` (sequencer), the event vocabulary it
    consumes/produces, and `SessionPlayer` (Live View clip launching).
  - `src/instruments/` — synthesis and instrument resolution:
    `OscillatorVoice`/`GenericInstrument`/`SoundFont`, `Tuner`/`Tuning`
    (microtonal pitch math), `LFO`, `Arpeggiator`.
  `Oscillator` can be an array of members in one voice: `voices` members (up to 256),
    member k at `ratio`^k times the note's frequency and `falloff`^k times its
    level (ratio 1 = unison choir, 2 = octaves), with `detune` (cents) spread
    evenly and centred across the members (`OscillatorArray.h`; rendered by
    `OscillatorVoice`, whose waveforms come from `OscillatorKernel`, a
    vector-extension kernel). `spread` (a multiplier on the position's extent)
    is the radius of a cloud of buckets: as many as resolvable cells (25/16/12
    degrees at order 1/2/3) fit in the cloud's elliptical area, at least three (so a
    spread is 2D) and at most one per member, one when the cloud is under a
    cell wide or there's no spread. The buckets are laid out as concentric
    rings (`OscillatorVoice::ringCounts()`/`cloudPoint()`: J rings, populations
    proportional to radius), each ring turned and each point jittered per note
    (hashed from the note coordinate), and members are dealt into them round-robin. A
    bucket's members are summed and encoded once, all buckets in one
    register-accumulating pass (`AmbisonicStackEncoder`). The floor reflection
    and Aux sends run once on the summed signal at the centre. One voice is just
    the array of one. A detuned array (`detune` > 0, more than one member)
    gives each member its own slow aperiodic pitch wander, up to half the
    detune over `driftPeriod` seconds (default 2, 0 = off; `PitchDrift.h`:
    hashed value noise between control points, a pure function of the voice's
    age in samples, so it never depends on the block size; the kernel adds its
    exact phase integral per group of eight), keeping the array from settling
    into a repeating beat pattern; no detune, no drift. Other voice types aren't arrays.
  - `src/ambisonic/` — spatial encode/decode math and the `Mixer`
    hierarchy (see the `AmbisonicEncoding.h` bullet below).
  - `src/audio/` — `AlsaAudio` (device output and input, runtime device
    switching), `DeviceSettings` (the saved device choices - in
    `synth_engine`, so `Controller` and the tests use it), `AudioDevices`
    (listing what can be chosen; executable-only, like `AlsaAudio`),
    `AudioBuffer`, `OfflineRenderer`.
  - `src/ui/` — toolkit-agnostic UI plumbing with no notcurses
    dependency: `UI`/`UIElement`/`UIPlane`/`UIMenu` (abstract app/widget/
    render-surface interfaces a future non-terminal UI could implement
    against too - a concrete backend's real implementation, e.g.
    `TerminalPlane`/`TerminalMenu`, lives entirely inside that backend's
    own file, not alongside the interface), `Chart`/`HeatmapChart`
    (same pattern - `TerminalChart`/`TerminalPixelChart`/
    `TerminalHeatmapChart`/`TerminalPixelHeatmapChart` are
    `TerminalUI.cpp`'s own concrete renderers), `StyleProvider` (a plain
    named-color theme struct, not terminal-specific itself), the
    Emacs-style keybinding dispatch (`KeyChord.h`/`Keymap.h`/
    `CommandRegistry.h`), and selection/clipboard data types
    (`SelectionBounds.h`/`SelectionScope.h`/`ClipboardEntry.h`/
    `GridPosition.h`). `UI` itself owns nothing widget-shaped - just the
    app-level lifecycle every backend needs (spawning the audio/
    visualization threads, running until told to quit, reporting status)
    - a concrete subclass owns its own widget set, screen layout, and
    Launchpad callback wiring (`UI::wireLaunchpad()`'s hook). `src/ui/tui/`
    — the concrete notcurses implementation built on those interfaces:
    `TerminalUI` (the sole such subclass today - owns every widget,
    `initializeWidgets()`/`layout()`/`renderComponents()`, every Emacs
    keybinding, and every `EventHandler` override), `PatternEditor`/
    `OutlineView` and the rest of the terminal widgets, plus
    notcurses-specific input handling (`NotcursesInputEventSource`,
    `EscapeCoalescer`) and text-cell rendering helpers (`SubcellGlyphs.h`)
    that wouldn't apply to a pixel-based UI.

    Info dialogs (the outline panel's details popup, the About dialog) have
    their content written as Markdown, parsed by `ui/Markdown.h` - a tiny
    subset (`#` headings, paragraphs, `*italic*`, `**bold**`, `\` escapes)
    and nothing else - so any backend can show the same text. `UI::
    showInfoDialog(title, markdown)` is the backend hook the shared `about`
    command calls (text in `ui/AboutText.h`); `tui/InfoDialog` is the
    terminal rendering (`markdown::layout()` word-wraps to cells; a GUI
    would lay out the parsed `Document` itself). The terminal one is modal
    and closes on Escape/Enter/q/C-g or a click.

    Commands (`CommandRegistry`-named, dispatched by both a keybinding and
    M-x) default to living in `UI::initializeCommands()` (`ui/UI.cpp`),
    not `TerminalUI`, since a future GUI backend shares the same command
    names/behavior even though its own keybindings (or menu items, or
    toolbar buttons) will look nothing like the Emacs chords `TerminalUI`
    binds them to - only the *binding* is backend-specific, not what the
    command does. A command stays in `TerminalUI` instead when it
    genuinely can't be shared yet - needs a prompt/dialog (`StatusLine`'s
    minibuffer has no GUI equivalent today) or reaches directly into a
    concrete terminal widget pointer (`pattern_editor_`/
    `arrangement_grid_`) with no abstract stand-in to go through. The one
    deliberate, permanent exception is pattern-editor selection/copy-paste
    (`set-mark`/`kill-region`/`kill-ring-save`/`yank`/`exchange-point-and-
    mark`, see the Emacs mark/kill-ring model below) - a GUI's own
    copy-paste is expected to follow ordinary GUI conventions (Ctrl-C/-X/
    -V-style, not Emacs mark-and-kill), so these aren't a "not yet shared"
    gap to close later; they belong in `TerminalUI` for good.
  - `src/ui/headless/` — `HeadlessUI`, the backend with no screen: just the
    main loop, Launchpad wiring and stderr status output. Launchpad pad/button
    handling itself lives in `UI` and is shared with `TerminalUI`.
  - `src/launchpad/` — Launchpad hardware I/O and layout - see the
    Launchpad section above.
  - `src/util/` — small, dependency-free helpers (`constants.h`,
    `Logger.h`, …) used from everywhere. `Utf8.h`/`.cpp` is the one
    exception to "dependency-free" (it wraps libunistring, keeping its
    header out of `Utf8.h` itself) - still dependency-free of every other
    synth module, which is the property call sites actually rely on.

  `src/effects/`, `src/bus/`, and `src/dsp/` (below) predate this split
  and already lived in their own directories before it; this list only
  covers what used to be flat directly under `src/`.
- `src/effects/` — per-track audio effects (chorus, compressor, distortion, …)
  — each constructed fresh per track/note and torn down with it, unlike
  the shared send bus (`src/bus/`, below). There is no per-track reverb any
  more (`effects/Reverb.{h,cpp}`'s GPL-licensed `MVerb`-based
  `<reverb preset="...">` was removed) — the shared bus's spatial FDN
  reverb (`bus/FDNReverb.h`) is the only reverb left, and `<reverb>` now
  only ever means that one, as a `<bus>` child.
- `src/ambisonic/AmbisonicEncoding.h` — ambisonic encode/decode math (SN3D gains up to
  3rd order via `AmbisonicGains`/`computeAmbisonicGains`, per-voice
  gain-interpolated encoder, stereo decode/re-encode helpers, plus the
  max-rE helpers `maxReGainsPerDegree()`/`maxReReferenceCosine()`/
  `acnDegree()` used only by the legacy binaural rig — see the Run section
  above) shared by every ambisonic-aware node. `cubeVertexDirections()`
  gives the 8 cube-vertex directions (az ±45°/±135°, el ±35.264°) both the
  legacy rig's order-1 speaker layout and the shared send bus's spatial
  reverb taps encode into — a single shared source of truth, not two
  independently-declared copies of the same constants.
  `AmbisonicDecoders.h` — the master-bus `Mixer` subclasses
  (`AmbisonicStereoMixer`, always available; `AmbisonicBinauralMixer`, the
  legacy virtual-speaker-rig decoder, libmysofa-gated).
  `AmbisonicMagLSDecoder.{h,cpp}` (also libmysofa-gated) is the default
  binaural decoder — see the Run section above for how the two differ and
  how `MixerFactory.{h,cpp}` picks between all three, given the
  process-wide `ChannelConfiguration`, `Controller`-level `MixerType`
  setting, and `Controller::getUseLegacyBinaural()` — used by both
  `Player` and `OfflineRenderer`/`--render` so they exercise identical
  mixer-selection logic. There is no third, plain-stereo-pan mixer any
  more (`BasicMixer` was retired along with `ChannelConfiguration::STEREO`
  — see the Run section above): every song is always rendered through an
  ambisonic bus, `MONO` (0th-order ambisonic, W-only) included, which
  `AmbisonicStereoMixer`'s `decodeToStereo()` broadcasts equally to both
  output channels rather than needing its own separate mixer type. (A
  prior, incompatible ambisonic attempt, `HRFT.{cpp,h}`, predated the
  current `Mixer`/`AudioBuffer` interfaces and was never in the build;
  deleted rather than revived.)
- `AudioBuffer.h`'s `Channel` enum has three values, `Main`/`AuxA`/`AuxB`.
  `Main` covers every regular (ambisonic) channel as a group — addressed
  individually by plain raw index (0 = W, 1 = Y, ... in ACN order, up to
  16 at order 3), not one enum value per channel — and
  `hasChannel(Channel::Main)` is *derived*, not stored:
  `regularChannelCount() > 0` (`channels_ - auxCount()`), so it's always
  in sync with whatever channel count `channels_` was fixed to at
  construction and can never drift out of sync via a later `zero()`/
  `clear()`/`mix()`/`assign()` call — none of those mutate any presence
  flag any more (the old `is_zero_`/`isZero()`/`setNonZero()` machinery, a
  whole-buffer *content* flag conflated with this *structural* one, is
  gone). `AuxA`/`AuxB` aren't part of the fixed 0..N-1 regular-channel run
  at all (a buffer may carry either, both, or neither, independent of its
  regular channel count) — they always land immediately after the regular
  channels, `AuxA` before `AuxB` (`AudioBuffer::indexOf()`), backed by
  their own stored `has_aux_a_`/`has_aux_b_` bools (unlike `Main`, knowing
  "one aux channel is present" doesn't say *which* one, so these can't be
  derived from a count alone). A voice/accumulator with nothing routed to
  Main this block (e.g. a voice whose Send Main level is 0) simply has
  zero regular channels — it isn't allocated and then zeroed, the same way
  `AuxA`/`AuxB` already only ever get allocated when something actually
  sends to them. `getChannel(Channel::Main)` returns a pointer to channel
  0 when present, else `nullptr`, the same contract shape `AuxA`/`AuxB`
  already have. `isClipping()`/`calculateLoudness()` never gate on
  `hasChannel(Channel::Main)` — a buffer can legitimately have zero Main
  channels and real, clippable `AuxA`/`AuxB` content (a 100%-wet,
  Main-bypassing voice), so both always scan whatever real channels are
  actually present. `AudioBuffer::mixNamed()`/`assignNamed()` are
  `mix()`/`assign()`'s aux-tolerant siblings — same exact-match/mono-
  broadcast rules for the regular channels (plus an explicit no-op case
  when the other side has zero regular channels, e.g. a Main-less child
  voice mixing into an accumulator some sibling voice gave real Main
  channels to), but an aux channel present on only one side is silently
  ignored (rather than asserting) instead of requiring both sides to
  match exactly.
- `SendA`/`SendB` (kept as "Send", distinct from the `AuxA`/`AuxB` buffer
  channels above — a track *sends* A and B; what arrives on the shared bus
  is carried in the `AuxA`/`AuxB` channels) are user-configurable
  per-`InstrumentTrack` amounts (`sendA`/`sendB` XML attributes,
  `InstrumentTrack::getSendA()`/`getSendB()`), threaded down through
  `Track::playNote(...)`'s shared signature to every leaf voice
  (`InstrumentVoice::getSendA()`/`getSendB()`) — any instrument type can
  send, not just SoundFont. A `SoundFontVoice` additionally combines the
  track's knob with its own SF2 region's `reverbEffectsSend`/
  `chorusEffectsSend` generator data (parsed in `SoundFont.cpp`'s
  `tsf_region`/`genMetas` table, generators 15/16 — additive-then-clamped
  via `SoundFont.cpp`'s `adjustSendA()`/`chorusSendFor()`, mirroring SF2's
  own generator-merge convention). A voice with Send Main = 0 skips Main
  entirely — `InstrumentVoice::encodePosition()` checks `sends.main > 0.0f`
  before doing any ambisonic gain-encode work, the same presence check
  `sends.a > 0.0f`/`sends.b > 0.0f` already used for `AuxA`/`AuxB`.
  `TrackState::renderChildren`/`InstrumentTrackState::render` decide an
  accumulator's exact shape by rendering every active child/voice *first*,
  then checking the real results' `hasChannel(Main/AuxA/AuxB)` — not a
  separate non-rendering prediction, since the rendered output already
  answers the question. `InstrumentTrackState::render(frames, instruments, context)`'s
  chunked loop (new voices can trigger mid-block) defers the shape
  decision the same way: it collects each chunk's `(offset, AudioBuffer)`
  first, then builds the final accumulator from their union and places
  each chunk via `AudioBuffer::assignNamed()` — so a voice that starts
  mid-block with an aux channel not seen earlier in the same block is
  captured immediately, not just next block. Aux channels do reach each
  `Mixer` subclass's own accumulator (via `mixNamed()`, so a track's
  aux-carrying output never trips an exact-channel-count assert) but every
  `Mixer`'s `encode()` deliberately never reads them — unprocessed aux
  content there would just sound bad without real bus DSP consuming it
  first.
- That DSP lives in `src/bus/` (the shared send bus's own subsystem, depending
  on `src/dsp/` — reusable, dependency-free DSP building blocks, never the
  reverse) — `SongState`'s `SendBusProcessor` (`bus/SendBusProcessor.h`/
  `.cpp`) is not anything inside the `Mixer` hierarchy: `SongState::render()`
  sums `AuxA`/`AuxB` off every top-level track's own rendered output (each
  already correctly summed within its own subtree) into two persistent
  mono accumulators, and — only when its own `ChannelConfiguration` is
  `AMBISONIC` (skipped for the one synthetic top-level `MONO` config a
  Compressor regression test constructs directly, which has no sensible
  ambisonic tap-encode target) — always runs them through
  `SendBusProcessor::process()` (even when both are silent, so every
  slot's internal tail/feedback/modulation state stays continuous across
  blocks — the same reasoning as `AmbisonicBinauralMixer`'s overlap-add
  tail), then accumulates the result directly into the mixer with a
  single `mixer.accumulate()` call — no decode step happens in
  `SongState` itself, since the top-level mixer is always ambisonic-shaped
  too. `SendBusProcessor`'s own output (`getBusAmbisonic()`) is *always*
  ambisonic-shaped (`config.numberOfChannels()` — 4 at order 1, 9 at
  order 2), never a plain stereo signal.
  `SendBusProcessor` is a generic 2-slot effect chain (`kSlotA`/`kSlotB`),
  not a hardcoded reverb+chorus pair — which concrete `BusEffect`
  (`bus/BusEffect.h`) occupies each slot is resolved once, at song load,
  from the project file (or the compiled-in default, from
  `bus/BusEffectRegistry.{h,cpp}`: slot A = `FDNReverb`, slot B =
  `MultiTapDelay`), and never changes for that song's lifetime.
  `BusEffectRegistry` also offers `GranularCloud` (a granular-cloud send
  effect) and `NullBusEffect` (both slots default to this before
  `SongState::initialize()` installs the real ones, so `process()` is
  always safe to call even pre-load); any of the three real effects can
  occupy either slot. Every `BusEffect` shares the same shape:
  `process(monoInput, frames)` always runs, even on silent input, so
  internal state stays continuous; `getNumTaps()`/`getTap()`/
  `getTapDirection()` expose its output as N independent spatial taps
  (`FDNReverb`: 8 feedback-delay lines at the cube-vertex directions from
  `AmbisonicEncoding.h`'s `cubeVertexDirections()`; `MultiTapDelay`: 4
  taps at its own fixed azimuths; `GranularCloud`: one tap per
  simultaneously-sounding grain), each encoded into the shared ambisonic
  bus via its own `AmbisonicVoiceEncoder` (`getTapEncoder()`) at
  `getWetLevel()`'s gain. Slot B is processed first each block; its
  pre-encode tap sum (`getChainSendSum()`), scaled by its own
  `getChainSendLevel()`, is added into slot A's input before slot A
  processes — a same-block chain send (default: some of slot B's delay
  output picks up slot A's reverb too). Slot A's own chain-send ratio
  exists (every `BusEffect` has one uniformly) but is never read, since
  nothing sits after slot A. `dsp::ChorusEngine` (`dsp/ChorusEngine.h`/
  `.cpp`, a multi-voice, LFO-modulated, linearly-interpolated delay-line
  chorus) is no longer used anywhere in `bus/` — it now only backs the
  per-track `Chorus` effect (`effects/Chorus.h`/`.cpp`, XML attributes
  `voices`/`rate`/`delay`/`depth`/`mix`), with `decorrelate = false` there
  so a channel with no signal (e.g. the silent side of a hard-panned
  source) stays silent, and separately processes Main and `AuxA`/`AuxB`
  through their own independent, always-present delay-line/LFO state
  (never raw-index-shared, since Main's channel count can be 0 or full
  but never partial) — width is never invented where the input didn't
  have any.
- Nonlinear/dedicated-DSP per-track effects (`effects/Chorus.cpp`/
  `Distortion.cpp`) reduce their children to `MONO` before rendering them
  (`reduceForEffect`, `AmbisonicEncoding.h`) rather than raw ambisonic —
  real stereo panning doesn't survive underneath either of these (a
  deliberate trade-off, not a bug), and re-encoding their processed output
  back up into an ambisonic parent afterward (`reencodeIfNeeded()`) uses
  `encodeMonoAsPoint()` for the Main channel (folds into `W` only, unity
  gain — a mono signal has no direction to encode) while carrying
  `AuxA`/`AuxB` straight through unencoded, never `encodeStereoAsPoints()`
  (that needs genuine 2-channel input and is reserved for things that
  actually have it, like `FDNReverb`/`MultiTapDelay`'s multi-tap spatial
  encode above). Per-track effects otherwise touch Main and `AuxA`/`AuxB`
  alike — Amplifier/EnvelopeFilter/Compressor/Tremolo/BiquadFilter/
  Distortion/Chorus all shape whatever channels are actually present,
  since the shared reverb/delay bus should hear the same envelope/gain/
  tone-shaping the dry signal does, not a bypassed copy of the pre-effect
  signal — except Compressor's *detection* (the loudness measurement
  driving its gain), which uses Main only. Persistent per-channel
  filter/delay state (`Biquad<T>`,
  `dsp::MoogVCF<T>`, `dsp::ChorusEngine::ChannelState`) gives `AuxA`/`AuxB`
  their own dedicated, always-present slots rather than reindexing by raw
  position (Main's channel count toggles between 0 and full, never
  partial) — and once a channel has genuinely carried real data at least
  once, its state keeps being advanced through silence rather than frozen
  (`Biquad::apply(blockSamples)`/`MoogVCF::apply(blockSamples, ...)`/
  `ChorusEngine::processSilence()`, all no-buffer overloads), so it
  decays/resumes correctly instead of picking up later as if no time had
  passed. `docs/known_bugs.md` notes one known-not-fixed inconsistency
  from this: `Distortion.cpp` can distort Main and `AuxA`/`AuxB`
  differently under clipping, since they carry differently-scaled copies
  of the same dry signal and a nonlinear curve responds differently to
  different amplitudes.
- `songs/` — example/test songs (XML, hand-editable).
- `docs/` — note-number tables for various EDOs, key bindings, MIDI notes;
  `known_bugs.md` tracks open, not-yet-fixed bugs; `glossary.md` defines
  the project's own terms (row, swing, groove, ...) - check it before
  using one of those words in new code or UI text, and add to it when
  introducing a term
- `tools/` — helper scripts (e.g. `minimal_edo.pl`).
- `third_party/` holds vendored third-party code, one subdirectory per
  library, each with its own upstream `LICENSE`/provenance note -
  `third_party/tinyxml2/tinyxml2.{cpp,h}` (zlib licence) and
  `third_party/pocketfft/pocketfft_hdronly.h` (BSD-3-Clause, the FFT
  backend behind `dsp/RealFFT.h`) so far. Do not reformat or refactor any
  vendored file.
- `src/dsp/RealFFT.h` — the engine's one FFT wrapper (real-signal r2c-forward/
  c2r-inverse, fixed size at construction, no per-call allocation),
  templated on float/double though only `RealFFT<float>` is actually
  instantiated anywhere. Backed by PocketFFT (`third_party/pocketfft/`),
  defining both `POCKETFFT_CACHE_SIZE` (a small nonzero LRU cache of
  PocketFFT's own internal per-length plan objects — its plain `r2c()`/
  `c2r()` free functions have no plan object a caller can hold onto the
  way FFTW's `fftw_plan` did, and without a cache every call fully
  replans from scratch) and `POCKETFFT_NO_MULTITHREADING` (deterministic,
  single-threaded execution) before including the header — see the
  class's own doc comment for why. `dsp/SpectrumAnalyzer.h` (`Player.cpp`'s
  live spectrum chart) wraps
  a `RealFFT<float>` with ring-buffer accumulation and dB conversion;
  `AmbisonicMagLSDecoder`'s precomputation uses `RealFFT<float>` directly.
- `THIRD_PARTY_LICENSES.md` is the canonical, consolidated list of
  vendored/linked third-party licence obligations; `--licenses` prints it
  at runtime (content embedded into a generated header at build time from
  `ThirdPartyLicenses.h.in` — see `CMakeLists.txt` — not read from disk,
  so it works regardless of the binary's working directory). Update the
  `.md` file, not the generated header, when a dependency changes.

## Conventions

- C++17, no exceptions ethos in audio path; state objects (`*State.h`) are
  separated from song model objects so playback state can be reset cheaply.
- The build enables many `-Werror=` flags plus `-Wsign-conversion`; new code
  must compile warning-clean.
- A command only a pad controller dispatches (nothing else registers it) is
  named with a `pad-` prefix (`pad-next-track`), not `launchpad-`: other
  devices can use it too. A pad gesture whose terminal equivalent is
  copy/kill/yank gets no command of its own (`docs/terminal.md`).
- Where a well-known convention exists in the lineage this project draws on,
  prefer it to inventing something new, and where a document records the
  lineage (`docs/drums-and-sequencer.md`, the README), keep it accurate. Add a
  layout or number only when it is known exactly - leave it out rather than
  add a subtly wrong version.
- Comments: keep them short (a one-liner covers most cases). Don't point
  at something outside the code to explain the code - state the reasoning
  directly instead of citing: a `plans/*.md` file (they get deleted once
  done, leaving a dangling reference), `todo.txt` (being phased out, same
  reason), a specific third-party software product (describe the
  convention/behavior generically instead - Emacs is the one exception,
  cited by name throughout this codebase's own keybinding comments), or a
  specific keybinding when the binding itself is declared elsewhere
  (`keymap_.bind()`/`commands_.define()` - a second source of truth that
  silently goes stale if the binding ever changes; name the command
  instead and let the actual binding site be the only place the key
  appears).
- Never link to the chat/session that produced a change (no `Claude-Session:`
  trailer, no claude.ai/code URL) in commit messages, PR descriptions, or
  comments; the URL isn't useful to other readers and can't be fully removed
  from GitHub afterwards.
- Pull requests: when pushing more commits to a branch that has an open PR,
  update the PR description in the same step if the push changes what it
  says - a push updates the diff but never the description.
