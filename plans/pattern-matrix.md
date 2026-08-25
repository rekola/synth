# Pattern Matrix: song grid + single-cell copy/paste + Launchpad overview

## Context

An earlier, retired plan split the old all-tracks-at-once `Pattern` into
today's `Scene` (one song position, owning one `Pattern` per track) plus a
per-track `Pattern` (`Scene.h`/`Pattern.h`). That's the only prerequisite
this plan needed, and it already landed. The referenced-pattern-pool half
of that old plan never did and still isn't needed here - see "Future: the
full Matrix" below for what's still worth keeping from it.

This plan covers: a read-only overview of which (scene, track) cells have
content, cursor navigation over that grid, single-cell copy/paste, shared
track/scene selection across `PatternEditor`/`PatternMatrix`/Launchpad,
and a Launchpad `GridMode::OVERVIEW` that mirrors the terminal grid. All
of it is built on `Song`/`Scene`/`Pattern` as they exist today - no XML
format changes.

Component: `PatternMatrix` (`src/ui/PatternMatrix.h`/`.cpp`).

## Data layer

- `Pattern::isEmpty()` - `notes_.empty() && commands_.empty()`.
- `Pattern::hasSoundingNote()` - true iff at least one note is a genuine
  note-on (`Note::isDefined() && !isOff() && !isAftertouch()`; correctly
  covers percussion note-ons too, since that check never looks at
  `Tuning`). Distinguishes "has content" from "produces sound" - a
  `Pattern` can be non-empty from note-offs/aftertouch/`Command` data alone.
- `Scene::setPatternForTrack(track_id, Pattern)` - replaces one track's
  whole `Pattern` in one call; used by `yank`.
- `Song::getOrCreateScene(int i)` - `getScene()`'s write-intent
  counterpart: grows `scenes_` (via `addScene()`) up to and including `i`
  instead of falling back to `getScene()`'s shared, process-wide
  `empty_scene_` sentinel. Every call site that *writes* into the Scene at
  the current edit position uses this instead of `getScene()`: raw
  keyboard and MIDI note/command entry, Launchpad pad-press note entry,
  annotation commit, `insert-row`, and `PatternEditor`'s own `yank`
  (`PatternEditor.cpp`, `LaunchpadManager.cpp`). Read-only call sites
  (`kill-ring-save`, rendering, `kill-row`/`transpose-region-*` - nothing
  real to act on if the Scene doesn't exist anyway) stay on `getScene()`
  deliberately; growing the song just from reading/rendering would be its
  own bug. Unit-tested directly in `tests/SongTests.cpp`.
- `Pattern` is a plain copyable value (no manually-deleted copy ops), so
  `yank`'s deep copy and `Scene::setPatternForTrack()` need no special
  handling.
- Grid cell state is three-way, not boolean: populated (`scene.
  getPatternsByTrack()` has a non-empty `Pattern` for that track), empty,
  not-applicable (`DrumMachineTrack` - its step sequence lives on the
  track itself, never in per-scene `Pattern` data, so it can never be
  populated - see "Next phase" below).

## `PatternMatrix` widget

- A `UIElement` subclass shaped like `PatternEditor`: own `keymap_`/
  `commands_`, `dispatchCommand()` early in `offerInput()`; arrow-key
  cursor movement is handled directly in `offerInput()`, not registered as
  a command (matches `PatternEditor`'s own convention).
- **Always visible**, in the top-left corner of the screen's "scope row"
  (`UI::layout()`'s `kScopeRow`/`kScopeHeight`, shared with `chart_`/
  `heatmap_`/`volume_meter_`) - not a togglable view, not one of
  `windows_`'s click-to-reveal popups. Claims the leftmost columns first;
  the three scopes divide whatever width is left, dropping `heatmap_`
  first and `volume_meter_` second if it doesn't all fit. Height fixed at
  `kScopeHeight` (5 rows: 1 header + ~4 data rows), so `PatternEditor`'s
  own vertical budget is untouched. Click-activatable via `tryActivate()`,
  same as `octave_control_`.
- **Grid axes**: rows = `song.getScenes()` in existing sequential order;
  columns = `song.getRootTrackIds()` filtered to color-eligible tracks
  only (`VisibleTrackInfo::color_ordinal_ >= 0` - every `LeafTrack`, never
  an Effect - an Effect never carries per-scene note content of its own;
  see "Copying an Effect's own automation" below for the consequence).
  `PatternMatrix::getVisibleTrackIds()` is public so Launchpad OVERVIEW
  mode addresses the identical column list.
- **Columns are 2 character cells wide** - the track's own glyph plus a
  blank separator, explicitly painted every frame (`ncplane_erase()` only
  clears content, not color - the same gotcha `TerminalUI.cpp`'s
  plot-plane code already documents). The separator gives a two-digit
  header ordinal (10+ tracks) somewhere for its second digit, and gives
  the populated-cell glyph (ambiguous East Asian Width in some fonts)
  somewhere to spill without overlapping the next column.
- **Scrolling**: `scroll_row_`/`scroll_col_`, cursor-follows-viewport via
  `ensureCursorVisible()`.
- **Colors**: track identity is hue only, near-fully saturated -
  `Color::fromHSL(baseline.getHue(), 0.8f, 0.45f)`, one `identity_color()`
  helper used for both the header digit and the data glyph.
  `VisibleTrackInfo::getColor()` (35%/42%, tuned for a background bar with
  white text) is too faint as foreground/text color, so this plan factored
  the hue out as `VisibleTrackInfo::getHue()` rather than duplicating the
  golden-angle formula. Cell backgrounds stay plain `window_bg_color`; only
  the playhead row gets any background treatment (below). Track identity
  lives entirely in the glyph's foreground color. Lightness is tuned
  independently from Launchpad's own OVERVIEW LEDs (see "Launchpad
  integration" below) - a directly-emitted LED pixel at a given lightness
  reads brighter than the same value does as terminal glyph text.
  - Header row: the track's ordinal, in `identity_color()`, **bold**
    (`UIPlane`/`UIElement::setBold()`, new - mirrors the existing
    `setUnderline()` exactly, same "replaces the whole style state, not
    additive" contract), on the plain background - no background bar.
  - Playback-position row: `PlaybackInfo::getPatternIndex()` (same source
    `PatternEditor.cpp`/`InfoLine` already read) highlights that row.
  - Cursor cell: same "always some highlight, even degenerate" convention
    `PatternEditor`'s own selection highlighting uses. **Local and
    passive** - moving it never touches `PatternEditor`, the playhead, or
    any Launchpad; only Enter commits it (see "Global track/scene
    selection" below). Only drawn while this widget is `active_element_`
    - see "Focus-gated highlighting" below.
- **Cell glyph**, four states:
  - Sound-producing: `🗏` (U+1F5CF PAGE) - `Pattern::hasSoundingNote()`.
  - Content but nothing sound-producing: `🗌` (U+1F5CC EMPTY PAGE) -
    `!isEmpty() && !hasSoundingNote()`.
  - Empty: nothing - a genuinely blank cell, no glyph.
  - Not applicable (`DrumMachineTrack`): `✕` (U+2715 MULTIPLICATION X,
    regular weight - not `✗` BALLOT X's hand-drawn look, not `✖` HEAVY
    MULTIPLICATION X's over-heavy one), in `StyleProvider::error_fg_color`
    (`#dc3c3c`, new - no red/error color existed before this).

## Extending the song by moving past the last row

The row axis allows the cursor one position *past* the last real `Scene` -
a virtual, not-yet-instantiated row - instead of clamping at the last real
one. `ensureCursorVisible()` treats this as `num_scenes + 1` valid slots
rather than a separate special case (which also makes a brand new,
zero-`Scene` song show one real, navigable row instead of none).
`render()`'s row loop needs no special-casing either: `Song::getScene()`
already returns a shared empty `Scene` for an out-of-range index, so the
virtual row renders as an ordinary all-empty row.

Not unbounded scrolling (moving past the virtual row does nothing
further), and not instantiated by mere navigation:

- Plain Down/Up and Enter only move the cursor/commit a (track, scene)
  pair - `Controller::setEditPosition()` happily targets a not-yet-
  existing position, the same tolerance `PatternEditor`'s own row
  navigation already has for running off the end of the last real
  pattern. Looking at/jumping to the virtual row never creates it.
- `yank` is the one place it gets created: pasting onto the virtual row
  (`cursor_scene_ == song.getScenes().size()`) calls `song.addScene()`
  first. `kill-ring-save` needed no change - it already refused to copy
  from an out-of-range position (nothing to copy), which is exactly
  correct for the virtual row too.
- In `PatternEditor`, reaching the virtual row via Enter and then writing
  a note/annotation/command there goes through `Song::getOrCreateScene()`
  (see Data layer above), not `getScene()` - the write actually grows the
  song instead of landing in the shared sentinel.

This keeps "look at this position" and "put real content here" as two
separate actions, matching `PatternEditor`'s own navigation-vs-editing
distinction.

## Copy/paste/cut a single cell (`kill-region`/`kill-ring-save`/`yank`, not new verbs)

`PatternMatrix` defines commands under `PatternEditor`'s own Emacs names -
`kill-region` (C-w, cut), `kill-ring-save` (M-w, copy without deleting),
and `yank` (C-y, paste) - rather than inventing
`cut`/`copy`/`paste-scene-cell`. Each `UIElement` has its own local
`commands_` registry, and `UI::executeCommand()`/`dispatchCommand()` check
the active element's own registry first, so the same C-w/M-w/C-y keystroke
reaches whichever of the two widgets is focused, each with its own
differently-shaped logic - no new dispatch mechanism needed. All three are
defined here even though `kill-region`'s body is barely more than
`kill-ring-save`'s: `UI::executeCommand()`'s named-command dispatch (M-x,
Launchpad) falls through to `PatternEditor`'s own registry for any command
name this widget doesn't define, so a bare, undefined `kill-region`
invoked while the Matrix has focus would silently cut `PatternEditor`'s
own selection instead of the Matrix's cell - the same leak
`move-row-up`/`move-row-down`'s own no-op definitions already guard
against for a different pair of commands.

- `PatternMatrix` has its own clipboard, not `PatternEditor`'s
  `ClipboardEntry`/`PatternBlock` (shaped for row-range selections within
  one track's pattern): `std::optional<Pattern> cell_clipboard_` plus the
  originating `track_id`.
- `kill-ring-save` copies the `Pattern` at (cursor scene, cursor track)
  into the clipboard (an empty `Pattern` if the track has no entry).
  `kill-region` does the same, then also clears the cell via
  `Scene::setPatternForTrack(track_id, Pattern())`. Both no-op on a
  `DrumMachineTrack` column.
- `yank` writes the clipboard into the destination scene via
  `Scene::setPatternForTrack()` - always a deep, independent copy; editing
  the pasted cell afterward never touches the source. This sidesteps
  everything Part 1 of the original Matrix plan worried about (usage
  counting, edit-everywhere semantics, instrument-compatibility) since
  there is no sharing to reason about. Always targets the track the copy
  came from, regardless of the cursor's current column - matches "clips
  are track-specific" from the original plan. Reports "Clipboard empty"
  rather than doing nothing silently when there's nothing to paste.

### Copying an Effect's own automation (not solved here)

An Effect track can carry its own per-scene `Command` automation (its own
`track_id`'s slot in the same `Scene`, independent of whatever leaf track
it's nested under) - `kill-ring-save`/`yank` never touch it, since Effect
tracks aren't columns in this grid at all. Copying a leaf track's cell
today silently leaves any automation on its nested Effect children behind.

Not solved because more than one leaf track can share the same parent
Effect subtree (a send/group effect feeding several instruments at once) -
that shared `Command` data has no single owning cell to fold into. Real
follow-up question: does a leaf track's copy stay scoped to its own
`Pattern` forever, with Effect automation copied independently later via
its own UI? Or does it bundle automation only when the Effect isn't
shared (needs an "is this Effect exclusively owned by this track" check
`SongStructure`/`Track` don't expose today)? Either way, the grid should
probably surface the gap visibly rather than leaving it a silent
data loss.

## No undo integration needed

No undo/history system exists in this codebase, so `yank` is a plain,
immediate mutation like every other pattern-editing operation.

## Testing

- Model-level: `tests/SceneTests.cpp` (`Pattern::isEmpty()`/
  `hasSoundingNote()`, `Scene::setPatternForTrack()`'s deep-copy semantics)
  and `tests/SongTests.cpp` (`Song::getOrCreateScene()`).
- No audio/render path is touched - nothing belongs in `RenderTests.cpp`.
- Interactive/Launchpad behavior: `tools/e2e/verify_launchpad_overview.py`
  plus manual verification via `./build/synth` - no other headless path
  exists for notcurses UI.

## Next phase (future) - drum machine cells

`DrumMachineTrack`'s step sequence is track-global (one `loop_length_`-step
loop repeating for the whole song, never per-scene), which is why every
`DrumMachineTrack` column is permanently not-applicable here. Making it a
real cell (populated/empty, `kill-ring-save`/`yank`-able) needs a data
model change, deferred to its own follow-up plan:

- The goal is consolidation, not a parallel sibling: a per-(scene, track)
  step-pattern container structurally next to `Pattern` is the easy first
  cut, but landing there and stopping leaves two permanently separate
  representations `Scene` has to know about. That phase should aim for one
  unified per-(scene, track) content representation both a tracker track
  and a drum machine track resolve into, not necessarily forcing step/lane
  bit data into `Pattern`'s own shape verbatim.
- `DrumMachineTrack::getSequenceId()`/`setSequenceId()` already exist,
  carried through save/load for "the later reusable-named-sequence
  roadmap item" but unused - that phase should resolve through this hook,
  not invent a second id scheme.
- Once real per-scene step content exists, `kill-ring-save`/`yank` extend
  to it via the same deep-copy semantics already built here - a second
  cell payload type, not a new copy/paste concept.
- Open design fork for that phase: does a copied-in step pattern become
  that scene's *only* content while active (replacing "one loop for the
  whole song"), or does the whole loop model need reworking into
  per-scene first?

## Global track/scene selection

Track selection is one piece of state shared by every window on the
buffer and every attached Launchpad, not owned by whichever widget is
focused.

- **Where it lives**: `PatternEditor`'s existing `getCursorTrackIndex()`/
  `setCursorTrack(int)` (originally added to feed Launchpad's own track
  assignment) are the single source of truth - `current_cursor.track`
  stays a `PatternEditor` field, just also settable from outside
  (`PatternMatrix`'s Enter commit). No new storage needed.
  `LaunchpadManager` never needed its own copy either -
  `UI::renderComponents()` computes the track index it hands to
  `launchpad_manager_->refresh()` fresh every frame from
  `pattern_editor_->getCursorTrackIndex()`. `LaunchpadManager::
  assignedTrackIndex()`/`advanceTrack()`'s per-device "detach and follow
  your own track" capability is untouched and still available in ordinary
  `NOTES` mode (irrelevant in `GridMode::OVERVIEW`, where "which track" has
  no meaning at all).
- **No track selected**: `UI::renderComponents()` passes `-1` instead of
  `pattern_editor_->getCursorTrackIndex()` to Launchpad whenever
  `active_element_.lock() == pattern_matrix_`, the same frame every
  connected device switches to `GridMode::OVERVIEW` (see "Launchpad
  integration" below).
- **Focus-gated highlighting**: neither `PatternMatrix`'s cursor-cell
  highlight nor `PatternEditor`'s region/selection highlight and
  active-column header brightening draw while that widget isn't
  `active_element_`. Both `render()` overloads take a `bool focused`,
  threaded into the actual color decisions (`PatternMatrix`'s
  `is_cursor_cell`; `PatternEditor::renderRow()`'s selection booleans;
  `PatternEditor::renderHeading()`'s `segment_color()`) and into each
  `render()`'s own dirty-check, so a pure focus change forces an
  immediate redraw in both directions. `UI::renderComponents()` computes
  `active_element_.lock()` once and passes the right bool to each. The
  playhead-row tint is untouched by this - transport state, not input
  focus, stays visible regardless of focus.
- **Enter commits the selection**: on a `PatternMatrix` cell, looks up the
  `track_id` in `Song::getRootTrackIds()` (not `PatternMatrix`'s own
  filtered index space), calls `pattern_editor_->setCursorTrack()`, jumps
  the real playhead via `Controller::setEditPosition(scene_idx *
  song.getPatternLength())` (there is no "displayed scene" independent of
  the playhead in this codebase), and moves `active_element_` to
  `pattern_editor_`. `PatternMatrix` only calls a callback UI supplies
  (`setCommitCallback()`) - it has no idea any of this exists, the same
  separation `getCursorTrackIndex()`/`setCursorTrack()` already established
  for Launchpad. The whole commit refuses while playing (all-or-nothing,
  matching `PatternEditor`'s own `move-row-up`/`move-row-down` guard) -
  not a split commit that moves the track but not the playhead. Known
  edge case, not handled: if `PatternEditor`'s own mark/selection is still
  active when Enter commits, `setEditPosition()` clamps to that
  selection's pattern instead of reaching the target scene.
- **Windows stay separate**: `PatternMatrix` and `PatternEditor` are two
  independently-focusable windows, not merged into one plane - the
  friction that might suggest merging was focus and track selection being
  the same thing by accident (each widget owning its own cursor), which
  shared state already resolves without a layout change. The two windows
  can be focused independently while always agreeing on which track/scene
  is current, the same way two Emacs windows on one buffer share its
  buffer-local state while each keeps its own point.
- **Boundary crossings**, routed through two UI methods
  (`requestOverviewFocus()`/`exitOverview()`) rather than four separate
  implementations:
  - `PatternEditor`: plain Left with the cursor already on the first
    track's first column enters the overview instead of doing nothing.
  - Launchpad: "prev-track" (CC93) already on track 0 does the same -
    `LaunchpadLayout::advanceTrackIndex()` clamps at 0 rather than
    wrapping, so "already at the first track" is a real, detectable edge.
  - Either entry point lands the overview's own cursor on its last
    (rightmost) column, not wherever it was left last time.
  - `PatternMatrix`: Right with the cursor already on the last column
    exits back to `PatternEditor`. Launchpad: "next-track" (CC94) while
    already in `GridMode::OVERVIEW` does the same (prev-track/CC93 there
    is a no-op, not a track-advance - "which track" has no meaning while
    every device is uniformly in OVERVIEW).
  - Both exits land on `PatternEditor`'s **first** track, always - a
    single, predictable starting point, not a mirror of whichever column
    was current in the overview.

## Launchpad integration

- `LaunchpadManager::GridMode::OVERVIEW`, forced onto every connected
  device simultaneously whenever `PatternMatrix` has focus - `refresh()`
  takes an `OverviewWindow` parameter (`active`, `track_ids`, `num_scenes`)
  and, per device, sets `state.grid_mode = GridMode::OVERVIEW` while
  `active`, or resets a device stuck there back to `NOTES` the moment it
  isn't - overriding whatever a device's own mode-toggle buttons last
  selected. One shared mode across every device, matching the "every
  Launchpad shows the same track" model above. `OverviewWindow::track_ids`
  is `PatternMatrix::getVisibleTrackIds()` (public) - the exact column
  list the terminal widget uses.
- **Scroll position is Launchpad's own**, not mirrored from
  `PatternMatrix`: `overview_scroll_row_`/`overview_scroll_col_`
  (`LaunchpadManager` members), reset to 0 whenever OVERVIEW isn't active.
  The two surfaces don't have the same number of visible rows (the
  terminal widget's ~4 data rows vs. the pad grid's 8), so tying them
  together dragged one around whenever the other scrolled for no reason.
  "move-row-up"/"move-row-down" (CC91/92) scroll `overview_scroll_row_`
  while a device is in OVERVIEW mode (clamped to the same "one virtual row
  past the last real Scene" bound the terminal cursor uses); outside
  OVERVIEW those command names aren't this class's at all, and fall
  through to `PatternEditor`'s own row navigation as usual. Column scroll
  has no dedicated button yet (CC93/94 are the enter/exit-overview
  gesture) and stays fixed at 0.
- y is flipped from `PatternMatrix`'s own top-down scene order: `y=0` is
  the bottom-left pad, so `scene_idx = overview_scroll_row_ + (7 - y)`
  keeps "reading order" top-to-bottom like the terminal grid.
- **LED color**: same hue/near-full-saturation identity the terminal grid
  uses (`getHue()` at 0.8 saturation), not `VisibleTrackInfo::getColor()`
  directly, for the same too-faint reason - but at its own, dimmer
  lightness (`fromHSL(hue, 0.8f, 0.3f)`, vs. the terminal's `0.45f`): a
  directly-emitted LED pixel reads brighter than the same lightness value
  does as terminal glyph text, so the two surfaces are tuned independently
  rather than sharing one constant. Off (fully dark) when a cell isn't
  populated or the column is a `DrumMachineTrack` - collapsing the
  terminal grid's finer three-way distinction, since there's no
  established red-✕-equivalent for an RGB pad.
- **Playing scene's row**: brightened via `Color::blend()` toward white,
  not a hue change - populated cells on that row blend further than
  their normal color; otherwise-off cells on that row rise from black to
  a low, uniform white blend (no track hue - nothing populated to
  represent) so the row reads as one continuous line.
- **Pad press** (`LaunchpadManager::handleOverviewPadEvent()`, dispatched
  from `UI::handleLaunchpadPadEvent()` ahead of the ordinary NOTES-mode
  path, the same way DRAW mode's own pad handling is special-cased) is
  the spatial equivalent of moving the terminal cursor there *and*
  pressing Enter at once - commits through `UI::commitOverviewCell()`,
  the same function `PatternMatrix`'s own Enter calls, not a second
  implementation. Refused past the last real/virtual row.

## Future: the full Matrix

Three things worth keeping from the retired `per-track-patterns-scenes-
matrix.md` plan, none needed by anything above:

**A referenced pattern pool.** `Song::patterns_` would become a flat,
id-addressable pool; `Scene` would hold `unordered_map<track_id,
pattern_id>` references instead of owning `Pattern` instances (today's
shape stays exactly as-is until/unless this happens). Needs usage
tracking (leaning toward Renoise's own answer - editing a pattern edits
it everywhere it's used, not copy-on-write) and an instrument-
compatibility check (tracks here are instrument-locked, unlike Renoise's
pattern numbers) - a `Pattern` would need to record which track/instrument
it was authored against, and cell assignment should refuse or warn on a
mismatch. `DrumMachineTrack` stays exempt even once it has real per-scene
content (see "Next phase" above). This MVP's `yank` sidesteps all of this
by always deep-copying.

**`Section` wiring.** `Section` (an ordered list of pattern/scene ids)
exists in name only - not wired into playback, load path an unfinished
stub. Wiring it as "a named, ordered group of Scenes" (verse/chorus/
bridge) is where the Matrix's rows could gain grouping beyond the plain
sequential list this MVP uses. Open fork, better decided with a concrete
Matrix UI in front of it: is a `Scene` the mandatory content of one
sequence position, or a reusable named preset droppable into several?

**Ableton-style live performance/Session mode.** Each track would get its
own live "currently playing pattern" pointer, independent of the
arrangement sequence - triggering a `Scene` retargets every track's
pointer at once; triggering one track is the same operation scoped to
one. Worth deciding deliberately: an empty cell in a triggered `Scene`
should leave that track's current pattern playing rather than stopping it
(matches Ableton; Renoise has no such state since every track's content
is always explicit) - the Matrix (explicit-everywhere) and a Session view
(empty-means-leave-alone) can legitimately want different semantics for
the same empty-cell representation.

Also carried over, ruled out for every part above, not just this MVP:
per-track/per-pattern variable row length or independent looping (every
`Pattern` keeps sharing one song-wide `pattern_length_`), and
tempo-relative live-trigger quantization ("launch on the next bar") - a
transport/timing feature layered on top of "which pattern is a track
playing," not a prerequisite for it.

## Explicitly out of scope

- Ableton-style clip launching/auditioning via Launchpad - OVERVIEW mode
  is select-and-commit only, not trigger-and-play (see "Future: the full
  Matrix" above).
- Multi-Launchpad tiling for extended real estate (more octaves in NOTES
  mode, a larger grid in OVERVIEW/drum-machine mode by spreading one
  logical view across several devices) - only sensible because every
  device now shares state, but its own future feature.
- The pattern pool / referenced patterns, `Section` wiring, Session mode
  (see "Future: the full Matrix" above).
- Cross-track cell assignment or instrument-compatibility checking -
  copy/paste always stays within one track's own column.
- Editing note content from `PatternMatrix` - overview + whole-cell
  copy/paste only, not a second pattern editor.
- Multi-cell rectangular copy - single cell only.

## Open questions

1. Exact `blend()` factor(s) for the playhead-row highlight, both surfaces
   (terminal `PatternMatrix` and Launchpad OVERVIEW LEDs) - the scheme is
   settled and implemented, only the numeric factors need eyeballing on
   real terminal/Launchpad hardware.
