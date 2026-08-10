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

Dependencies (Ubuntu): `libnotcurses-dev libfmt-dev libsndfile1-dev
libasound2-dev` plus CMake and a C++17 compiler. FFT support (the live
spectrum analyzer, MagLS binaural precomputation) is via vendored PocketFFT
(`third_party/pocketfft/`) — no separate FFT library package needed.
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
project; useful for chasing memory bugs (e.g. `AudioBuffer`'s copy-assignment
leak was confirmed this way).

Needs a real terminal (notcurses full-screen UI) and an ALSA output device.
Options: `--samplerate N`, `--stereo`, `--ambisonic [order]`,
`--legacy-binaural`. Every song is always rendered through an
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
**Space** toggles playback, Ctrl-Q quits, Ctrl-N creates a new song,
**Ctrl-K** opens the M-x command minibuffer (reliable on any terminal; see
below for why it exists alongside Esc-x/Alt-x).
`docs/commands.md` lists the pattern effect commands (slides, vibrato, …),
split into **Implemented** (only `ZBxx`, pattern break, so far - see
`SongState.h`'s command-handling loop) and **Planned** (accepted/stored
but currently no-ops at playback time).

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
mnemonic's own trailing hex-digit argument (e.g. `ZBxx`'s destination row)
stays permissive too, parsing a non-hex character as digit 0 rather than
rejecting it (`Command::getBreakDestinationRow()`).

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
  logic), `Song`/`Scene`/`Pattern`/`Track` (song model — `Song` holds a flat,
  sequentially-played `vector<Scene>`; each `Scene` is one point in the song,
  holding one `Pattern` — a track's own note/command content, no `track_id`
  in it anywhere — per track that has anything there, plus that scene's own
  row-keyed annotations), `Player` (sequencer), `AlsaAudio` (output),
  `TerminalUI`/`PatternEditor`/`HierarchyView` (notcurses UI), `Tuner`/
  `Tuning` (microtonal pitch math), `OscilatorVoice`/`GenericInstrument`/
  `SoundFont` (synthesis).
- `effects/` — per-track audio effects (chorus, compressor, distortion, …)
  — each constructed fresh per track/note and torn down with it, unlike
  the shared send bus (`bus/`, below). There is no per-track reverb any
  more (`effects/Reverb.{h,cpp}`'s GPL-licensed `MVerb`-based
  `<reverb preset="...">` was removed) — the shared bus's spatial FDN
  reverb (`bus/FDNReverb.h`) is the only reverb left, and `<reverb>` now
  only ever means that one, as a `<bus>` child.
- `AmbisonicEncoding.h` — ambisonic encode/decode math (SN3D gains up to
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
- That DSP lives in `bus/` (the shared send bus's own subsystem, depending
  on `dsp/` — reusable, dependency-free DSP building blocks, never the
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
  `known_bugs.md` tracks open, not-yet-fixed bugs (as opposed to `todo.txt`'s
  long-lived feature/idea backlog).
- `tools/` — helper scripts (e.g. `minimal_edo.pl`).
- `todo.txt` — long-lived idea backlog, not a list of in-progress work.
- `third_party/` holds vendored third-party code, one subdirectory per
  library, each with its own upstream `LICENSE`/provenance note -
  `third_party/tinyxml2/tinyxml2.{cpp,h}` (zlib licence) and
  `third_party/pocketfft/pocketfft_hdronly.h` (BSD-3-Clause, the FFT
  backend behind `dsp/RealFFT.h` - see `plans/magical-wondering-engelbart.md`)
  so far. Do not reformat or refactor any vendored file.
- `dsp/RealFFT.h` — the engine's one FFT wrapper (real-signal r2c-forward/
  c2r-inverse, fixed size at construction, no per-call allocation),
  templated on float/double though only `RealFFT<float>` is actually
  instantiated anywhere. Backed by PocketFFT (`third_party/pocketfft/`),
  defining both `POCKETFFT_CACHE_SIZE` (a small nonzero LRU cache of
  PocketFFT's own internal per-length plan objects — its plain `r2c()`/
  `c2r()` free functions have no plan object a caller can hold onto the
  way FFTW's `fftw_plan` did, and without a cache every call fully
  replans from scratch) and `POCKETFFT_NO_MULTITHREADING` (deterministic,
  single-threaded execution) before including the header — see the
  class's own doc comment and `plans/magical-wondering-engelbart.md` for
  why. `dsp/SpectrumAnalyzer.h` (`Player.cpp`'s live spectrum chart) wraps
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
- "Oscilator" (single l) is the established spelling in this codebase; keep it
  for consistency.
