# Clip editor in PatternEditor (arrangement / session views)

Goal: `PatternEditor` gains a second mode that shows and edits only clip
content, one **scene** (clip-list row index, the same row Session view and
the Launchpad Session grid address) at a time, with one playhead per track.
Views stop being buffers: a buffer is a song, and a view is how the UI is
laid out around it.

Status: Phases 0-4 committed; Phase 5 done (not yet committed). Every phase lands as its own commit(s), with `ctest`
and the e2e scripts green.

## The cursor model

- **Column** = the current track (`Song::getCurrentTrackId()`), shared
  with every other widget, in both modes, as today.
- **Row**:
  - Arrangement mode: the transport edit position, as today.
  - Session mode: a plain editor-local `(scene, row)` owned by the
    session source, independent of every playhead. Moving it never
    touches playback. Playheads are display-only: each track tints the row
    its playing clip is on, but only when that clip is in the displayed
    scene. A "follow playhead" option (the cursor row follows the current
    track's playing clip) can come later, once it's clear it's wanted.

**Scenes are emergent.** A scene is just a clip-list row index: the
clips that share row k across tracks. No scene object, no names; the
title row shows only the index. Row alignment across tracks already
exists (holes are real empty `Clip`s, `Song::ensureClipAt()`), which is
all an emergent scene needs.

## Where things stand today

`PatternEditor.cpp` is 3413 lines. Everything it shows or writes comes
from:

1. **Row addressing**: `getPlaybackInfo().getPatternIndex()/getRowIndex()`,
   `Song::normalizePosition()` (about 20 call sites).
2. **Cell content**: `resolveReadTarget()`/`resolveEditTarget()`
   (`ArrangementOps.h`): a placed instance, the focused-clip override, or
   the background Pattern.
3. **Column shape**: `getTrackInformation()`, which combines the section
   scan, the placed instances' leaf patterns, and the `SongStructure`
   baselines.
4. **Per-row extras**: section annotations, section title rows,
   neighbouring-section dimming, the instance tint.

Views are tied to buffers through `Controller::BufferAspect` (the
`" [Session]"`/`" [Outline]"` suffixes and `open_aspects_by_song_`), from
which `TerminalUI::workspace_aspect_` is derived. Session triggering
(`triggered_pattern_by_track_`, `audition_clock_`) lives in
`LaunchpadManager`.

---

## Phase 0: minimal refactoring, only where it pays off directly

Rule: extract something only if it is reused, if it moves out of `tui/`
into `ui/` to be shared with a future GUI, or if it is clearly separate
from the terminal widget. File length alone is not a reason.

### 0a. `InlineEditor` (reused 5×)

The reader lifecycle (open over target cells, force a repaint on
close/cancel, commit on Enter against the row/track captured at open
time, abort on C-g, `cancelReaderEdit()` for M-x) is implemented five
times:

- `PatternEditor`: annotation and track name
- `SessionView`: clip name and track name
- `ArrangementGrid`: section name

Extract one `src/ui/tui/InlineEditor` (it needs `UIPlane::showReader()`
and the repaint-over-cells detail, so it belongs in `tui/`). Each widget
supplies the position, width, initial text, and a commit callback. This
removes PatternEditor's `annotation_edit_*`, `track_name_edit_*` and
`force_redraw_` fields, plus the equivalents in the other two widgets
(SessionView's `last_styles_` too). PatternEditor keeps its
`last_styles_` for the track-name scroll correction. The owner repaints
the field backdrop at the end of every `render()`, and all three widgets
now block global keys and are cancelled by M-x while editing.

### 0b. `PatternSource` in `src/ui/` (needed by Phase 3, shared with a GUI)

It is toolkit-agnostic, and a GUI pattern editor needs exactly the same
answers, so it lives in `src/ui/` and can be linked from
`tests/synth_tests`.

```cpp
struct RowAddress { int block; int row; };   // block = section or scene

class PatternSource {
 public:
  // Row half of the cursor (arrangement: transport edit position;
  // session: the source's own local (scene, row))
  virtual RowAddress cursorRow() const = 0;
  virtual void moveCursorRow(int delta_rows) = 0;
  virtual void setCursorRow(RowAddress) = 0;

  // Display-row mapping (replaces normalizePosition()/getPatternIndex() use)
  virtual std::optional<RowAddress> offset(RowAddress from, int delta_rows) const = 0;
  virtual std::string blockTitle(int block) const = 0;   // section name / scene index

  // Content
  virtual ReadTarget read(int track_id, RowAddress) const = 0;
  virtual EditTarget edit(int track_id, RowAddress) = 0;   // may create a clip in session mode
  virtual void collectTrackInfo(RowAddress first, int rows,
                                std::unordered_map<int, VisibleTrackInfo> &) const = 0;

  // Other tracks' playhead tint (arrangement: the one transport row)
  virtual std::optional<int> playheadRow(int track_id, int block) const = 0;

  // Capabilities, so the UI hides features instead of branching on mode
  virtual bool hasAnnotations() const = 0;
  virtual bool supportsCopyToClip() const = 0;
};
```

- `ArrangementPatternSource` takes over today's logic unchanged:
  `getTrackInformation()`, `apply_baseline_track_info`, the resolve calls,
  and annotations. PatternEditor's ~20 `getPatternIndex()`/`getSection()`/
  `normalizePosition()` sites switch to `source_->...`, and
  `selection_start_pattern_` becomes a block index.
- One PatternEditor widget swaps between the two sources, rather than two
  subclasses. The cursor column, the mark and the clipboard carry across.
- Tests: read/edit/row mapping against the existing fixtures (instance vs.
  background, crossing a section boundary, a looping instance's wrap, the
  focused-clip override).

Implemented as: `PatternSource` (`src/ui/PatternSource.h`) with notes
via `read()`/`edit()`, and effect commands plus block operations via a
per-block `PatternGrid` (`src/model/PatternGrid.h`), which
`PatternBlockOps` now takes instead of a `Section`. The arrangement grid
is the section background, as before: block operations and commands
never touch clip instances there, even on rows showing clip notes. That's
existing behaviour, kept unchanged. Annotations, `insertRow()`,
`hasInstance()`/`stopInstance()` and `sampleBackground()` cover the rest.
Transport-keyed recording calls (`ensureNoteRecordingClip()` etc.) still
read the playback position directly.

### 0c. Track commands the menu offers become global

Done. `add-instrument-track`, `add-sample-track`, `add-percussion-track`,
`add-group-track`, `delete-track` and `apply-preset-*` live in
`UI::initializeCommands()` and act on `Song::getCurrentTrackId()`. Their
keys (C-t, C-r, C-S-d) are in TerminalUI's global keymap, so they work
from every widget, the same as the Track menu. Before, the menu
advertised C-t, but it only worked in the pattern editor.
`toggle-track-collapse`, `add-`/`remove-note-column` and `rename-track`
stay in PatternEditor. `toggle-mute`/`-solo` are unchanged (PatternEditor
and SessionView each keep their own).

Fixed along the way: PatternEditor wrote its cursor track into the
shared current track on every frame, overwriting what ArrangementGrid had
just selected (e.g. C-t in the grid added the track after the pattern
editor's track instead). It now writes the shared track only when its
cursor moves, it gains focus, or the shared track isn't a valid track
(startup, or it was deleted). `delete-track` makes the track now at the
deleted one's position current.

### Not doing

- Moving command bodies to another file only to shorten the main one.
- Splitting `renderRow()`/`renderHeading()` out. The renderers are
  inherently terminal-specific and have no second user.
- Extracting selection/mark state. Mark/kill-ring is permanently
  terminal-only by convention, and a GUI uses its own copy/paste.
- Live note input and recording (`active_*_notes`, `auto_*`,
  `handleMidiEvent()`) *is* backend-independent, but moving it adds
  nothing until Phase 3 needs it. Reassess then, and move it to `src/ui/`
  if session-mode recording ends up sharing it.

Exit criteria: no user-visible change.

---

## Phase 1: a read-only session playhead snapshot

Done (not yet committed). `LaunchpadManager` always exists, with or
without a device, and already runs Session-view clip playback, so the
clip editor only needs to read it: `LaunchpadManager::sessionPlayheads()`
returns `track_id -> {clip_index, row, queued_clip}` (row -1 while the
audition clock isn't running). The row math is `clipPlayheadRow()` in
`LaunchpadTiming.h`, unit-tested there. TerminalUI, which already wires
`LaunchpadManager`, hands the snapshot to PatternEditor and ClipGrid in
Phase 3, so neither depends on Launchpad code.

Deferred, optional: moving session playback itself (triggering,
bar-quantized launch/stop, recording queues, the audition clock) out of
`LaunchpadManager` into a Controller-owned `SessionPlayer`. It's cleaner
ownership, but a large move with weak e2e coverage (the Launchpad LED
checks are flaky here), and nothing in this plan needs it.

---

## Phase 2: separate views from buffers (Arrangement / Session)

Done (not yet committed), as below, with two deviations. The scope row
(cover art, ArrangementGrid, charts) stays in both views for now, since
the chart planes attach straight to the screen and can't simply be
hidden; removing it from Session view is part of Phase 4. The outline
panel starts hidden. `SessionView` is renamed `ClipGrid`, and Controller's
`setSessionViewFocused()`/`setSessionViewCursor()` become
`setClipGridFocused()`/`setClipGridCursor()`.

- Remove `BufferAspect`, the `" [Session]"`/`" [Outline]"` buffers,
  `open*ViewBuffer()`, `open_aspects_by_song_`, and
  `activeSongHasOtherOpenViews()`. A buffer is a song again.
- Add `enum class View { ARRANGEMENT, SESSION }` on `TerminalUI`. It is
  global and survives buffer switches, like an Emacs window layout.
- Rename the `SessionView` widget to **`ClipGrid`** (a grid of clip slots,
  tracks as columns, mirroring `ArrangementGrid`), since "Session view"
  now names the whole view. The `session-view` command switches to the
  view.
  - **Arrangement**: today's layout (scope row with cover art,
    `ArrangementGrid` and charts, plus `PatternEditor` using the
    arrangement source).
  - **Session**: `OutlineView` as a panel on the left of `ClipGrid`, with
    `PatternEditor` below them, still using the arrangement source in this
    phase. This is the same place the browser sits in the live-sequencer
    lineage. `toggle-outline` shows or hides the panel. OutlineView's own
    layout (its tree beside a details panel) has to fit a narrow column;
    see Phase 4.
- Real per-view rect assignment replaces `layout()`'s raise-to-top trick.
  Widgets that aren't in the current view drop out of focus and click
  activation.
- Commands in `UI::initializeCommands()`: `arrangement-view`,
  `session-view`, `toggle-view` (Arrangement ↔ Session),
  `toggle-outline`. `outline-view` switches to Session view with the
  panel shown. Add a View menu.
- Launchpad `GridMode` stays decoupled from the terminal view.
- Fix-ups:
  - `Controller::isSessionViewFocused()`
  - the buffer-change listener, which should no longer close views
  - kill-buffer, which no longer needs "is this only a view" logic
  - the startup focus
  - the e2e scripts that open Session view through the buffer command

### Keybindings

- **Tab → `toggle-view`** (Arrangement ↔ Session), the live-sequencer
  convention. The outline panel is toggled through `toggle-outline` (View
  menu / M-x); a direct key can be added later.
- The pattern editor's current Tab use (cycling the cursor through a hex
  field's digits) is dropped, and column navigation stays exactly as it
  is: Left/Right step columns, Ctrl+Left/Right jump a track. Typing
  already auto-advances through a field's digits, so changing one digit
  means retyping the field. Giving digit-stepping to plain Left/Right
  instead was considered and rejected: every effect field would cost four
  presses to cross.

---

## Phase 3: session mode in PatternEditor (`ScenePatternSource`)

Done (not yet committed):

- **`ScenePatternSource`** (`src/ui/`) and **`SceneGrid`**
  (`src/model/PatternGrid.h`). Blocks are scenes: clip-list index k
  across every track, plus one empty scene past the last used one to
  create clips in. A scene is as long as its longest clip (one bar if
  empty). A shorter looping clip repeats, with the repeats dimmed
  (`ReadTarget::repeat_length`); a one-shot ends. The cursor is the
  source's own, per buffer, independent of the transport and every
  playhead.
- **Editing** writes into the clip, so every arrangement placement of it
  changes too. Typing into an empty slot creates a looping "Clip N" of
  the scene's length. Typing past a one-shot's end lengthens it to that
  bar (rather than refusing the edit). Effect commands and block
  operations (kill/copy/yank/transpose, insert/kill row) act on the
  clips. Clearing never creates a clip.
- **Display:** no clip-indirection tint, identifier digits or half-block
  edges; no annotations (no column, the cursor can't reach the slot).
  The cursor row gets a neutral grey tint. Each track's launched clip
  tints its own playing row green, in that track's column only, from
  `LaunchpadManager::sessionPlayheads()` (Phase 1), passed in by
  TerminalUI.
- **Transport:** in session mode the cursor moves while playing, and
  keyboard/MIDI entry never starts the transport, records at the
  transport's row, or writes aftertouch there (`PatternSource::
  cursorFollowsTransport()`).
- **Space** (`play-or-stop`): in Session view's pattern editor it
  launches the cursor's scene (all tracks together, bar-quantized; a
  running transport stops first, since launched clips only play while
  it's stopped), and pressed again stops every launched clip.
  Everywhere else (ArrangementGrid, the arrangement pattern editor, the
  clip grid) it toggles the transport as before. Play/Stop is gone from
  the Song menu, since Space isn't only the song's transport any more.
- **Clip menu**: Launch Clip, Launch Scene, Stop All Clips, New Clip
  from Selection, Merge Clip to Background, plus placeholders not built
  yet (Duplicate Clip, Double/Halve Clip Length, Toggle Clip Loop,
  Rename Clip...) that say so when invoked.
- The outline panel is shown by default in Session view. `C-x o` cycles
  in screen order: outline, clip grid, pattern editor.
- Tests: 7 `ScenePatternSource`/`SceneGrid` unit tests, and
  `tools/e2e/verify_session_pattern_editor.py`. `add-`/`remove-note-column`
  were checked by hand in session mode; they work with no new code.

Remaining from the original Phase 3 list:
- `next-scene`/`previous-scene` commands and a visible scene number
  (crossing a scene by scrolling works; Phase 4's clip grid selecting the
  scene covers most of the need).
- A queued-launch marker in the track heading.
- Opening a clip's Launchpad step grid moves the session cursor there.
- **Known limitation:** effect commands typed into a clip are stored but
  silent. Playback reads commands only from the section, not from clips,
  in the arrangement and in Session-view launches alike. See Phase 8.

---

## Phase 4: Session view layout

Done (not yet committed):
- No scope row in Session view: its widgets move below the screen, and
  resizing rebuilds the charts' plot planes there. `toggle-scopes` hides
  it in Arrangement view too (the arrangement grid with it).
- The clip grid has one row per scene (`ScenePatternSource::
  sceneCount()`: the used scenes plus an empty one, at least 8) and takes
  at most half the height, re-laid out as the count changes. Its cursor
  never sits on the header row; F2 renames the clip under the cursor, or
  the track where there's none (`M-x rename-track` works there too).
- Shared cursor (`TerminalUI::syncSessionView()`): the clip grid's clip
  row is the pattern editor's scene, and the current track is the column
  in both, whichever moved last. Unfocused, the clip grid still marks the
  clip being edited, faintly. The scene's whole row is marked faintly across every
  track (cells and dividers), the cursor's own cell brighter on top.
- The outline panel has a single, narrow layout: the tree across the
  panel, the row's action buttons as chips in a 3-row bar under it, and
  the details text (description, hints) in a popup beside the panel
  (`?`, following the cursor; `?`, Ctrl-g or Escape closes it). Escape
  reaches it at once (`UIElement::wantsBareEscape()`) yet still starts an
  Alt chord, so ESC x is still M-x. The groove target
  picker opens just above the bar. A divider column separates the panel
  from the clip grid.
- Tests: `tools/e2e/verify_session_view_layout.py`.

**Mouse-wheel scrolling** (done, not yet committed): the wheel reaches
the shown widget under the mouse, without moving focus
(`TerminalUI::offerInput()`), and scrolls its view, never its cursor - so
the pattern editor no longer moves the transport. PatternEditor has a
view anchor of its own (`view_block_`), so it can scroll above its
cursor's block too. PatternEditor, ArrangementGrid and ClipGrid detach
their view from the cursor (and the playhead) on a wheel scroll and
reattach on the next cursor move - for the arrangement grid, also when the
edit position it follows moves while stopped. Shift+wheel scrolls tracks
sideways. OutlineView already worked this way. Emacs's keep-point-on-
screen was not followed: in Arrangement view the cursor is the transport.
Test: `tools/e2e/verify_mouse_wheel.py`.

Follow-up: **unified cursor colours** (done). `StyleProvider`'s
`highlight_bg_color` is the clip grid's bright grey, used by every
widget's focused cursor (and the M-x selector), and
`highlight_unfocused_bg_color` is a faint grey for an unfocused widget's
cursor - the pattern editor's region, the arrangement grid's cell or
title row, the outline's row, the clip grid's Sends/Direction cells.
Coloured cells brighten their own colour: clip cells as before, an
arrangement instance cell by half toward white while focused (it already
shows the selected column's brightening otherwise). Velocity/delay take
the region's foreground inside it, as before.

All cursor and playhead colours lean cyan, apart from the neutral grey
bar highlighting. The arrangement pattern editor's playhead row is the
session cursor row's tint (`cursor_row_tint_color`); a session track's
own playhead is a stronger cyan (`playhead_tint_color`). The Session
view's headers are one strip: the clip grid's header row takes the
outline's brighter `heading_bg_color`, the divider between them carries
it, and the outline's shadow row under its heading is gone.

The design notes below are kept for reference.

The scope row (5 rows), plus ClipGrid's fixed 15 rows (18 columns per
track), plus the pattern editor doesn't fit a normal terminal.

Column alignment with the pattern editor was considered and rejected.
Pattern-editor tracks can be arbitrarily wide (chords add note columns,
plus velocity/delay/effect), so aligned session cells would be a mix of
huge empty boxes and cramped ones. The two widgets are linked by the
shared cursor instead, not by geometry.

**Plan:**

- **Session view = [OutlineView | ClipGrid] on top, PatternEditor
  (session mode) below**, each with its own fixed column widths and
  horizontal scroll. ClipGrid keeps its uniform per-track columns. The
  outline panel has a fixed width (about 30 columns), and ClipGrid takes
  the rest. OutlineView's details panel moves below its tree in that
  column instead of beside it. When hidden, ClipGrid takes the full
  width.
- **Shared cursor, not shared geometry.** The current track is the
  column in both, and ClipGrid's clip row is the scene PatternEditor
  shows. Moving in either one moves the other; ClipGrid scrolls on its
  own to keep the current track in view. The track's identity colour
  links the two (ClipGrid's clip cells, PatternEditor's heading).
- **ClipGrid gets at most half the vertical space** and keeps all its
  rows (clips, Sends, Direction). Its height is
  `min(content height, available / 2)`. When everything fits, nothing
  changes; otherwise it scrolls vertically, which it already does (pinned
  header, `ensureCursorVisible()`). PatternEditor gets the rest.
- **Clip rows follow the scene count.** With emergent scenes, the fixed
  `kClipRowCount = 8` becomes the scene count (the longest clip list plus
  one empty row to create into, at least 8). The Sends/Direction rows sit
  below them, so a song with many scenes scrolls to reach them. That's
  acceptable now that the view scrolls, and the rows are always
  reachable.
- **Outline panel layout.** At about 30 columns, the details panel
  can't sit beside the tree. Options:
  1. Action buttons (Delete, Add to Song, Preview, ...) in a row under
     the tree, and the details text in a popup opened on demand (Enter,
     or a Details button). The panel already uses a floating plane for
     its groove target-track picker, so a popup has precedent.
     (Suggested; recommended.)
  2. Details below the tree in the same column. Little room, since the
     strip is at most half the height.
  3. Details replace the tree while shown, toggled.
  4. The outline panel spans the full height, left of both the clip grid
     and the pattern editor, leaving room for the tree with details
     below it.
- **No cover art or scope row in Session view.** A global
  `toggle-scopes` lets Arrangement view reclaim those rows too.

---

## Phase 5: block commands act on clips in arrangement mode

Done (not yet committed): `SectionRegionGrid` (`src/model/PatternGrid.h`)
anchors a block operation per track on whatever supplies it at the anchor
row (a placed clip, the Launchpad-focused clip, or the background); rows
where another content takes over have no Pattern, so a paste stops there.
`PatternSource::readGrid()`/`editGrid()` take that anchor (the mark, or
the cursor; the cursor for yank/kill-row/effect entry), and
`getEffectiveSelectionBounds()` clamps the region to
`PatternSource::sourceRows()`, which the highlight then shows. Effect
commands stay on the section background (`PatternGrid::findCommands()`/
`obtainCommands()`), where playback reads them - Phase 8 decides whether
clips get their own. Tests: 5 `SectionRegionGrid` unit tests and
`tools/e2e/verify_arrangement_clip_region.py`.

The original design notes follow.

Today kill/copy/yank/transpose, kill-row and the effect column act on the
section background, even on rows showing a placed clip's notes, so
killing a region that shows clip notes clears hidden background notes.

- A selection holds notes from **one source only**: the background, or
  one clip instance (the one at the mark). Moving point across an
  instance boundary (background ↔ clip, or clip A ↔ clip B) clamps the
  region at that boundary instead of extending past it. That's the same
  "the mark never strands in another block" rule the section boundary
  already follows.
- The block commands then run on that source's grid:
  `ArrangementPatternSource` gains a grid over the instance's clip
  pattern (rows offset by the instance's start, wrapped by its length),
  chosen by what `resolveReadTarget()` finds at the mark. It needs no new
  PatternBlockOps: they already take a `PatternGrid`.
- Yank writes into whatever source is at point (the clip under it, or
  the background), clipped to that source's extent.
- The region highlight shows the clamped extent.
- The effect column: decide whether commands on clip rows go to the
  clip. Today playback reads commands from the section only, so a
  command stored in a clip would be silent; this needs the playback side
  too, or stays background-only.
- Tests: `PatternSource`/`PatternBlockOps` unit tests for clamping and
  clip-grid mapping, plus an e2e test killing a region that shows clip
  notes.

## Phase 6: failing e2e scripts

These fail identically before and after this plan's changes, and none is
in `docs/known_bugs.md`:

- `verify_patterneditor_cross_tuning_paste.py`: the "incompatible
  tuning" refusal check
- `verify_consonance_colors.py`: all 5 checks
- `verify_percussion_layout.py`: both checks
- `verify_launchpad_stepseq.py`: both checks
- `verify_launchpad_buttons.py`: CC94 moving the cursor, CC93/CC94 LED
  colours
- `verify_launchpad_notecustom.py`: CC96 and CC97 LED checks

For each, find whether the script or the app is wrong. Fix the script
if it's stale (e.g. screen positions predating the current layout), fix
the app if it's a real bug, or record it in `docs/known_bugs.md` if a fix
isn't feasible now.

Also failing, found along the way: `verify_launchpad_note_brightness.py`
(it read CC98's LED as a play indicator; CC98 is Capture MIDI/Draw now).

Outcome - all but one were the scripts or the harness. The one app bug:
the track picker's row kept a playing clip's pulse instead of its own
static colour (the picker pass overwrote the RGB but not the lighting
type). The rest:

- Stale fake binaries (gitignored, never rebuilt): notecustom.
- Stale expectations: consonance colours (the enharmonic retune), the
  percussion pad's idle red and its "BD2" note name, CC93/94's dim white
  in Note mode, a fresh session's overview focus, Send A's purple, the
  dB Send fader curve (the press was a no-op), the step grid's 4-step
  scroll.
- `pump(N)` taken for a sleep: it returns once the app goes quiet.
- `time.sleep()` while synth runs: the unread pty blocks synth's UI
  thread, and with it Launchpad I/O - the "sandboxed ALSA stall" that
  `docs/known_bugs.md` documented across a dozen scripts (those entries
  are gone now). `Screen.wait()` keeps reading instead.
- Fakes sleeping without reading their ALSA input: LED frames dropped.
  They `drain()` instead.
- `harness.is_playing()` passed vacuously: InfoLine's right half
  overwrote "PLAYING" whenever the buffer name was long. "PLAYING" now
  comes right after the time.
- Isolation: fakes register as "Launchpad X (e2e)" clients, which an
  interactive synth skips (`LaunchpadIO::acceptsClient()`), so a running
  session no longer grabs a test's fake device.

Not fixed: InfoLine's `track:col` field is hard-wired to 0.

---

## Phase 7: per-track input monitoring in the clip grid

Each track gets a **Monitor** setting in ClipGrid: the live audio input
is played through that track - its own effect chain, sends and spatial
position - to the speakers, so you can hear e.g. how the track's effects
treat your voice before (or while) recording.

- **Setting:** follow the live-sequencer convention of In / Auto / Off.
  In: always monitor. Auto: monitor only while the track is armed (Record
  Arm). Off: never. Default Off (Auto for a SampleTrack?). Persisted per
  track in the song XML. Shown and edited in ClipGrid (a new
  cursor-addressable row beside Sends/Direction, or a glyph on the
  header row next to M/S), plus a `cycle-monitor` command.
- **Engine:** capture currently runs only while recording or
  threshold-armed (`Player.cpp`'s capture-enable edge; captured blocks
  go to the UI as `RecordEvent`s). Monitoring needs capture running
  whenever any track monitors, and the captured block fed into that
  track's render on the audio thread as an input source ahead of its
  effects (`TrackState`/`SampleTrackState`), not routed through the UI
  thread. Mono downmix, matching how recorded input is stored.
- **Latency:** monitoring adds capture plus playback latency
  (`getCaptureDelayFrames()` and the playback delay the recording path
  already measures). Keep the period small while monitoring, and show
  the round-trip latency in the info line so it's not a surprise.
- **Feedback:** monitoring through open speakers with a microphone can
  howl. Warn on first enable (status line), and make Off the default.
- **Recording interplay:** recording keeps capturing the dry input as
  today; monitoring only changes what's heard.
- Which track types can monitor: SampleTrack certainly; for an
  InstrumentTrack the input would bypass the instrument and go straight
  into its effects. Decide whether that's wanted.
- **Clip grid shows clip state like the Launchpad does:** a playing clip
  (green, like the pad's pulse), one queued to launch or stop (green,
  flashing or a distinct marker), an armed track's empty slot (dim red),
  a take queued or recording (red, recording marked like the pad's red
  pulse), and a take queued to stop. Same source of truth as the pads
  (`LaunchpadManager`'s `SessionPadHighlight` states, extended from
  Phase 1's playhead snapshot), so the terminal and the device never
  disagree. Terminal cells can't pulse on their own; use a steady colour
  plus a glyph, or blink by redrawing on the audition clock's beat.
- Tests: a render test feeding a synthetic input buffer through a
  monitoring track and checking it reaches the output through the
  track's effect (e.g. a gain change), and that Off/Auto gate it.

---

## Phase 8: play effect commands stored in clips

Session mode (Phase 3) lets you type effect commands into a clip, but
nothing plays them:

- **Arrangement playback** (`SongState::renderBlock()`) reads commands
  only from the section's background pattern, never from a placed clip's
  own pattern. That's deliberate: its comment says every command lives
  at the track/section level, so automation survives regardless of which
  clip is placed. The same comment says the pattern editor doesn't show a
  clip's command column, which is no longer true in session mode.
- **Session-view launches** (`LaunchpadManager::fireClipStep()`, on the
  UI thread, driven by the audition clock) send only note events
  (`PLAY_NOTE`/`STOP_NOTE`) to the audio thread.

Work:

- **Decide the arrangement rule.** When a placed clip has commands and
  the background has commands on the same row, which wins? Options:
  - the clip's commands apply while it plays, the background's
    otherwise;
  - both apply, background first;
  - clip commands only in Session-view launches, never in the
    arrangement (keeps the current rule there).
  Recommended: both apply, clip after background, so the clip's own
  value wins on conflicts (e.g. two volume sets on one row). This keeps
  automation recorded into the background working under any clip.
- **Arrangement:** in `renderBlock()`, after the background commands,
  also process `getCommandsAt()` of the instance's clip pattern at the
  instance-relative row the notes already use.
- **Session-view launches:** commands must reach the audio thread at the
  right time. Either a new `PlaybackControlEvent` that carries a command
  for a track, applied like a row command in `SongState` (reusing the
  slide/set scheduling), or, better long-term, moving launched-clip
  playback into `SongState` itself (the deferred `SessionPlayer` move
  from Phase 1), where commands come for free with the arrangement path.
- **Pattern break (`ZBxx`)** is song-level and meaningless inside a
  launched clip: ignore it there (or make it restart the clip; decide).
- **Recording:** Launchpad fader automation written while a clip plays
  (`recordFaderAutomationIfArmed()`) currently goes to the background.
  Decide whether a Session-view take records it into the clip instead.
- Update the `SongState.h` comment and `docs/commands.md` to state where
  commands are read from.
- Tests: render tests with a fixture song where a placed clip carries a
  volume set (`0Lxx`) and the rendered level changes accordingly; a
  conflicting background command on the same row resolves by the chosen
  rule; and a unit test that a Session-view launch applies the clip's
  command.

---

## Open decisions

1. Later: a "follow playhead" option for the session cursor row, and
   optional scene names (a `Song`-level list keyed by row index), only if
   emergent scenes turn out not to be enough.
2. Should one view drop the effect tree (the pattern editor heading's
   track/group/effect hierarchy rows above the track columns)? It takes
   vertical space, and whoever wants it can switch to the view that
   shows it. The likely candidate is Session view, where the clip grid
   already competes for rows. That would need a pattern-editor option
   to draw only the leaf-track title row.
