# Drag and drop (window only), starting with clips

Goal: drag a clip out of the clip grid; it detaches from its cell and follows
the pointer; releasing over a cell drops it there. Window (GTK) only - the
terminal never offers it. Steps 0-2 come in that order. Delete this file and the README's "Drag & Drop"
roadmap line when done.

## Scope

- In-app drag only (clip to clip). No OS-level drag between windows or files.
- The rules are the copy-paste rules in `ClipGrid` (`kill-ring-save`/`yank`):
  the drop target's track must be the same kind as the source's (a
  percussion clip never lands on a pitched track, audio only on audio), and
  the target slot is overwritten like a yank. Extract that check into one
  helper used by both `yank` and the drop so they cannot drift.

## 0. Prerequisite: a drag is a `MouseMove`, on every backend (done)

A drag used to arrive as a second `MouseDown` for the held button: the
terminal reports it as the press again and the GTK window copied that, so
`PatternEditor`, `ClipGrid` and `InteractiveUI` told a drag from a press by
the release (`mouse_down_`, `button1_was_held_`). Now the terminal's `toInput()`
and GTK's `motionEvent()` both send `Key::Motion`; the dispatcher gives
the widget that got the press a `MouseMove` with `buttons` set, and a
`MouseDown` is always a fresh press. `PatternEditor`, `ClipGrid`,
`ArrangementGrid`, `OutlineView` and `SpinBox` handle `MouseMove`;
`TestBackend::pushMotion()` simulates it. The GTK change is not compiled
in the cloud (no GTK 4); check it on a desktop build.

## 1. cellui: a drag session (done)

Events, not widget hooks, as everywhere in cellui (`cellui/DragDrop.h`):

- The source posts `StartDrag{source, DragPayload{kind, data}}` to the root
  once a `MouseMove` with the button held has passed `kDragThreshold`; the
  `InputDispatcher` begins a session only where `DisplayInfo::drag_drop` (the
  GTK window) and a button is held, else answers `Handled::no` and the
  widget behaves as before.
- While it runs: the widget under the pointer gets `DragOver` per move
  (`Handled::yes` accepts) and `DragLeave`; the source gets `DragMove` and
  `DragEnd{dropped}` and no ordinary mouse events; release over an accepting
  widget posts `Drop`; Escape cancels and the rest of the gesture goes
  nowhere.
- `DragGhost`: the plane that follows the pointer (modal elevation, the grab
  point kept under the pointer, sub-cell through `setScrollOffset()` where
  the display scrolls smoothly), with `GRABBING` / `NOT_ALLOWED` cursors
  (new `MouseCursor` shapes) and a redraw when acceptance changes.
  `InteractiveUI` enables sessions from the backend's `drag_drop`. The
  sub-cell placement and the look are unchecked in the cloud (no GTK 4): see
  them on a desktop build.
- Tests: dispatcher sessions (drop, refused drop, leave, Escape, no
  `drag_drop`, no button held) and the ghost's cursor in
  `lib/cellui/tests/InputTests.cpp`.

## 2. ClipGrid: dragging a clip

- A press on a populated clip slot becomes `pressed_`; once a `MouseMove`
  with the button held passes the threshold, start a session (payload: source
  track id, clip index, the `Clip` copy, track type) instead of leaving it
  to the release. A plain click still launches on release, unchanged; a
  drag never launches.
- First extract the slot drawing in `ClipGrid::render()` into
  `drawClipSlot(plane, y, x, clip, styles, state)`, so the grid and the
  ghost draw the same thing: track identity colour, clip name, loop icon,
  no playing-state colour or pulse.
- While dragging the source cell draws as empty (the clip "detaches"; it is
  not deleted until the drop commits, so Escape or a refused drop restores
  it) and the ghost is that slot drawing. The cell under the pointer shows
  a drop mark (the pressed-cell treatment with its own `StyleProvider`
  colour) when the target is valid, the ghost dimmed and no mark otherwise
  (wrong kind of track, master/scene/send rows, the header). Scroll the grid
  when the pointer nears an edge, as `PatternEditor` does for its drag
  (`kDragScrollInterval`).
- Drop = move: one `Song::Edit` that places the copy (`placeClipCopy`) and
  deletes the source (`deleteClip`), so it is a single undo step. Dropping
  on the source cell, or cancelling, changes nothing. Dropping on a populated
  slot overwrites it, as `yank` does.
- Ctrl-drag copies instead of moving (source stays) - the desktop-keymap
  convention; the terminal keymap has no drag, so no Emacs precedent applies.
- A clip that is playing or queued: check what `deleteClip` does to it
  (launches and stops apply on the next bar) and make the move follow the
  same rule rather than inventing one.
- Colours (ghost, drop mark, refusal mark) go in `StyleProvider`.
- Tests in `InteractiveUITests`: a drag moves the clip; a percussion clip
  dragged to a pitched track is refused and nothing changes; Ctrl-drag
  copies; one undo restores the move; a plain click still launches;
  nothing happens on a backend without `drag_drop`. No layout tests.
- Docs: `docs/user-interface.md` (the gesture), `src/ui/CLAUDE.md` (mouse
  section), `docs/glossary.md` if "drop" or "ghost" gets a project meaning.

## 3. Finish

- Remove "Drag & Drop" from the README roadmap once 1 and 2 land.
- Later, by the same session API: dragging clips in the arrangement grid
  and a sample file onto a clip slot.

## Open questions

- Should a drop on an occupied slot swap rather than overwrite? Overwrite
  matches `yank`; swap is friendlier but is a new rule. Plan: overwrite.
- Should the drag cross to the arrangement view (it has a different grid)?
  Plan: not now; Tab is a separate gesture.
