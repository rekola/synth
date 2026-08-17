# Expand the menu bar: more sections, a populated File menu

## Context

The menu bar is mechanically sound as of `3b5daf6` (input routing, z-order
visibility, and item activation all fixed and verified) but content-wise
it's still just decoration: one section (**File**), one item (**New**).
This plan is about actually populating it - more top-level sections, a full
File menu, and the command-registry work each item needs behind it.

### What "Emacs-flavored, tracker-capable" means here

Emacs's own pull-down menus are a *thin, optional shortcut* onto the exact
same named commands the keyboard and `M-x` already reach - never a separate
code path with its own logic. This codebase already leans this way
(`CommandRegistry`/`Keymap`/`UIElement::dispatchCommand()`, see CLAUDE.md's
"Keybinding dispatch is centralized" section) and this plan continues it:
every menu item thunks to an existing or newly-`commands_.define()`'d named
command, the same one a keybinding or `M-x` would invoke - never bespoke
logic living only behind a menu click. Renoise doesn't have a comparable
top menu bar to take a UX cue from here (its editing surface is toolbar/
keybinding-driven), so Emacs is the actual precedent for *how* the menu
should feel; the tracker-specific *content* (add track, add note column,
toggle mute...) comes from this app's own already-existing raw keybindings,
promoted to named commands where they aren't already.

### Survey: what's already a named command vs. raw-only

Reusable as-is (`commands_.define()` already exists):

- UI: `new-song`, `open-song`, `save-song`, `save-buffers-kill-terminal`,
  `toggle-playing`
- PatternEditor: `set-mark`, `kill-region`, `kill-ring-save`, `yank`,
  `keyboard-quit`, `transpose-region-up`/`-down`, `move-row-up`/`-down`,
  `toggle-mute`, `toggle-solo`, `add-note-column`, `remove-note-column`,
  `add-drum-machine-track`, `delete-track`
- Controller (`sendCommand()`'s own if/else chain, not `commands_`):
  `save-song`, `toggle-mixer-type`. (`add-filter` is also matched there but
  its body is empty - a stub, not a real command; omit it from the menu
  until it does something.)

`delete-track` and `open-song` (below) have both since been implemented,
ahead of the rest of this plan - see their own notes in the Track/File
tables below for what actually landed.

Ctrl+T (add a fresh `InstrumentTrack`), Ctrl+R (start recording), and
`Ins` (insert a row) have all since been promoted to named commands
(`add-instrument-track`/`add-sample-track`/`insert-row`) - see the Track
and Song sections below for each.

Ctrl+D ("duplicate track") remains a **literal no-op stub**, empty body
(`PatternEditor.cpp`, right after `delete-track`'s own real handling).
**Excluded from this plan entirely** - a menu item that visibly does
nothing on click is worse than no item at all. Implementing it for real
is separate, not-yet-scheduled work.

### A dispatch bug this plan must fix before any Controller-level item works

**Fixed.** `UI::offerInput()`'s menu-activation handling now calls
`getController().sendCommand(cmd)` - the same door `M-x` already goes
through (tries `Controller::sendCommand()`'s own chain first, falls back to
`UI::executeCommand()` via `Controller::setCommandFallback()`) - with
`setStatus("Invalid command")` on failure, matching `StatusLine`'s own
message for the same case. Any menu item mapped to a Controller-only
command (the Song menu's mixer toggle, below) is safe to add now.

### ncmenu's real display constraints (confirmed against the installed library)

- An item's `.shortcut` renders as a single bare trailing character with no
  modifier indication (confirmed: a `Ctrl+N` shortcut rendered as a plain
  `N`, not `^N` or `C-N`) - misleading for anything that isn't a plain
  unmodified key, which is most of this app's real bindings (`C-x C-s`,
  `C-S-Up`, ...). Given that, most items below leave `.shortcut` zeroed
  (no auto-rendered letter) and instead spell the real binding out in the
  `.desc` text itself, right-aligned in the item box the way Emacs's own
  pull-downs show a command's key next to its name - accurate beats terse.
  Section headers (`Alt+F`/`Alt+E`/...) keep real single-letter shortcuts;
  a bare `Alt+<letter>` has no modifier-ambiguity problem.
- `ncmenu_create()` deep-copies the `ncmenu_section`/`ncmenu_item` arrays
  and their strings at call time (confirmed: the current code already
  builds them as constructor-local stack arrays that go out of scope right
  after `ncmenu_create()` returns, and it keeps working correctly across
  the whole session) - a declarative table (below) doesn't need `static`
  storage duration, just to be alive for that one call.
- Item activation (a click on an item, or Enter while one's highlighted)
  is entirely this app's own responsibility, not ncmenu's - already
  handled generically by `TerminalMenu::activate()` since `3b5daf6`; every
  new item just needs an entry in its description->command-name table.

## Design

### One declarative table, not scattered structs + a side map

Replace the current pair of hand-written `ncmenu_item[]`/`ncmenu_section[]`
literals plus the separate `kItemCommands` map with a single source table
`TerminalMenu` builds both from at construction time. **Implemented as**
(`TerminalUI.cpp`'s `menuSpec()`/`MenuItemSpec`/`MenuSectionSpec` - `label`/
`binding` split rather than one pre-formatted `desc` string, so the
constructor computes each section's own right-alignment column from its
widest label automatically instead of every table entry hand-padding
itself):

```cpp
struct MenuItemSpec {
  const char * label;    // nullptr = separator
  const char * binding;  // human-readable keybinding, e.g. "C-x C-s"; "" = none
  const char * command;  // name passed to Controller::sendCommand()
};
struct MenuSectionSpec {
  const char * name;
  char mnemonic;          // Alt+<mnemonic> opens this section
  std::vector<MenuItemSpec> items;
};
```

`TerminalMenu`'s constructor walks this table once to build the flat
`ncmenu_item`/`ncmenu_section` C arrays `ncmenu_create()` wants (each
item's `.desc` is `label` plus computed padding plus `binding`, backed by
a `reserve()`d `desc_storage_` vector so no reallocation can invalidate a
short/SSO string's own buffer while still being filled), and separately
builds the `desc -> command` map `activate()` already uses, keyed by that
same padded string. Adding an item is one line in the table, not three
coordinated edits across two structures.

`ncmenu_item` already has a real separator primitive - a `NULL` `.desc`
renders a horizontal divider (confirmed in `notcurses.h`'s own struct
comment: "utf-8 menu item, NULL for horizontal separator"). A
`nullptr`-command `MenuItemSpec` (the `—` rows in the tables below) maps
straight to `{ .desc = nullptr, ... }` - no app-level divider hack needed,
and `activate()`'s lookup naturally never matches a null description.

### Sections

Five sections, each with a distinct, obvious `Alt+<letter>` mnemonic
(`F`/`E`/`T`/`S`/`H` - no collisions, unlike trying to give "Playback" and
"Pattern" both a natural single-letter mnemonic, which is why playback and
pattern-structural items share one **Song** section below rather than each
getting their own).

#### File (Alt+F)

| Item | Keybinding shown | Command | Status |
|---|---|---|---|
| New | `C-n` | `new-song` | exists |
| Open... | `C-x C-f` | `open-song` | **implemented** |
| Save | `C-x C-s` | `save-song` | exists |
| Save As... | `C-x C-w` | `save-song-as` | **new** |
| — | | | |
| Quit | `C-x C-c` | `save-buffers-kill-terminal` | exists |

`open-song` needed a filename prompt, which didn't exist anywhere in the
UI before (`save-song` always targets whatever filename the song was
opened/created with; there was no in-app "open a different file" at all,
only the CLI argument). Implemented, ahead of the rest of this plan:

- `StatusLine` gained a generic `showPrompt(prompt, on_submit,
  initial_text = "")`: opens the reader, and runs `on_submit(typed_text)`
  on Enter (Ctrl-G cancels without running it at all). `M-x` itself is now
  just this method's first caller (`StatusLine::showMx()`, a private
  one-line wrapper so all three M-x trigger paths share one prompt string
  and one failure message) rather than a hardcoded special case - simpler
  than the mode-enum design originally sketched here, and behaviorally
  identical for `M-x`. `open-song`'s command (`UI.cpp`) calls
  `showPrompt("Open: ", ...)`, and on submission calls
  `getController().openSong(text)`, reporting success/failure via
  `setStatus()`. No filename-argument/completion beyond that - a richer
  file-picker is still future work.
- **`save-song-as`/`Controller::saveSongAs()` are still not implemented** -
  only `Open...` landed so far. `saveSongAs(const std::string & filename)`
  should save to `filename` (`current_song->save(filename)`, same as
  `sendCommand("save-song")`'s existing body) *and* update
  `current_song_filename` to it, so a subsequent plain `save-song` targets
  the new name - standard "Save As" semantics, matching what changing
  `current_song_filename` already means for `openSong()`. The reader-based
  prompt this needs is the exact same `StatusLine::showPrompt()` `open-song`
  already uses.
- **Resolved**: `new-song`/`open-song` do warn before discarding unsaved
  work - `Controller::hasUnsavedChanges()` (compares `Song::getVersion()`
  against a baseline snapshotted at the last new/open/save) gates both
  behind a `UI::confirmDiscardThenRun()` "discard? (y/n)" `showPrompt()`
  step, skipped entirely when there's nothing to lose. Explicitly temporary
  - `hasUnsavedChanges()`'s own doc comment flags it for removal once the
  editor supports multiple open song buffers, at which point New/Open stop
  discarding anything at all. `save-song`/`save-song-as` need no such guard
  - they never discard anything.

#### Edit (Alt+E)

| Item | Keybinding shown | Command |
|---|---|---|
| Set Mark | `C-SPC` | `set-mark` |
| Kill Region | `C-w` | `kill-region` |
| Copy | `M-w` | `kill-ring-save` |
| Yank | `C-y` | `yank` |
| Cancel | `C-g` | `keyboard-quit` |
| — | | |
| Transpose Up | `C-S-Up` | `transpose-region-up` |
| Transpose Down | `C-S-Down` | `transpose-region-down` |

All seven already exist as named commands on `PatternEditor` - no new
command work, just table entries. Every one of them, though, only exists
in `PatternEditor`'s own `commands_` registry, which raises a real
question the current single File/`new-song` item never exercised (`new-
song` lives on `UI` itself, reachable regardless of what's focused):

**Menu clicks don't change `active_element_`.** If the status line (or,
once `HierarchyView` ever comes back, some other widget) happens to be
active when a user clicks "Kill Region", `active_element_`'s own registry
doesn't have it. **Resolved**: option (a) from this section's original two
candidates - `UI::executeCommand()` now tries `pattern_editor_` as a
fallback whenever the current `active_element_` doesn't own the command
(skipped when `active_element_` already *is* `pattern_editor_`, which is
the overwhelmingly common case), before finally falling back to `UI`'s own
`commands_`. Same precedent `UI::offerInput()`'s `BUTTON1` handling already
established for unclaimed clicks ("fall back to the pattern editor - the
default/main workspace"), now extended to command dispatch generally - not
menu-specific, so `M-x` and Launchpad-by-name dispatch get the same
fallback too. Option (b) (unconditionally stealing focus on any menu
click) was rejected specifically because of the StatusLine-reader-active
edge case noted below.

#### Track (Alt+T)

| Item | Keybinding shown | Command | Status |
|---|---|---|---|
| Add Instrument Track | `C-t` | `add-instrument-track` | **implemented** |
| Add Drum Machine Track | `C-S-D` | `add-drum-machine-track` | exists |
| Add Sample Track | `C-r` | `add-sample-track` | **implemented** |
| Delete Track | `C-S-T` | `delete-track` | **implemented** |
| — | | | |
| Toggle Mute | `\` | `toggle-mute` | exists |
| Toggle Solo | `C-\` | `toggle-solo` | exists |
| — | | | |
| Add Note Column | `C-S-Right` | `add-note-column` | exists |
| Remove Note Column | `C-S-Left` | `remove-note-column` | exists |

"Promoted" (done) meant lifting the raw `if (input.hasCtrl() && ...)` body
in `PatternEditor::offerInput()` into a `commands_.define()` lambda -
same pattern already used for `move-row-up`/`toggle-mute`/etc. - then
repointing a `keymap_.bind()` entry at the new name and deleting the old
raw branch, behavior-preserving rather than a rewrite.
`add-sample-track`'s raw body branched on "is the current track already a
`SampleTrack`, reuse it, else create one" - that branching logic moved
into the command body unchanged (self-contained now: it re-resolves
`current_track` itself rather than capturing whatever `offerInput()`'s own
local already had, since a `commands_.define()` lambda can be invoked from
contexts - M-x, the menu - that never ran that local computation at all).

`delete-track` is implemented for real now (`Song::removeTrack()`,
recursing into `Track::removeChildByInternalId()` for a nested/grouped
track; refuses to remove the last remaining root track, closing the
`docs/known_bugs.md` zero-root-tracks landmine for this one specific new
code path - not a retroactive fix for every other unchecked
`track_ids[...]` site that entry lists; clamps `new_cursor` afterward;
clears `Controller`'s `recording_track_id` if it pointed at the
now-gone track). Still no **Duplicate Track** item - that one's still a
literal no-op stub, unimplemented.

#### Song (Alt+S)

| Item | Keybinding shown | Command | Status |
|---|---|---|---|
| Play/Stop | `SPC` | `toggle-playing` | exists |
| — | | | |
| Toggle Binaural Mixer | *(none)* | `toggle-mixer-type` | exists (Controller-only) |

**Update**: `insert-row` was promoted to a real named command after all
(`PatternEditor.cpp`, plain `Ins`), and gained a new sibling, `kill-row`
(Emacs's own `C-k`/kill-line, clipboard-aware unlike `insert-row`'s own
destructive shift) - both outside this plan's original scope, triggered
by a separate conversation about repurposing Ctrl-K once M-x itself no
longer needed it as a fallback (notcurses 3.0.17 fixed the underlying
Esc-x/Alt-x bug `docs/known_bugs.md` used to document). Both are
whole-row: every track's notes and command, plus the row's annotation,
shift together (`Scene::insertRow()`/`deleteRow()`, no `track_id`
parameter) - matching how `C-k` itself acts on the whole line regardless
of any narrower selection, not scoped to a single track. Still not menu
items here, by choice, not oversight - whether this section wants them
(and whether that finally justifies giving Song its own top-level entry
rather than folding pattern-structural and transport actions together) is
a separate decision from "does the command exist."

Deliberately excludes `move-row-up`/`move-row-down` - those are plain
cursor navigation (Up/Down arrow while stopped) wearing a command-name
badge for Launchpad-button reuse; putting "Move Row Up" in a pull-down
menu is the equivalent of a text editor menu-item for "Move Cursor Left",
not a real menu-shaped action. This section holds pattern/song-structural
and transport actions only.

`toggle-mixer-type` is the concrete reason the dispatch-path fix (now
applied - see above) was required before this section could ship - it
only exists in `Controller::sendCommand()`'s chain.

#### Help (Alt+H)

| Item | Keybinding shown | Command | Status |
|---|---|---|---|
| About / Licenses | *(none)* | `show-licenses` | **new, small** |

`--licenses`' content is already embedded at build time
(`ThirdPartyLicenses.h.in` -> a generated header, see CLAUDE.md) purely
for the CLI flag path in `main.cpp`, printed to stdout before the UI even
starts. `show-licenses` reuses that exact same generated string at
runtime - some new UI is still needed to *display* it inside the running
app (stdout isn't visible once the notcurses full-screen UI owns the
terminal), likely a read-only scrollable text pane; the simplest first cut
is a full-screen `showReader()`-style overlay seeded with the licenses
text and closed on any keypress, reusing existing plumbing rather than a
new widget class. A full "Keybindings reference" item (rendering
`docs/commands.md`, or a curated cheat-sheet) is a reasonable Help-menu
companion but needs its own content pipeline (that doc isn't embedded into
the binary the way licenses are) - noted as a natural follow-up, not
committed to in this plan.

## Status

Everything in this plan is implemented and landed **except the Help
section** - File/Edit/Track/Song are all real, populated `TerminalMenu`
sections now, every item backed by a real command, verified against the
real notcurses library (multi-section/separator construction, label-
padding math, and click->command resolution all confirmed with a
standalone reproduction of the exact table/build logic below - a click on
a padded, keybinding-suffixed item like "Delete Track" and a plain
no-binding item like "Toggle Binaural Mixer" both resolved to the right
command name end to end).

- Dispatch-path fix (menu activation goes through
  `Controller::sendCommand()`, `setStatus("Invalid command")` on failure).
- Active-element fallback to `pattern_editor_` (`UI::executeCommand()`).
- `delete-track` (`Song::removeTrack()`, `Track::removeChildByInternalId()`,
  the `PatternEditor` command + `Ctrl+Shift+T` binding, old stub removed).
- `open-song` (`StatusLine::showPrompt()`, the `UI` command +
  `C-x C-f` binding) and `save-song-as`/`Controller::saveSongAs()`
  (`C-x C-w`, pre-filled with the current filename).
- Unsaved-changes confirmation for `new-song`/`open-song`
  (`Controller::hasUnsavedChanges()`, `UI::confirmDiscardThenRun()`).
- `add-instrument-track`/`add-sample-track` promoted from raw
  `offerInput()` branches to `commands_.define()` entries, old branches
  removed, same as `delete-track`.
- `TerminalMenu` rebuilt around the `MenuItemSpec`/`MenuSectionSpec`
  declarative table (`menuSpec()`, `TerminalUI.cpp`) - File/Edit/Track/Song
  exactly as specified above (Help omitted - see below), item shortcuts
  zeroed in favor of a right-aligned keybinding hint baked into each
  item's built `.desc` string, `activate()`'s lookup map built from the
  same table instead of a hand-maintained one.
- The previously-added `setStatus("menu: " + ...)` status-line noise
  (useful only for confirming activation *reached* something, back when
  nothing real happened yet) is gone now that every item actually does
  something and reports its own real status (`"Saved ..."`, `"Opened
  ..."`, `"Invalid command"`, ...) - `UIMenu::getSelected()` removed
  entirely along with its only caller.

**Help is not implemented** - deliberately deferred rather than rushed.
`show-licenses` needs a genuine scrollable read-only overlay (the license
text is far larger than one screen), which means a new modal-input-capture
concept `UI::offerInput()` doesn't have any equivalent of today - a
materially bigger and riskier piece of work than any single item in the
other four sections (all of which were "wire an existing or nearly-
existing command to a menu item"). Left as its own follow-up rather than
either skipped silently or built hastily.

## Files touched (remaining - Help only)

- A new small read-only overlay for `show-licenses` (exact shape TBD at
  implementation time) - likely a `UIElement` or a `showReader()`-style
  helper on `UIPlane`, not a new top-level class if avoidable. Needs
  `UI::offerInput()` (or `TerminalUI`'s own render loop) to know a
  full-screen overlay is showing and route all input to it until closed -
  the one piece of new plumbing every other item in this plan managed to
  avoid needing.
- `docs/commands.md` - not touched (still just pattern-effect commands,
  unrelated namespace).

## Verification

- `ctest --test-dir build` stays green throughout - no engine-level
  change, `synth_tests`/`RenderTests` shouldn't even notice this work.
  (Confirmed for every change so far.)
- The `menuSpec()`/construction logic (label-padding math, multi-section +
  separator `ncmenu_item`/`ncmenu_section` building, click->command
  resolution via `ncmenu_mouse_selected()`) was verified against the real
  installed notcurses library with a standalone reproduction of that exact
  code, independent of the running app (no audio device/song needed) -
  confirmed a click on a padded, keybinding-suffixed item ("Delete Track")
  and a plain no-binding item ("Toggle Binaural Mixer") both resolve to
  the correct command name.
- **Still needed - real-terminal manual pass**, since none of the above
  drives the actual running app: open each of the four sections
  (`Alt+<letter>` and a mouse click on the header, both already confirmed
  working paths in general), activate every item by both mouse click and
  arrow-navigation + Enter, confirm the same observable effect a direct
  keybinding/`M-x` invocation of that command already produces - no
  menu-only behavior anywhere. In particular `toggle-mixer-type` (Song
  menu) - the concrete case the dispatch-path fix exists for - and every
  item whose underlying command was only ever reachable via a raw
  keybinding before this plan (`add-instrument-track`, `add-sample-track`,
  `delete-track`).
- `open-song`/`save-song-as`: confirm a round-trip (open a different
  `songs/*.xml` file, edit, Save As under a new name, confirm both files'
  contents on disk match what the UI shows) and that a cancelled prompt
  (`C-g`) leaves the current song untouched. Also confirm the unsaved-
  changes prompt actually appears/is skipped correctly: edit a song, try
  New/Open (prompt appears, "n" leaves it untouched, "y" proceeds); save,
  then try New/Open again (no prompt, since `hasUnsavedChanges()` is back
  in sync with the saved baseline).

## Open questions - worth deciding before/while implementing, not resolved here

- Should `menu_->offerInput()` even be reachable while `StatusLine`'s
  reader is active? `UI::offerInput()`'s menu dispatch has no
  `isReaderActive()` guard at all (unlike the earlier `dispatchCommand()`
  call, which does) - probably fine in practice (a mouse click while a
  text reader is focused is already an unusual sequence), but now a live
  question rather than a hypothetical one: `open-song`'s unsaved-changes
  prompt and filename prompt both already exercise the reader for
  something other than `M-x`.
- Whether a separator (`NULL` `.desc`) is itself mouse/keyboard-navigable
  in a way that needs skipping explicitly (e.g. does arrow-navigation land
  on it, does `ncmenu_selected()` ever return NULL-but-still-"an item is
  highlighted"?) - confirm against the library during implementation.
