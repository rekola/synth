# Clip editor in PatternEditor (arrangement / session / outline views)

Goal: `PatternEditor` gains a second mode that shows and edits only clip
content, one **scene** (clip-list row index, the same row Session view and
the Launchpad Session grid address) at a time, with one playhead per track.
Views stop being buffers: a buffer is a song, and a view is how the UI is
laid out around it.

Status: Phase 0a done (not yet committed). Every phase lands as its own commit(s), with `ctest`
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

### 0c. Track-admin commands move to `UI::initializeCommands()`

The following act on the current track and don't touch the widget, so
the Layout convention says they belong in the shared UI layer:
`add-*-track`, `delete-track`, `apply-preset-*`, `toggle-mute`/`-solo`,
`toggle-track-collapse`, `add-`/`remove-note-column`. Only their key
bindings stay in `TerminalUI`. `rename-track` stays (it needs the inline
editor).

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

## Phase 1: session playback state out of LaunchpadManager

Move `triggered_pattern_by_track_`, `queued_pattern_by_track_`, the queued
recordings, `audition_clock_`, `session_origin_step_`, and the
trigger/stop/quantize logic into a Controller-owned `SessionPlayer`
(`src/playback/`). `LaunchpadManager` and `SessionView` become clients.
Expose a snapshot map for display:
`track_id -> {clip_index, row, queued_clip}`.

The terminal widgets need this to draw per-track playheads without
reaching into Launchpad code. This phase is behaviour-neutral, and the
existing session e2e scripts are the regression net.

---

## Phase 2: separate views from buffers (Arrangement / Session / Outline)

- Remove `BufferAspect`, the `" [Session]"`/`" [Outline]"` buffers,
  `open*ViewBuffer()`, `open_aspects_by_song_`, and
  `activeSongHasOtherOpenViews()`. A buffer is a song again.
- Add `enum class View { ARRANGEMENT, SESSION, OUTLINE }` on `TerminalUI`.
  It is global and survives buffer switches, like an Emacs window layout.
  - **Arrangement**: today's layout (scope row with cover art,
    `ArrangementGrid` and charts, plus `PatternEditor` using the
    arrangement source).
  - **Session**: `SessionView` + `PatternEditor`, still using the
    arrangement source in this phase.
  - **Outline**: `OutlineView`, as it is today.
- Real per-view rect assignment replaces `layout()`'s raise-to-top trick.
  Widgets that aren't in the current view drop out of focus and click
  activation.
- Commands in `UI::initializeCommands()`: `arrangement-view`,
  `session-view`, `outline-view`, `toggle-view` (Arrangement ↔ Session).
  Add a View menu.
- Launchpad `GridMode` stays decoupled from the terminal view.
- Fix-ups:
  - `Controller::isSessionViewFocused()`
  - the buffer-change listener, which should no longer close views
  - kill-buffer, which no longer needs "is this only a view" logic
  - the startup focus
  - the e2e scripts that open Session view through the buffer command

### Keybindings

- **Tab → `toggle-view`** (Arrangement ↔ Session), the live-sequencer
  convention. Outline is reached through `outline-view` (View menu /
  M-x); a direct key can be added later.
- The pattern editor's current Tab use (cycling the cursor through a hex
  field's digits) is dropped, and column navigation stays exactly as it
  is: Left/Right step columns, Ctrl+Left/Right jump a track. Typing
  already auto-advances through a field's digits, so changing one digit
  means retyping the field. Giving digit-stepping to plain Left/Right
  instead was considered and rejected: every effect field would cost four
  presses to cross.

---

## Phase 3: session mode in PatternEditor (`ScenePatternSource`)

- **Rows**: the session source's own `(scene, row)` cursor (see the
  cursor model). Scrolling past the last row goes to scene k+1 (and before
  row 0, to scene k-1), like crossing a section boundary.
  `next-scene`/`previous-scene` jump directly. The title row shows the
  scene index. Scenes are emergent, so the scene count is the longest
  clip list across tracks, plus one empty scene at the end to write new
  clips into.
- **Scene length** = the longest clip in the scene. Past a shorter clip's
  end, a looping clip shows its wrap dimmed (`unwrapped_row >= length`,
  the existing repeat mechanism) and a one-shot shows blank, non-editable
  rows. Empty filler slots render empty.
- **Editing** writes into the clip's leaf Pattern, and every arrangement
  placement of that clip reflects the change (clips are shared; this is
  intended). Typing into an empty slot creates the clip via
  `Song::ensureClipAt()` plus id assignment, at the scene's length (one
  bar if the scene is entirely empty). Add commands for clip length
  (double/halve or `set-clip-length`) and loop toggle.
- **No clip tint in session mode.** In arrangement mode the clip-coloured
  rows mean "indirection: these notes live in a clip, not at the position
  being edited". In session mode there is no indirection (the clip *is*
  what's being edited), so cells use the plain background, and the
  track's identity colour appears only in its heading.
- **Playheads** are display-only: each track tints its playing clip's
  row when that clip is in the displayed scene. A queued launch shows as a marker in that track's heading.
  `launch-clip` and `launch-scene` go through `SessionPlayer`. Space stays
  the global transport toggle.
- **Hidden in session mode** (capability flags): annotations and
  `copy-to-clip`. Live recording goes through the existing Session
  record-arm path. Keyboard auto-advance-while-held recording comes
  later, once it can follow a per-track playhead (see Phase 0's
  "not doing" note on live note input).
- **Selection/clipboard** are unchanged. `ClipboardEntry` is
  pattern-based, so copying in one mode and yanking in the other works.
- **Sample tracks** use the same waveform box, fed by the scene's clip.
- **Launchpad step grid**: opening a clip's step grid moves the session
  cursor to that clip's scene and track.
- The view picks the source: Session view uses `ScenePatternSource`, and
  Arrangement view uses `ArrangementPatternSource`.
- Tests:
  - `ScenePatternSource` unit tests: scene length, one-shot vs. loop
    past-end rows, create-on-write, crossing scenes.
  - One e2e test: in Session view, type a note in scene 2, then check
    that the clip exists and that an arrangement placement of it shows
    the note.

---

## Phase 4: Session view layout

The scope row (5 rows), plus SessionView's fixed 15 rows (18 columns per
track), plus the pattern editor doesn't fit a normal terminal.

Column alignment with the pattern editor was considered and rejected.
Pattern-editor tracks can be arbitrarily wide (chords add note columns,
plus velocity/delay/effect), so aligned session cells would be a mix of
huge empty boxes and cramped ones. The two widgets are linked by the
shared cursor instead, not by geometry.

**Plan:**

- **Session view = SessionView on top, PatternEditor (session mode)
  below**, each with its own fixed column widths and horizontal scroll.
  SessionView keeps its uniform per-track columns.
- **Shared cursor, not shared geometry.** The current track is the
  column in both, and SessionView's clip row is the scene PatternEditor
  shows. Moving in either one moves the other; SessionView scrolls on its
  own to keep the current track in view. The track's identity colour
  links the two (SessionView's clip cells, PatternEditor's heading).
- **SessionView gets at most half the vertical space** and keeps all its
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
- **No cover art or scope row in Session view.** A global
  `toggle-scopes` lets Arrangement view reclaim those rows too.

---

## Open decisions

1. Later: a "follow playhead" option for the session cursor row, and
   optional scene names (a `Song`-level list keyed by row index), only if
   emergent scenes turn out not to be enough.
