# Drag and drop (window only), starting with clips

Goal: drag a clip out of the clip grid; it detaches from its cell and follows
the pointer; releasing over a cell drops it there. Window (GTK) only - the
terminal never offers it. Delete this file and the README's "Drag & Drop"
roadmap line when done.

## Scope

- In-app drag only (clip to clip). No OS-level drag between windows or files.
- The rules are the copy-paste rules in `ClipGrid` (`kill-ring-save`/`yank`):
  the drop target's track must be the same kind as the source's (a
  percussion clip never lands on a pitched track, audio only on audio), and
  the target slot is overwritten like a yank. Extract that check into one
  helper used by both `yank` and the drop so they cannot drift.

## 1. cellui: a drag session

cellui already reports a held button's repeated press as a drag
(`InputDispatcher::offer()`), and a widget that gets the press keeps the
rest until release. What is missing is a drag that carries something.

- `DragSession` (new `cellui/DragDrop.h`): a payload (`std::any` plus a
  `kind` string so a target can refuse what it doesn't know), the source
  widget, and a ghost: a function drawing into a small floating `Plane`
  that tracks the pointer. Started by the source widget on a drag past a
  threshold (about 4 px, via `Input::getFx()/getFy()`), owned by the root
  (`InputDispatcher`).
- Dispatch while a session runs: motion goes to the root, which moves the
  ghost and hit-tests the widget under the pointer; it calls
  `dragOver(session, row, col)` on that widget (returns accept/refuse, so
  the cell under the pointer can show a drop mark or a "not allowed"
  mark) with `dragLeave` on the one before. Release calls
  `drop(session, row, col)` on the accepting widget, else
  `cancel` on the source. Escape cancels. Ordinary press/hover handling is
  suspended while it runs.
- Widget hooks (default no-ops, so nothing existing changes):
  `onDragOver`, `onDragLeave`, `onDrop`, `onDragCancel`.
- Gate it with a `DisplayInfo::drag_drop` flag, true only for the GTK
  backend (it has precise pointer and floating planes). Where it is false
  `ClipGrid` never starts a session and presses behave as today.
- Ghost placement: sub-cell positions need the precise mouse the window
  already provides; the ghost plane is a `BufferPlane` child of the root
  above everything else (check ordering in `Plane`, add a "top" layer if
  there is none), drawn with `setLift()` so it reads as picked up.
- Tests in cellui (`TestBackend`): session starts after the threshold,
  accept/refuse reach the right widget, Escape and release-outside both
  cancel, no session when `drag_drop` is false. Document it in
  `lib/cellui/CLAUDE.md` and the cellui README.

## 2. ClipGrid: dragging a clip

- A press on a populated clip slot already becomes `pressed_`; once the
  pointer has moved past the threshold, start a session (payload: source
  track id, clip index, the `Clip` copy, track type) instead of leaving it
  to the release. A plain click still launches on release, unchanged; a
  drag never launches.
- While dragging the source cell draws as empty (the clip "detaches") and
  the ghost shows the clip's name and colour. The cell under the pointer
  shows a drop mark when the target is valid, a refusal mark otherwise
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
