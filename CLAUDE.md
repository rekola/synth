# synth — microtonal tracker / synthesizer

Tracker-style music production system with microtonal notes (12/19/31/53-TET).
Terminal UI (notcurses), ALSA audio output, songs stored as XML.
Formerly developed as the `syna/` subdirectory of the private `personal` repo;
full history was preserved when it was extracted into this repository.

Conceived as an amalgam of Emacs (keybinding philosophy — mark/point
selection, kill/yank, M-x), classic step-sequencer trackers (pattern-editor
concepts, tracker workflow), and live sequencers (clip launching/session-
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
`libmysofa-dev` is optional (binaural ambisonic decoding,
`SYNTH_ENABLE_BINAURAL`, auto-detected) — without it, `--ambisonic` still
works via the cardioid stereo decoder fallback.

## Run

```sh
./build/synth songs/demo3.xml                    # open a song
./build/synth                                    # open songs/welcome.xml, or a fresh empty song if that's missing
./build/synth --render out.wav songs/demo3.xml   # headless render to WAV
```

`--render` needs no terminal or audio device: it renders the song offline
(plus the effect/release tail until silence, capped at 10 s) and exits — use
it to verify audio changes and to regression-test songs.

With no file given, `main.cpp` opens `songs/welcome.xml` by default -
resolved cwd-relative first (running from the source tree), then from
wherever `make install` put it (`InstallPaths.h.in`, baked in at configure
time from `CMAKE_INSTALL_PREFIX`), falling back to a fresh empty buffer if
neither is there. The UI starts in Session view with the clip grid
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

Build with `-DSYNTH_ENABLE_SANITIZERS=ON` to enable ASan+UBSan for the whole
project; useful for chasing memory bugs (e.g. `AudioBuffer`'s copy-assignment
leak was confirmed this way).

Needs a real terminal (notcurses full-screen UI) and an ALSA output device.
Options: `--samplerate N`, `--stereo`, `--ambisonic [order]`,
`--legacy-binaural`, `--view session|arrangement`. Every song is always rendered through an
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
`0Lxx`/`0Fxx`/`0Mxx` Volume/Send A/Send B set; the real-time
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
In Session view each pattern editor column marks only its own position,
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
mnemonic's own trailing hex-digit argument (e.g. `ZBxx`'s locator number)
stays permissive too, parsing a non-hex character as digit 0 rather than
rejecting it (`Command::getBreakLocatorNumber()`).

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
logic (note entry, grid-mode dispatch, Session view, drum-machine step
grid) and is Song/Controller-aware but UI-agnostic - it works the same
whether or not a terminal UI exists at all. `LaunchpadIO`'s own destructor
blanks every LED on every connected device (`clearAllLeds()`, an
all-black LED-lighting SysEx covering the grid plus every extra-button
index - `LaunchpadProtocol::allExtraButtonLedIndices()`) before closing
the ALSA connection, so quitting doesn't leave a Launchpad still showing
whatever Session view/step grid/etc. happened to be lit - not a
Programmer Mode exit (this codebase never actually leaves Programmer
Mode once entered), just going dark, a clearer and more predictable
"we're done" signal than whatever a device's own standalone light show
would otherwise resume showing.

- **`GridMode`** (`LaunchpadManager::GridMode`) - one of `NOTES`/
  `SEND_MAIN`/`PAN`/`SEND_A`/`SEND_B`/`DRAW`/`SESSION`/`CUSTOM`, mutually
  exclusive, purely per-device (`toggleGridMode()`), never tied to
  terminal UI focus - one connected Launchpad can sit in Session view
  while another stays on ordinary note entry. Defaults to `SESSION`.
  `CUSTOM` is deliberately generic ("customize whatever's assigned to
  this device") even though only the percussion lane picker is built for
  it today - a pitched `InstrumentTrack` assigned instead currently shows
  nothing there. `SEND_MAIN`/`PAN`/`SEND_A`/`SEND_B`, plus the
  track-picker overlay's three purposes (Stop Clip/Mute/Solo - see its
  own bullet below), together form Session's own **mixer submode radio
  group** (`DeviceState::session_mixer_mode`, off by default) - see the
  Extra-button layout bullet below for what the same seven buttons do
  while that submode is off, and how it's toggled; while it's on, only
  one of the seven is ever active at once (`toggleGridMode()`/
  `toggleTrackPicker()`/`inSessionMixerFamily()` - pressing a different
  one always switches straight to it, even crossing between the fader-
  as-`GridMode` and picker-as-overlay mechanisms; pressing the one
  already active closes back to the plain Session grid). A press that
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
  from `GridMode::SESSION` (a no-op from `NOTES`/`CUSTOM`/`DRAW`) - this
  is what keeps the fader column mapping (the first 8 root tracks) from
  ever disagreeing with the track-picker overlay's own column mapping
  (`session_.track_ids`, Session view's own filtered list); the two lists
  could differ, which used to read as the grid visibly "rotating"
  underneath the picker row whenever both happened to be showing
  together.
- **Extra-button layout** (raw CC, intercepted directly in
  `LaunchpadManager::handleRawButton()`/`UI::handleLaunchpadButtonEvent()`
  before any command-name resolution): 95/96/97 (Session/Note/Custom) plus DRAW are
  a true four-member exclusive group, not independent toggles - each of
  95/96/97's presses selects that mode unconditionally, even pressing the
  one already active, so the only way to leave a mode is selecting a
  *different* one of the four; DRAW is the one member not reached by a
  plain press (see its own bullet below), and the only way out of it is
  selecting one of 95/96/97. 98 is Session Record (its own bullet
  below). 91/92/93/94 are move-row-up/down/prev-track/next-track
  (named commands, via `LaunchpadProtocol::commandForButton()`). 91 doubles
  as a held shift modifier for opening a Session-view clip's own step
  grid directly (see the drum machine bullet below) - its own ordinary
  meaning still fires on a plain tap, just deferred to release rather
  than press (`LaunchpadManager::handleShiftButton()`, `DeviceState::
  row_up_shift_held`) so a press that turns out to combine with a pad
  never has to be undone; 92/93/94 are unaffected, still firing
  immediately on press.
  19/89/79/69/59/49/39/29 (Record Arm/Volume/Pan/Send A/Send B/Stop Clip/
  Mute/Solo, plus Pro MK3 left-column twins 30/20 for Mute/Solo) are the
  real Launchpad X's own right-column "Track control" group, all eight
  sharing one dispatch, keyed on Session's own mixer submode (`GridMode`'s
  own comment): **off** (the default) - each launches a whole scene
  instead (`LaunchpadManager::triggerSceneRow()`, `row = (cc_number - 19)
  / 10` - the classic Launchpad right-column convention, matching every
  visible track's own clip at that row simultaneously, through the same
  audition/assign (Record Arm) split an ordinary Session pad press
  already goes through) - **on** - each is the mixer radio group instead:
  Volume/Pan/SendA/SendB enter that fader `GridMode` (`toggleGridMode()`),
  Stop Clip/Mute/Solo/Record Arm open/retarget the track-picker overlay
  (`toggleTrackPicker()`) with their own purpose. Either way Mute/Solo no
  longer act on the currently-followed track directly the way
  "toggle-mute"/"toggle-solo" (`PatternEditor`'s own `commands_`, still
  reachable via keybinding/M-x) do, and Record Arm no longer reaches
  `Controller::isNoteCaptureArmed()`'s own per-current-track toggle at all
  - that's shift + CC98 instead (see its own bullet below), reachable
  from any `GridMode`. **Shift** (CC91 held) turns all eight right-side
  buttons into labelled alternate functions, in every `GridMode`
  (`handleRawButton()`'s shift branch): Volume (CC89) is Duplicate, Solo
  (CC29, Pro MK3 CC20) is Draw, and the other six do nothing rather than
  launch or switch anything; their LEDs show only those two while shift
  is held (Duplicate cyan, Draw purple, the rest dark). 95 ("Session") doubles as the
  mixer-submode toggle: a repeat press while already at the plain Session
  grid with nothing from the radio group active flips
  `session_mixer_mode`; any press otherwise just lands on (or stays on)
  that plain grid, closing an active fader/picker first if there was one.
  Session's own LED (95) reflects this three ways: dim green when not
  showing anything from the Session family at all, bright green while
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
  take. A long hold is Capture MIDI, a stub that only says so. With shift
  held, CC98 is the arrangement's own Record Arm instead
  (`"toggle-record-arm"`, `Controller::isNoteCaptureArmed()`, which makes
  a Session pad press write the clip into the arrangement). Its LED is
  bright red while anything records (`record_arm_led_on`), dim red
  otherwise.
- **DRAW mode** (shift + Solo) - a plain per-pad coloring toy,
  independent of Song/Track state. Shift + Solo enters it from any
  `GridMode` (same exclusive-group rule as Session/Note/Custom - only
  one of 95/96/97 leaves it), or blanks the canvas if DRAW is already
  showing.
- **Duplicate** (shift + Volume) - held for as long as Volume stays down:
  in Session view a populated pad picks that clip as the source (lit
  white) and a press on an empty slot of the same track column copies it
  there (`duplicateClip()`, `ArrangementOps.h` - an independent copy
  under a fresh id, never overwriting); the source stays picked, so one
  hold can fill several slots. Releasing Volume with a source picked but
  no destination copies to the next empty slot. The terminal's
  `duplicate-clip` (clip grid) does the same.
- **The drum machine** (`PercussionTrack`, up to `kMaxLanes` = 8 lanes,
  `getLaneNotes()`) - the same track type as ordinary percussion note
  entry, not a separate one: with no lanes it's a plain percussion track
  (free note entry through the free-drumming percussion pad layout, a
  fixed family/color arrangement of GM sounds, not an isomorphic pitched
  grid); once it has at least one lane (`isStepSequenced()`) its grid
  becomes a step sequencer instead. Its step data is a real
  `Pattern` like any other track's (a step is a `Note`), not
  track-global, so it's copy/paste-able through `PatternEditor`'s own
  clipboard and renders as its compact one-cell-per-lane view. On a
  Launchpad, the step grid (rows = lanes, columns = steps) only ever
  edits a specific `Clip` actually open for editing on the assigned track
  (`Controller::getFocusedClipTrackId()`) - never the track's own
  background `Pattern`, which has no pagination and spans the whole
  song, far more than this fixed grid (even split across several
  connected devices) could ever show meaningfully. Opening a clip is
  reachable two ways, both funneled through the same shared
  `Controller::toggleDrumClipFocus(track_id, clip_index)`: the
  terminal-driven one ("toggle-record-arm"/Ctrl-X r while the
  `ClipGrid` widget has focus, `Controller.cpp`'s own drum-machine-
  track repurposing), and a Launchpad-only gesture - holding CC91
  ("move-row-up", printed with an up-arrow icon) as a shift modifier and
  pressing a Session-view pad opens that pad's own clip instead of
  triggering/assigning it. The pad half of that combo resolves on its own
  release, not its press: pressing it while shift is held is recorded and
  swallowed outright (never falls through to an ordinary trigger, even
  for a clip that turns out not to be step-sequenced - a no-op then, not
  a trigger), and only the matching release actually opens it
  (`LaunchpadManager::handleShiftButton()`/`handleSessionPadEvent()`,
  `DeviceState::row_up_shift_pending_pad`) - so an abandoned press (shift
  released first, the pad dragged off) never has to be undone; it
  completes on release regardless of whether shift is still held by
  then. `toggleDrumClipFocus()` doesn't gate on lane count at all - a
  lane-less `PercussionTrack` opens exactly the same way a step-sequenced
  one does, its own step grid just showing empty (`DeviceState::
  show_step_grid`'s own comment) rather than either doing nothing or
  routing anywhere else (the lane picker included - a performer reaching
  for "open editing" shouldn't land on a different surface than the one
  they asked for); only some track type that isn't a `PercussionTrack` or
  a pitched `InstrumentTrack` at all still declines outright, the same
  silent no-op as before. Both halves light up full bright white while held - CC91 itself
  (dim white beforehand, in every `GridMode` but the step grid, since it
  has a real, Launchpad-visible meaning everywhere else) and whichever pad
  it's currently combined with, so a performer sees the pair confirmed
  before ever releasing (`LaunchpadManager::refreshLeds()`'s own
  `GridMode::SESSION` branch). Opening forces every connected Launchpad
  into `NOTES` mode showing that clip's own step grid automatically,
  regardless of whatever `GridMode` each was in (`TerminalUI.cpp`'s own
  drum-edit-request listener, `LaunchpadManager::
  forceNotesModeOnAllDevices()`) - and gives each device its own default
  page into the clip (`LaunchpadManager::resetStepGridView()`, "device i
  shows page i" in whatever order they're currently ready), splitting a
  clip longer than 8 steps across however many are connected without
  anyone paging by hand first. The step grid isn't `PercussionTrack`-only
  either - a pitched `InstrumentTrack`'s own clip opens exactly the same
  way via the Launchpad's own shift+pad gesture (`Controller::
  toggleDrumClipFocus()` accepts either track type now), its rows drawn
  from the song's own scale (`Song::getScaleDegrees()`, up to 8 ascending
  offsets from the tonic - deliberately never wrapped back into a single
  octave's own pitch-class range, so the list keeps climbing past the
  octave boundary rather than folding back below the tonic - resolved
  from `Song::getScale()`'s own chosen `Scale` - `MAJOR`/`MINOR`/
  `MICROTONAL_A`/`MICROTONAL_B`, each a fixed 7-name degree list in
  `Note::stringToKey()`'s own note-name syntax so the exact same list is
  correct under every tuning without hardcoding a separate interval set
  per one - transposed to `Song::getKey()`; `Scale::NONE`, the default,
  falls back to the plain chromatic scale) rather than a manually-picked
  lane list - there's no per-track lane concept for a pitched track at
  all, so nothing gates this the way lane count doesn't gate a
  `PercussionTrack`'s own clip either. Every named scale here has exactly
  7 degrees, one short of the step grid's own 8 rows - rather than
  leaving the 8th row unused, `getScaleDegrees()` fills it by cycling
  back to the first degree name (the tonic) one octave higher, so the
  step grid's last row is always the tonic repeated an octave up, the
  conventional way a scale's own degrees are shown (e.g. C D E F G A B
  C). Each degree lands in this
  device's own current octave register (`LaunchpadManager::
  resolveStepGridLaneNotes()`, the same register formula `resolveNote()`
  already uses for the ordinary isomorphic grid), so a step placed here
  and a note played on that grid at the same octave are the identical
  absolute value. "toggle-record-arm" itself is deliberately *not*
  extended to a pitched track this way, even though `toggleDrumClipFocus()`
  itself now accepts one - Record Arm there already means something real
  (multi-track Session View recording, below), which opening the step
  grid would silently preempt every time rather than only when actually
  wanted; only the Launchpad's own shift+pad gesture reaches a pitched
  track's step grid, `PercussionTrack` remaining reachable both ways.
  Once the step grid is showing, every button
  with no meaning left there goes fully dark rather than keeping its usual
  out-of-Session color - Note (CC96, forced into already - pressing it
  again is a true no-op), and Session Record's own idle color (CC98 - a
  tap there would only say there is nothing to overdub; its recording/red
  indicator stays lit regardless, since that's real track-global
  recording state a performer still needs to see, not a per-mode
  affordance). All
  four of 91-94 are repurposed instead of going dark, together covering
  everything a scale/chromatic run has that the fixed 8x8 grid alone
  can't show at once, both scrolling `kStepGridScrollStep` (4) rows/steps
  per press rather than jumping a whole 8-wide window at once, so
  consecutive windows overlap and a performer can actually follow where a
  press landed relative to before: prev-track/next-track (CC93/94) scroll
  every connected device's own `DeviceState::drum_edit_step_offset`
  together, in lockstep (never just the one device the press landed on,
  which would otherwise drift devices onto overlapping or duplicate
  windows), through the clip's own steps (columns); move-row-up/
  move-row-down (CC91/92) scroll every connected device's own
  `DeviceState::drum_edit_row_offset` instead, on a *pitched*
  `InstrumentTrack`'s own step grid only (a `PercussionTrack`'s lanes are
  a small, fixed, manually-curated list - never more than 8 - with
  nothing to scroll to at all) - `Song::getScaleDegreesWindow()`'s own
  `start_index`, reaching any row a scale/chromatic run's own unboundedly
  long ascending sequence has, not just whichever 8 happened to be shown
  when the clip was opened. Deliberately *not* an octave shift the way
  this same button pair ordinarily is elsewhere (`octaveUp()`/
  `octaveDown()`, still used for their own ordinary out-of-step-grid
  meaning) - jumping a full octave (all 8 rows at once) left no overlap to
  visually track, and tied the row window to a register concept that
  assumes a scale repeats every single octave, which a future scale (more
  than 7 degrees, or spanning more than one octave) need not
  (`DeviceState::drum_edit_row_offset`'s own comment) - so this device's
  own octave/note-entry register is left untouched by a step-grid row
  scroll entirely now. Both loop over `readySessionIds()` the same way -
  a press on any one device moves them all, not just the one physically
  pressed. Unlike CC93/94's own step-lockstep, which preserves each
  device's own relative offset from `resetStepGridView()`'s initial
  per-device split (steps), CC91/92's own row-lockstep has no such
  per-device offset to preserve - every device always starts a clip at
  the identical row offset (`resetStepGridView()`, below), so a scroll
  just applies the same delta to all of them uniformly. All four light the
  same white rather than 93/94's own former blue, reading as one family of
  step-grid navigation (row window vs. step window) once a clip's open,
  except CC91/92 now go dark for a `PercussionTrack`'s own step grid
  specifically (nothing to scroll to there), matching every other
  "nothing a performer could see would happen" button here. CC93/94 still
  go dark once scrolling would be a no-op: whenever the clip's own length
  is already covered end-to-end by however many devices are connected
  (`resetStepGridView()`'s own split already shows the whole clip at
  once), a press would just re-clamp back to where it already is -
  CC91/92 have no equivalent ceiling to go dark for on a pitched track,
  since a scale/chromatic run's own row window can always keep scrolling
  either direction without ever becoming a true no-op.
  Opening a clip resets four things at once
  (`resetStepGridView()`): every connected device's own step offset back
  to its own device-order default (its existing per-device split, below);
  every connected device's own row offset back to 0 (the tonic) - same
  reasoning as the step offset, for a pitched track's own row window
  instead of a clip's own columns; every connected device's own octave
  register back to 0 - without this, opening the same clip twice could
  start at a different, unpredictable octave depending on whatever
  unrelated note entry had happened to leave a given device's own octave
  at in between; and the step grid's own preview clock
  (`LaunchpadManager`'s `preview_clock_`, separate from `SessionPlayer`'s)
  - without this last one, a clip's
  audition playhead resumed from wherever that clock's stale phase
  already happened to be rather than row 0, so a freshly opened clip could
  visibly start partway through instead of from its own beginning. Every
  open now starts from the same known step window, row window, octave,
  and playhead position. Closing it again is reachable two ways too: the same shift+pad combo on the same
  pad (only from the plain Session grid - the step grid a successful open
  switches every device to has no (track, clip index) addressing of its
  own to shift-combine with, so this needs a CC95 press back to Session
  first), or a lone CC95 ("Session") press by itself, from any grid mode
  - Session's own button doubles as "leave the sequencer entirely"
  (`Controller::closeDrumClipFocus()`, `LaunchpadManager::
  handleRawButton()`'s own CC95 case) - the more direct route, not
  requiring shift at all. Either closing route returns every connected
  device to Session view. Merely navigating the shared cursor onto a
  `PercussionTrack` or pitched `InstrumentTrack` (or recording into it -
  see the Multi-track Record Arm bullet below) does *not* by itself show
  the step grid - both fall through to ordinary free-drumming/chromatic
  pad entry instead, same as a lane-less `PercussionTrack` always does.
  Setting a fresh step
  always auditions immediately (`handleStepGridPadEvent()`'s own
  `suppress` check); pressing an already-lit step pad (removing it) never
  auditions the sound that was just removed. CC97 ("Custom")
  launches the lane picker directly (`GridMode::
  CUSTOM`, gated on the assigned track being a `PercussionTrack` at
  all, any lane count - this is how a lane-less track gains its first
  lane): the free-drumming percussion layout doubles as a lane add/remove
  surface, reachable straight from Session view without detouring through
  Note mode first. A handful of named lane-subset presets beyond the
  default ("rock") kit - Latin, electronic - are reconfigurable on an
  existing `PercussionTrack` via M-x/the Track menu
  (`apply-preset-rock`/`-latin`/`-electronic`,
  `PercussionTrack::applyPreset()` - a full replace of the lane list, not
  additive).
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
  (`!clip.isEmpty()`), for exactly this reason - Session view's own
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
- **Session view** (`GridMode::SESSION`, reached/left only via CC95/96/97/98,
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
  (`absolute_pos_ % rows_per_bar == 0`) - even the first launch into
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
  rule as the edit position), and keeps the Session View takes, resolving
  them on bar rows it sees in the snapshots (`SessionPlayer::tick()`,
  once per UI frame). A finished take loops back on the bar its stop
  resolved on. Explicitly never the
  triggered clip's own loop length (that only decides where *it* loops,
  not when a pending change is allowed to interrupt it) and never
  immediate - an empty pad queues a stop (unless its stop button was
  removed - `Clip::hasStopButton()`: "toggle-stop-button" in the clip
  grid, or "delete-clip" on an empty slot, which then shows no ⏹: launching that slot, alone or in its
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
  A pad's own identity-hue static color (`DeviceState::session_colors`)
  gets a transport-state overlay (`session_highlight`, `LaunchpadManager::
  SessionPadHighlight`) matching the convention -
  playing pulses and queued flashes a fixed green
  (`LAUNCHPAD_SESSION_GREEN_PALETTE_BRIGHT`/`_DIM`) regardless of that
  pad's own hue, via the LED-lighting SysEx's own hardware-driven
  flash/pulse lighting types (`LaunchpadProtocol::LightingType::FLASH`/
  `PULSE`) rather than a software brightness blend - the device animates
  it on its own internal clock once sent, so `refreshLeds()`'s existing
  send-only-on-change dedup means this never needs re-sending itself
  either. Those two lighting types only understand a fixed 128-entry
  palette, not arbitrary RGB, which is why they're a fixed green rather
  than each pad's own hue. An armed track (`Controller::isTrackArmed()`), or one
  recording or about to, switches its whole column from this green overlay to a red one instead
  (still `SessionPadHighlight`, four further states -
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
  Session-View-focused SampleTrack branch from the terminal), and
  pressing that same pad again cancels a still-idle arm
  (`LaunchpadManager::stopSampleTrackRecording()`) - never
  `Controller::trimSessionRecordingClip()`, the note-Pattern-specific
  finalize note-based Session recording uses, which would misread a
  SampleTrack take's own empty Pattern as "nothing was ever recorded" and
  reset its real audio length back to one bar.
- **Track-picker overlay** (`LaunchpadManager::toggleTrackPicker()`/
  `handleTrackPickerPadEvent()`/`isTrackPickerRow()`, `DeviceState::
  track_picker_active`/`track_picker_purpose`) - four purposes (Stop
  Clip/Mute/Solo/Record Arm, CC49/39/29/19 - see the Extra-button layout
  bullet above), all four members of Session's own mixer submode radio
  group (`GridMode`'s own comment covers the other four members, and the
  submode toggle itself); this bullet is about what those four purposes
  look like on the grid once the overlay is reachable at all.
  Session-view-only: pressing Stop Clip (CC49), Mute (CC39/Pro MK3 30),
  Solo (CC29/Pro MK3 20) or Record Arm (CC19) is a no-op from any other
  `grid_mode`, and every `grid_mode` reassignment site that moves off
  `SESSION` closes the overlay if it was open, so it can never be showing
  over anything else. This is what lets it light just the
  bottom grid row with one pad per selectable track and otherwise leave
  Session view's own rendering completely untouched - no dimming, and
  every row but the picker row still reaches Session view's own pad
  handling exactly as if the overlay weren't open (`isTrackPickerRow()`
  is what `UI::handleLaunchpadPadEvent()` uses to route only that one row
  here). Earlier revisions dimmed the rest of the grid and swallowed
  presses there, and could be opened from any `GridMode`; both were
  dropped once opening it over Send/Pan turned out to show the picker
  row's own track-column order (`session_.track_ids`, Session view's
  filtered list) alongside Send/Pan's *different* column order (the first
  8 root tracks) at once, which read as the grid "rotating" underneath
  the picker row - restricting both to Session view removes the only
  situation where the two mappings could ever disagree.
  The picker row's own pad colors don't use per-track identity color the
  way Session view's columns do: every pad in the row shares one hue
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
  button's own purpose (a Session-view-style quantized stop for Stop
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
  ordinal position in that track's own clip list, the same index Session
  view's own rows address - resolved the same way real playback does
  (`ArrangementOps.h`'s `resolveInstanceAt()`). A bar with no active
  instance falls back to a page/empty-page glyph showing whether the
  background has anything there. No per-cell copy/paste - placing/moving
  clip content is `copy-to-clip`'s own job, from `PatternEditor`. Track
  selection is the one shared cursor Session view also follows; Enter
  moves the transport to the cursor's bar.
- **Views** (`UI::View`, `ARRANGEMENT`/`SESSION`) - how the active song is
  laid out, UI state independent of which buffer (song) is active; a
  buffer is just a song. Arrangement view: the scope row (with
  `ArrangementGrid`) plus `PatternEditor`. Session view: `ClipGrid`
  (`src/ui/tui/ClipGrid.h` - per-track clip slots, Sends, Direction), with
  `OutlineView` as a narrow panel on its left (shown by default,
  "toggle-outline"; the tree, a button bar under it, details in a `?`
  popup that Escape closes at once - a widget can take a bare Escape via
  `UIElement::wantsBareEscape()` without it losing its Alt-prefix role),
  above `PatternEditor` (`TerminalUI::layout()`) - no scope row
  (Arrangement view's is optional too, "toggle-scopes"). The clip grid
  and the pattern editor share the current track; the clip grid marks the clip
  the pattern editor is editing for the cursor track
  with a pencil (✎) beside its loop icon (`ClipGrid::
  setTrackClipSource()`) - an icon, not a colour, so a Launchpad can show
  the same thing. The clip grid's cursor row is its own
  (`TerminalUI::syncSessionView()` shares only the track): it never
  moves a track's position, nor follows one - a track's position moves
  only in the pattern editor, or when a clip launched on it starts
  playing. Each track header ends in "◆IMS" (the ◆ double width):
  an orange ◆ while Session view has taken the track over from the
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
  source for both, `SessionPadHighlight`): a colored glyph in its icon's
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
  a block's RMS in the power domain so a low note doesn't ripple, and a
  `PeakHold` marker floating above the bar in its right dot column). The
  pattern editor's one-cell track meters and the scope row's
  `ChannelMeter` (two channels per cell, `getChannelLoudness()`, a
  per-channel RMS) share all of it; the master's reads
  the master track's output - the dry mix plus the returns, after the
  master's levels (`SongState::getMasterMeterValue()`) - which each
  playback snapshot reports as the master's `TrackInfo`.
  "toggle-view" (Tab) flips between them, the live-sequencer convention;
  "arrangement-view"/"session-view"/"outline-view" select one directly
  (View menu). `PatternEditor` reads and writes through a `PatternSource`
  (`src/ui/PatternSource.h`): `ArrangementPatternSource` in Arrangement
  view (the arrangement's one timeline and placed clips, a single block of
  absolute rows, the transport as its cursor row),
  `ScenePatternSource` in Session view (clips directly, no locators).
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
- **Defaults**: a fresh session opens in Session view on the clip grid
  (`UI::setInitialView()`, the `--view` option) rather than straight into
  note entry, and `GridMode` defaults to `SESSION` on every
  connected device - see the Run section above for the matching
  `songs/welcome.xml`/31-EDO startup defaults.
- e2e coverage: `tools/e2e/verify_launchpad_session.py` (see that
  directory's own `README.md`) covers Session view's basic trigger/assign
  path, arming Record Arm for it via shift + CC98 (the legacy global
  `toggle-record-arm` - CC19 no longer reaches it while looking at
  Session view); `verify_launchpad_notecustom.py`/
  `verify_launchpad_draw_clear.py` cover CC96/CC97's own mode-switch
  and the shift + Solo gesture (DRAW mode entry/canvas-clear);
  `verify_launchpad_record_arm_picker.py` covers CC19's own Session-view
  meaning, the track-picker overlay's fourth purpose - presses CC95 a
  second time first to enter mixer submode (required before CC19 does
  anything at all, same as the other three), same reasoning as
  `verify_launchpad_mute_picker.py` below; `verify_launchpad_stopclip.py`
  covers the track-picker overlay's CC49 purpose above (open/pick/close,
  staying open across a pick, and a playing clip's picker pad showing
  the picker's static red rather than its playing pulse);
  `verify_launchpad_mute_picker.py` covers
  the CC39 purpose (bright/dim polarity, the overlay leaving Session
  view's own rendering untouched outside the picker row), and also exercises Session's own
  mixer-submode toggle (a second CC95 press) and its green/orange LED,
  since CC39 means nothing at all until that submode is on; Solo's own
  CC29 purpose reuses the identical mechanism but has no dedicated e2e
  script of its own yet. `verify_launchpad_scene_row.py` covers the
  scene-launch action the same eight buttons perform while mixer submode
  is off (`LaunchpadManager::triggerSceneRow()`, row = (cc_number - 19) /
  10) - every track's own clip at that row launches together off a single
  press, not just the first track in `session_.track_ids` - every track
  queues for the same next bar. `verify_launchpad_mixer_hold.py`
  covers the same eight buttons' own momentary hold-to-preview gesture
  (`armMixerHoldPreview()`/`handleMixerFunctionRelease()`) - a quick tap
  stays (sticky), a real hold reverts to whatever was showing before it
  once released. `verify_launchpad_session_automation.py` covers a fader
  move during a Session View take landing in the take's own clip
  (`recordFaderAutomationIfArmed()`) rather than the arrangement, which a
  taken-over track ignores. `verify_launchpad_sampletrack_record_arm.py` covers the
  SampleTrack twin of `verify_launchpad_record_arm_holes.py` - a
  Session-grid press on a SampleTrack armed via the track-picker overlay
  actually arming real audio capture, and a second press cancelling it -
  verified through the terminal `ClipGrid` widget's own text.
  `verify_launchpad_shift_stepgrid.py` covers
  CC91-held-as-shift's own gesture (see the drum machine bullet above) -
  opening a step-sequenced `PercussionTrack` clip's own step grid from
  Session view, and a lone CC95 press closing it again - verified the
  same terminal-text way (the "*" focus marker).
  `verify_launchpad_shift_highlight.py` covers the same
  gesture's own LED feedback while held (both CC91 and the target pad
  lighting bright white before release). `verify_launchpad_paging_lockstep.py`
  covers the step grid's own prev-track/next-track page-shift gesture
  moving every connected device together rather than just whichever one
  was pressed - two simulated devices open a 4-page clip, confirm
  `resetStepGridView()`'s own device-order split put them on two
  different pages, then one pages forward once and both are confirmed to
  have scrolled together. `verify_launchpad_shift_no_lanes.py` covers the same
  shift+pad gesture opening a *lane-less* `PercussionTrack`'s own clip -
  showing the step grid completely empty rather than declining or routing
  to the lane picker instead. `verify_launchpad_shift_stepgrid_pitched.py`
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
    shown in Arrangement view's locator column; `ZBxx` jumps to one)
    and their value types (`Note`, `Command`, `SendLevels`, …).
  - `src/state/` — the parallel, cheaply-resettable playback-state
    objects (`*State.h`) mirroring the model objects above.
  - `src/playback/` — `Player` (sequencer), the event vocabulary it
    consumes/produces, and `SessionPlayer` (Session view clip launching).
  - `src/instruments/` — synthesis and instrument resolution:
    `OscillatorVoice`/`GenericInstrument`/`SoundFont`, `Tuner`/`Tuning`
    (microtonal pitch math), `LFO`, `Arpeggiator`.
  - `src/ambisonic/` — spatial encode/decode math and the `Mixer`
    hierarchy (see the `AmbisonicEncoding.h` bullet below).
  - `src/audio/` — `AlsaAudio` (device output), `AudioBuffer`,
    `OfflineRenderer`.
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
  `known_bugs.md` tracks open, not-yet-fixed bugs
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
- Pull requests: when pushing more commits to a branch that has an open PR,
  update the PR description in the same step if the push changes what it
  says - a push updates the diff but never the description.
