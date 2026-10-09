# Undo/redo via a document model

Two PRs. PR 1 makes the song a **document** (no user-visible change). PR 2
adds undo on top of it. Decided with the user: the document covers everything
except instrument/effect internals; the audio thread moves to a compiled
snapshot in PR 1; the XML format may change (fixtures and `songs/` are
converted by a script).

## Why the current code can't just get an undo stack

- No chokepoint: ~25 `Controller` methods mutate the model, ~110 call sites in
  `PatternEditor`/`LaunchpadManager`/`ClipGrid`/`ArrangementGrid`/`OutlineView`/
  `MidiNoteInput`/`ClipPlayer` mutate it directly.
- `incVersion()`/`incMinorVersion()` are inconsistent for the same edit
  (Launchpad take = major, MIDI take = minor) and bump *after* the write.
  Versions only go up, so "undo back to saved" can't read as clean.
- The audio thread reads the live `Song` (patterns, clips, arrangement, pool)
  unsynchronised; only the track tree has a mutex, and its snapshot copies raw
  `Track *`.
- ~35 classes own `loadParameters`/`storeParameters`; all XML I/O is in
  `Song.cpp`.

## Architecture (the five decisions)

1. **One node kind.** `Node` = `NodeId` + type tag + ordered property bag +
   ordered child slots. References are by `NodeId`. Score data is sparse: one
   node per note/command, with a `row` property, sorted by row.
2. **Four journaled primitives**: `setProperty`, `insertChild`, `removeChild`
   (detaches; node stays in the table), `moveChild`. Each records before and
   after into the open transaction. The journal is append-only, never
   truncated, and doesn't know which undo policy sits on top.
3. **Transactions are declared by the input layer** (key event, chord, command,
   take). They nest; the outermost commit appends one journal entry and fires
   one change notification carrying the changed `NodeId`s.
4. **Typed access via schema + views.** Property keys/types/XML names are
   declared once; `TrackView`, `PatternView`, … are stateless, short-lived
   wrappers. No mutable C++ mirror of document state anywhere.
5. **The audio thread reads only immutable compiled snapshots**, built on
   commit and handed over the event channel; displaced snapshots return to the
   UI thread for destruction.

### Property values

`Value = variant<monostate, int64, double, bool, string, NodeId>`. Unknown XML
attributes are kept as `string` values under their original name and written
back verbatim, so an older build doesn't destroy a newer file. Known
properties are parsed by schema on read and formatted by schema on write;
defaults are omitted on write.

### Instrument and effect trees

Their classes keep `loadParameters`/`storeParameters`. In the document each
instrument/effect element is a node whose property bag is exactly that
attribute set (a `ParameterSource` adapter over a node), so the node tree is
generic and undoable while the ~35 classes are untouched. The compiled
snapshot builds the existing instrument objects from those nodes (`prepare()`
etc.), so presets and `from=` resolution behave as today. Nothing edits these
in place at runtime today; when that arrives it becomes `setProperty`.

### Persisted ids

Persisted `id` on: song root (also persists the `NodeId` counter, restored
before children are parsed), tracks, clips, patterns that are referenced,
pool instruments. Not on notes/commands/locators/scenes: they are addressed by
parent + position, ids are allocated at load, and diffs stay quiet. Trade-off:
automation/modulation targeting an arbitrary note would need ids there too;
adding a persisted `id` to a node type is a schema change, not a redesign.

### Non-undoable state

Cursor, selection, scroll, clipboard, armed tracks, focus, recording-take
bookkeeping stay out of the document entirely (they live in `Controller`/
widgets as today). Mute/solo/monitor/sends/azimuth/collapsed *are* persisted
and so are undoable document properties.

### Change notification replaces `incVersion`

Commit publishes `{changed NodeIds, structural?}`. Dirty = journal position vs
saved position (so undo back to the saved state is clean). The partial-row
redraw gets a strictly better signal: the changed note nodes' `row` values.
`Song::getVersion()` survives as a thin derived counter until the last widget
stops polling it.

## Migration order (tree builds and plays at every step)

0. `src/doc/`: `Node`, `NodeId`, `Value`, `Schema`, `Document`, `Transaction`,
   `Journal`, node-tree XML reader/writer, debug diff verifier. New code, own
   tests; nothing consumes it yet.
1. `PlaybackSnapshot`: an immutable, compiled form of what `SongState` reads
   today, built from the *existing* typed `Song`; `SongState` stops touching
   `Song`/`Arrangement`/`Clip`/pool directly. Fixes the audio-thread race.
   `render` tests must stay bit-identical. From here on a migration step only
   changes the compiler's input.
2. Funnel every direct mutation through `Controller` transactions (still on
   the typed model). This is the audit work, done once; the no-chokepoint
   problem disappears before storage changes.
3. Move storage into the document in slices, each deleting the old class and
   replacing it with a view: song scalars/locators/scenes → patterns+notes →
   clips+arrangement → tracks/sends/pool/bus. After each slice: build,
   `ctest`, round-trip, render parity.
4. XML: node-tree writer/reader replace `Song::open`/`save`; fixtures and
   `songs/` converted by script; unknown attributes round-trip.
5. Debug-build verifier on in tests: every transaction is diffed against its
   recorded undo record.

## Event parity

An undone change must have the same audio-side effect as the original. The
snapshot compiler is the single place that diffs old and new snapshots and
emits the existing `PlaybackControlEvent`s (`SET_TRACK_*`, `SET_BUS_EFFECT`,
`CLEAR_VOICES` on instrument change). Original edits and undo both go through
it.

## Drum machine

Lanes were removed (the rack is fixed); steps are ordinary notes in clip
patterns, so they are covered by the note primitives. Verify with a test.

## PR 2: undo (sketch)

- **Policy layer** over the journal: Emacs (undo appends inverse
  transactions; chain flag; undoing an undo is redo) first; a Renoise-style
  cursor policy is a second implementation behind the same interface, chosen by
  a setting for the future GUI (Ctrl-Z/Ctrl-Y).
- **Keybindings**: `undo`, `redo` (cursor policy only) as named commands;
  Emacs binding is a two-key prefix (`C-x u`) plus `C-_`/`C-/`; nothing handles
  prefixes today beyond the `ESC`-then-`x` coalescer, so this needs a small
  prefix-keymap addition. Launchpad shift+Record Arm/Mute call the commands.
- **Selection of undo scope**: undo is per buffer (song); cursor is moved to
  the first changed node on undo.
- Live recording take = one transaction; sample audio is referenced by
  `shared_ptr` buffer, not copied into the journal.
- Journal trimming bound (memory) is the one place history is ever dropped.

## Open questions (guesses made; correct me)

1. Journal size cap (default: trim oldest beyond 10 000 transactions).
2. Snapshot granularity: per-pattern shared immutable data so one note edit
   recompiles one pattern, not the song. Measure before going finer.
3. Does a take recorded over a playing loop commit per-row (many undo steps)
   or once at take end (default: once).
