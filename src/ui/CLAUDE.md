# src/ui — the interactive UI

Toolkit-agnostic plumbing plus the one concrete backend, `InteractiveUI`.
Widgets are built on cellui (`lib/cellui/CLAUDE.md`); what stays here is what
knows the song or the controller: `PatternEditor`, `ClipGrid`,
`ArrangementGrid`, `OutlineView`, `InfoLine`, `StatusLine`, `PatternScroll`,
`TerminalMenu` on `MenuBar`, the pixel charts. Many `ui/*.h` are forwarding
headers for what moved to cellui. The UI sources are the `synth_ui` library.

## Structure

- `UI` owns the app-level lifecycle every backend needs (audio and
  visualization threads, running until quit, status) and nothing
  widget-shaped; a subclass owns its widgets, layout and Launchpad wiring
  (`UI::wireLaunchpad()`). `InteractiveUI` is the sole screen backend, on any
  cellui `Backend` (terminal, GTK or Win32 window); `HeadlessUI` is the one
  with no screen. `UI::View` (`ARRANGEMENT`/`LIVE`) is UI state independent
  of which buffer is active; a buffer is just a song.
- Threads: a status message from UI code is `Controller::log()`. The audio
  and visualization threads send plain event structs with
  `Controller::postToUI()` into the `cellui::ThreadInbox` that `UI::start()`
  makes over the loop, handled by `on<E>()` handlers; MIDI and Launchpad
  input is read on the UI thread and delivered to the same handlers
  (`UI::deliver()`). The other way are plain structs on `MessageQueue<T>`:
  `PlaybackControlEvent` to audio, `AudioBlockEvent` to visualization. There
  is no `Event` base class; `InputEvent` is `cellui::Input`.
- Info dialogs (outline details, About) are Markdown parsed by `ui/Markdown.h`
  (headings, paragraphs, `*italic*`, `**bold**`, `\` escapes, nothing else) so
  any backend can show them; `UI::showInfoDialog(title, markdown)` is the
  hook, `InfoDialog` the cell rendering. Modal dialogs are made on the stack
  and shown with `showModal()` (`cellui::ModalDialog`): a nested event loop
  returns the result; the dialog owns its scrim and takes every event through
  capture handlers on the root; `InteractiveUI` only knows one has the input.
  `showChoiceDialog()`/`showInfoDialog()` therefore return once closed; tests
  run their checks in `UIFixture::inModal()`.

## Link profile

`PerformanceProfile.h`: `profileFor(LinkClass)` is what the app draws by link class (`plans/link-degradation.md`
3.7). `InteractiveUI::applyProfile()` runs when the backend's link handler fires (and once at start, for a pinned
class). It sets: a cap on autonomous redraws (`frameDue()`: a frame an input or an edit asked for is never held
back, one only the audio blocks want waits for the class's gap); reduced motion from 15 Hz down, added to the
user's own setting; whether the scopes and the channel meter run (their speed is the frame cap's; switched off, they keep
their picture for `kScopeHold` (2 s) and are then cleared); `Backend::setPointerDetail()` (hover and pointer shapes, which cost a
terminal's link bytes); and `PatternEditor::setPageScroll()`. Not yet by class: Live View's per-column lines, the
arrangement grid, the backdrop.

## Where commands live

Commands (`CommandRegistry` names, run by a key or M-x) live in
`UI::initializeCommands()`, not `InteractiveUI`: a GUI backend shares the
names and behaviour, only the *binding* is backend-specific. A command stays
in `InteractiveUI` only while it needs a `StatusLine` prompt or a concrete
widget pointer with no abstract stand-in. The permanent exception is
pattern-editor selection/copy-paste (`set-mark`/`kill-region`/
`kill-ring-save`/`yank`/`exchange-point-and-mark`): a GUI's copy-paste
follows GUI conventions, not Emacs mark-and-kill, so these stay in the
widgets' own registries (`PatternEditor`, `ClipGrid`) for good.

Dispatch: `KeyStroke.h`/`Keymap.h`/`CommandRegistry.h` map key stroke →
command name → callable; widgets fill `keymap_`/`commands_` in their
constructor and call `dispatchCommand()` early in their key handler, anything
unbound falling through to manual handling. `Controller::sendCommand()` (what
the palette calls) falls back to `UI::executeCommand()` — the active element's
registry, then UI's — for names it doesn't know, so M-x reaches both
Controller-level and per-widget commands. Not migrated: `StatusLine`,
`OutlineView`. Every command needs a line in `ui/CommandDescriptions.cpp`
(a test fails otherwise).

## Keymaps and the command palette

- A display declares its `cellui::KeymapProfile` (`TERMINAL` for the
  terminal, `DESKTOP` for a window), reaching widgets through
  `Plane::keymapProfile()`; `--keymap gui|terminal` overrides it.
  Escape-as-Meta stays the display's: it is about how the display reports
  Alt, not the keymap. `Keymap::bind(stroke, name)` is for both profiles;
  `bind(profile, stroke, name)` for one, and wins over a shared binding.
  Profile-specific so far: the command prompt (M-x / Ctrl+Shift+P), undo/redo,
  cut/copy/paste, save/open/save-as/quit (Ctrl+S/O/Shift+S/Q on the desktop;
  Ctrl+Q exists only there, Emacs has no Ctrl-Q convention), and the `C-x`
  prefix (Emacs only, since Ctrl+X is cut on the desktop). There is no
  new-song binding (`select-named-buffer` starts one by name). Ctrl-K is
  `PatternEditor`'s kill-row (Emacs's C-k repurposed). `docs/user-interface.md`
  has the tables.
- The command palette (`cellui::CommandPalette`, opened by
  `InteractiveUI::showCommandPalette()`) is a modal panel across the bottom
  rows with no scrim: the query on the last row ("M-x " or "> "), the
  selected command's description above it (quiet, italic, on the display's
  own background so it isn't taken for a command), and above that the
  `cellui::fuzzyMatch()`-ranked commands with their bindings, growing upward.
  The app gives it `PaletteEntry`s from `commandCompletions("")`,
  `commandDescription()` and `Widget::bindingFor()` (the display profile's
  binding, spelled by `KeyStroke::describe()`). Choosing runs
  `Controller::sendCommand()` after the palette has closed. Tab completes
  Emacs-style to the longest shared prefix.
- `StatusLine`'s M-x detection is a single check for a Meta-x `Input`:
  `cellui::KeyReader` folds a bare `ESC` followed, arbitrarily later, by `x`
  into one Meta event (Emacs's indefinite ESC-as-Meta, "ESC-" indicator
  included — no timing window) and holds a bare Escape until the next key,
  asking the app's `takeEscape` hook whether something closes on it at once.
  A widget can take a bare Escape via `UIElement::wantsBareEscape()`.
  `Plane::showReader()` takes the prompt string itself and draws it before
  the reader's child plane; a `setMessage()` after `showReader()` is deferred
  and never shows.

## Pattern editor: selection, regions, cursor

- Mark-and-point on both keymaps (`C-SPC` mark, `C-w` kill, `M-w` copy,
  `C-y` yank, `C-g` cancel; Ctrl+X/C/V on the desktop, the same commands).
  The region is a rectangular row×track block; within one track it is scoped
  to note columns (voices): a fresh mark starts on the cursor's note column
  and moving sideways widens it; widen across all to select the whole track.
  `num_subtracks_` is derived from the widest visible row, not a fixed cap.
- **There is always a region**, degenerating to the single note under the
  cursor (`getEffectiveSelectionBounds()`); kill/copy/yank/transpose never
  say "No selection". It is therefore always shown: the region highlight *is*
  the cursor (no separate current-column highlight). The region's
  `SelectionScope` decides what it carries: within note columns only,
  `NOTE_COLUMN`; on the effect column only, `COMMAND` (just the track's
  `Command`s, `PatternBlockOps`' `...PatternBlockCommand()`); spanning both,
  or several tracks, `TRACK` (whole cells) — an effect command applies to the
  whole row. `transpose-region-*` never touch `Command`. The effect
  column's character validation stays permissive (any letter: mnemonics use
  non-hex letters); only velocity/delay nibble entry is strict `0-9a-f`.
- `getEffectiveSelectionBounds()` must run *after* `render()` commits
  `current_cursor` from `new_cursor`, or the region lags a frame. Killing a
  track's last note in its last voice slot shrinks `num_subtracks_` and can
  reinterpret a stale column index as another column; clamp through
  `getNoteNumber()`, not a raw bounds check.
- `transpose-region-up/-down` (Ctrl+Shift+Up/Down) act on the effective
  region and never clear the mark. To transpose a whole track or pattern,
  select it; there is no unmarked whole-pattern fallback.
- `mark-whole-buffer` selects the whole song by view (`SelectionScope::
  SCENES` in Live View: every clip; `ARRANGEMENT`: every background and
  placed clip): no mark, ended by any cursor move or `keyboard-quit`, since
  the single-block region can't express it. Transpose, humanize and the
  tuning commands act on it through the `...Song()` functions in
  `PatternBlockOps.h`; kill, copy and yank refuse.
- In Arrangement view a region acts on the notes it shows: per track,
  whatever supplies that track at the anchor row (a placed clip, the focused
  clip, or the background), stopping where other content takes over
  (`PatternSource::sourceRows()`). A yank writes into whatever the cursor is
  on and stops where that ends.
- Tuning corrections: a note's local fx can be `+hh`/`-hh` cents
  (`Note::setTuningCorrection()`, played as `NoteOverride::detune_cents`); the
  empty fx character is `.` (shown `·`) since `-` is a sign.
  `apply-just-intonation-region` tunes each pitched note to the gamut note
  of its key (`model/JustIntonation.h`): the scale names its gamut
  (`scaleGamut()`: duodene for major/minor, none for the maqamat and
  `Scale::NONE`), each key gets the gamut note whose nearest step it is, the
  scale's own note if several, else the closer, else it stays tempered. No
  chord context, so some triads are a comma off (`docs/user-interface.md`).
  A scale with no gamut writes nothing and the command says so. Live notes get
  it too (`SongScalars::scale_corrections`, `tuneNewNote()`). `+00` marks a note needing none;
  `clear-tuning-correction-region` removes them. A "tuned" note is retuned by
  transposition, by `set-song-scale` (`retuneSong()`, which removes them for a scale with no gamut) and by `set-song-tuning`
  (`convertSongTuning()` moves every note to the nearest step, a tuned note
  keeping the rest as its correction; a yank from a song in another tuning,
  notes or a clip, converts the same way, from C: `convertBlockTuning()`/
  `convertClipTuning()`, refusing only drum against pitched); `transposeSong()` moves the key with
  the notes when none is left outside its scope. "Tuning" means the song's
  EDO; the offset is a tuning correction (`docs/glossary.md`).

## Cursor, focus and colours

- A cursor shows only while the window has focus (`StyleProvider::
  window_focused`, from `BackendEvent::FOCUS`); a playhead always shows. Every
  widget's cursor uses the same colours, leaning cyan so it never reads as
  the grey bar/beat highlight: dark on bright grey focused
  (`highlight_fg/bg_color`), plain text on faint grey unfocused — an
  unfocused widget still shows where its edits land, except `ClipGrid` and
  `OutlineView`, which show none. A coloured cell brightens toward
  `cursor_tint_color` instead. A marked row (the cursor row, a scene row, the
  arrangement's playing row) takes `cursorRowTint()`; in Live View each
  column marks only its own position (`PatternSource::positionRow()`), so no
  column shows two marked rows. Velocity/delay's bright colours switch to the
  region foreground inside it. The theme is `InteractiveUI`'s `styles_`,
  handed to widgets in `render()` and carried by every `Plane`.

## Arrangement view, Live View, clip grid

- `toggle-view` (Tab) flips views; `arrangement-view`/`live-view`/
  `outline-view` select one. `PatternEditor` reads and writes through a
  `PatternSource`: `ArrangementPatternSource` (the one timeline and placed
  clips, the transport as cursor row) or `ScenePatternSource` (clips directly,
  no locators).
- **Arrangement view**: `ArrangementGrid` in the top band — one column per
  bar, running one past the content's end, rows = tracks named at the left
  under a bar ruler and the locators (`›` + name); height follows the track
  count up to a third of the screen, with the spectrum, DirAC heatmap and
  channel meter stacked to its right under title bars. The bars are a plane
  of their own, scrolled by parts of a bar on a smooth display, the playhead
  a vertical `Band`. A placed instance is a block in the track's identity
  colour showing its clip's hex ordinal on its leading bar, resolved as real
  playback does (`ArrangementOps::resolveInstanceAt()`); a bar with no
  instance shows `*` (sounding) or `.` (only note-offs/commands) for the
  background. No per-cell copy/paste: placing clip content is
  `copy-to-clip`'s job from `PatternEditor`. Enter moves the transport to the
  cursor's bar. The scope row is optional (`toggle-scopes`).
- **Live View**: `ClipGrid` (per-track clip slots, Sends, Direction) with
  `OutlineView` as a narrow panel on its left (`toggle-outline`; a button
  bar overlaid on its bottom rows; details in a `?` popup) above
  `PatternEditor`; no scope row. The clip grid and pattern editor share the
  current track; the clip grid marks the clip the editor is on with a pencil
  (✎) beside its loop icon — an icon, not a colour, so a Launchpad can show
  the same. The clip grid's cursor row is its own and never moves a track's
  position; a position moves only in the pattern editor or when a launched
  clip starts.
- In Live View each track has its own position (a clip and a row in it): a
  playing track's is its playhead (immovable; Up/Down say so), a stopped
  track's is wherever it was left, per buffer. The cursor is the cursor
  track's position and every other column is shown relative to it
  (`ScenePatternSource::trackAddress()`), so one screen row can show a
  different clip per column and regions act per track at its own rows. Each
  other track shows its position on a line of its own. Moving the cursor by
  hand moves every stopped track by as many rows, each on its line, so the
  highlighted rows move across still columns; the view scrolls only within
  `kScrollMargin` of an edge (Arrangement view too, but never before its
  first row). A playing track's line follows its playhead by the same margin
  rule (`keepTrackLinesVisible()`). A looping clip is periodic: the passes
  either side of the current one are shown dimmed, with no accents or row
  tint; rows before the launch are blank. Each column draws its own row
  numbers, bar/beat accents (from the scene's signature) and dimming, and
  blank where it has no row.
- Track headers end in "◆IMS" (◆ double width): orange ◆ while Live View has
  taken the track over, then Monitor, Mute, Solo. Only leaf tracks have
  Mute/Solo/Monitor and Direction. The last column is the master's,
  scrolling with the tracks: its slots launch scenes, its last row stops
  every track, and its Sends/Main dials are the song's dry level and the
  bus's two returns (sends are every `Track`'s, stored and set the same way).
- Each clip slot shows its state as its Launchpad pad does
  (`ClipPlayer::clipHighlight()`, the one source for both): a glyph in the
  icon's place — green ▸ playing / ▹ queued, dim ▸ paused, red ● recording /
  ○ queued to record, dim red ○ armed-empty or ● take stopping — and the slot
  background pulses like the pad (`statePulse()`: smooth over a second for
  playing/recording, a 250 ms flash for queued), so shape tells queued from
  running without motion.
- **Sends** are `cellui::Dial`s (`Dial.h`) in identical 5x3 slots: A and B in
  a 4-row Sends group, Main centred in a 3-row group of its own
  (`SendsDirectionRow`). The pointer is the send's dB over the arc
  (`model/SendFader.h`: 16 positions, off then -42 to 0 dB in 3 dB steps; the
  Launchpad faders and the recorded glide commands rest on the same ones). The plane decides picture or
  text (`dial_image::render()` or letter over dB), so a backend without
  pictures needs nothing. Gestures are the widget's: click starts typing a
  value over the dial (rounded to a position, no unit, nothing above 0; Enter
  commits, Escape, focus loss or a click elsewhere closes), drag turns it
  (a position a row, one undo step per drag), wheel turns it (a position). `ClipGrid` only
  places the dials, gives them values and `DialSpec`, passes on input, and
  calls `Dial::startEdit()` on Enter. A dial has four looks (normal, pressed, selected from half the cursor's ease-in, and hover, set from the grid's pointer motion), so a position has four pictures. Every column has a vertical level meter
  beside its Main and Direction rows (`LevelMeter.h`: one dB mapping,
  `Ballistics` smoothing block RMS in the power domain, bars shaded green →
  orange → clip red via `StyleProvider::levelColor()`, a `PeakHold` marker);
  the pattern editor's track meters and the scope row's `ChannelMeter`
  (`getChannelLoudness()`, per-channel RMS) share it, and the master's reads
  the master track's `TrackInfo`.
- **Scrolling and the mouse**: the wheel scrolls the widget under the mouse
  without moving focus, and its view, never its cursor (so never the
  transport); Shift or the sideways wheel scrolls tracks, and the next cursor
  move brings the view back. A notch moves three rows (the outline: one), more while notches
  come quickly (`cellui::WheelAcceleration`); scrolling widgets ease to the
  new position. Left press picks cells: `ClipGrid` shows the cell pressed and
  on release moves cursor and track there and presses the slot like Enter;
  `ArrangementGrid` moves the cursor to bar and track (Enter still commits);
  `PatternEditor` moves the cursor to the cell (a heading click picks the
  track, one past the last picks the locator slot) and clears the mark, and a
  drag sets the mark at the press cell and extends to the pointer. A drag is
  a `MouseMove` with the button held (`MouseMove::buttons`), sent to the
  widget that got the press; a `MouseDown` is always a fresh press.
  **Dragging a clip** (`cellui/DragDrop.h`): a press on a
  populated clip slot is a candidate (its own slot is one too, and dropping there
  does nothing); once the pointer has moved
  (`dragStarted()`) `ClipGrid` posts `StartDrag` (refused where the display has
  no drag and drop, and the press stays a click) and holds a `DragGhost`
  drawn by the same `putClipSlot()` as the grid's slots, its own slot showing
  empty meanwhile (while Ctrl is held, the drop copies and it stays). As the
  target it accepts a clip slot of a track that `trackHoldsKind()` the clip's
  kind (the rule of `yank`), shows the clip in it at half strength, and a
  `Drop` is `placeClipCopy()` plus `ClipPlayer::deleteClip()` of the source (so
  a playing clip stops first, as for a cut) in one edit.
  **Dragging a placed clip** (`ArrangementGrid`, same session): a press on a
  block is the candidate, the block is found with `resolveInstanceForBar()` as
  the bars it spans (a looping clip's run ends with the shown bars; its bars
  show empty while it is lifted); what is dragged and shown is one pass of the
  clip (its length in bars), however long it runs once placed, and keeps the
  bar grabbed under the pointer; the bars one pass would cover show it at
  half strength. A drop moves the placement (`removeInstanceLeavingSilence()` then
  `placeClipInstance()`, one edit) to the bar under the grabbed point; onto
  another track of the same kind the clip goes there as an independent copy in
  the first free slot. Ctrl places another and keeps the first. The clip and
  its other placements stay.
  **Dragging a track** (`TrackDrag.h`; the clip grid's header, the arrangement
  grid's name): a candidate on a press there, a ghost of the header, the source
  dim, and the target marked (tint and an arrow to the side it lands on); any
  cell of a column or row is a target if `Song::canMoveTrack()` (siblings only);
  a drop is `Song::moveTrack()` (the document's `moveChild`, one undo step) and
  makes the moved track the current one, which the grids' cursors follow.
  `ClipCard.h` is how a clip looks on a grid wherever one shows (slot, block,
  ghost): a card lifted off the grid so the display lights and shadows it.
- **Scopes**: the spectrum is a `cellui::LineChart` fed by
  `spectrum_curve::values()` (`ui/SpectrumCurve.h`: dBFS bins on a log axis,
  80 dB range); it, the DirAC heatmap and the level meter sit side by side in Arrangement view's top band
  and in Live View's left column under the outline (only while the outline
  is shown and the screen is tall enough); off-screen scopes are not
  updated. Under the display floor (the spectrum below its 80 dB range, DirAC energy under
  `DiracAnalyzer::kDisplayFloor`) a scope paints its empty state once and asks for no frames until a value
  crosses the floor upward; a voice that never ends (a note held when the transport pauses) keeps them going. Cell or pixel versions are chosen once at startup
  (`cellui::chartsFor()`).
- A fresh session opens in Live View on the clip grid
  (`UI::setInitialView()`, `--view`).
