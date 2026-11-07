# Tempo and time signature changes in the arrangement

## Context

The arrangement (`<arrangement>`, `ScoreSchema.h` type "arrangement") holds
per-track content only: instances, inline patterns, sample backgrounds.
The song has one time signature (`Song::getArrangementBars()` is a single
`BarGrid` from row 0) and one starting tempo; tempo changes inside the
arrangement are only `ZTxx` commands. A launched scene's tempo/signature
reaches the arrangement nowhere: it is a *running* override on the audio
thread (`RunningBars`, `SongState::pending_scene_`) that Back to Arrangement
drops. Recording a live take into the arrangement (a launch with Record Arm,
`ClipPlayer::assign`) writes the instances but loses the scene's tempo and
signature, so the recording plays back in the wrong meter. Locators are
document nodes too, but under the song root (`songschema::kLocatorsSlot`),
not under the arrangement.

## Model

Three new child slots of the arrangement node, each sorted by row, one
object per row per type:

| Type | Props | Slot |
|---|---|---|
| `tempo` | `row`, `bpm` (20-300) | `tempos` |
| `signature` | `row`, `numerator`, `denominator` | `signatures` |
| `locator` | `row`, `text` | `locators` (moved from the root) |

- The song's own tempo and signature stay as the values before the first
  object (the starting tempo `ZTxx` already leaves alone).
- `ArrangementView` gets `setTempo/clearTempo/getTempos`, the same for
  signatures; `Song::getLocators()/setLocator()` keep their API and forward
  to the arrangement. `Arrangement` (value type) carries the three lists so
  `PlaybackContent` and the clipboard see them.
- A signature object starts a new bar grid at its row (`BarGrid::origin`
  already supports a mid-song origin). A signature is only placed on a bar
  start of the grid in force; recording and the UI snap to it.
- `Song::getArrangementBars()` becomes `getArrangementBarsAt(row)`: the last
  signature object at or before `row`, else the song's. Every caller that
  assumes one grid (`ArrangementGrid`, `ArrangementPatternSource`,
  `PatternEditor`, copy-to-clip rounding, arrangement length) moves to it.
  Bar numbers count across grids (sum of whole bars before the origin).
- Row insert/delete in the arrangement shifts the three lists with the
  content (locators already move; check where).

## File

`<arrangement>` gains `<tempo row bpm/>`, `<signature row timeSignature/>`
and `<locator row>name</locator>` children; the top-level `<locators>` goes
away. No reader for the old form: convert the repo's songs (the ones with
`<locators>`) in the same change.

## Playback

- `PlaybackContent` carries sorted tempo and signature lists. `SongState`
  applies a tempo object when the row is reached and `rescanTempo()` includes
  them (last tempo object or `ZTxx` before the seek row wins; on one row the
  object applies first, then the command).
- `barsAt()` order: a launched scene's running signature (from its origin) if
  active, else the arrangement's piecewise grid. Back to Arrangement
  therefore lands on whatever the arrangement says at that row.
- Scene launch behaviour is unchanged (still a running override and still
  mirrored to `Song` by `mirrorSceneChange()`).

## Recording

In `ClipPlayer::assign` (arrangement assign with Record Arm), inside the
existing `Song::Edit`: when a launched scene has a tempo and/or signature that
differs from the one in force at the assign bar, write a tempo/signature
object at that bar row. A scene without them writes nothing. Open question:
when recording and Back to Arrangement is pressed, write nothing (the
arrangement was already the source) or the song's signature again.

## UI

- Arrangement grid / pattern editor: show a marker on the row of a tempo or
  signature change, draw bars from the piecewise grid.
- Commands (add to `CommandDescriptions.cpp`): `set-time-signature` and the
  tempo command act on the object at the arrangement cursor's bar in
  Arrangement view and on the song's starting value elsewhere; a clear
  command removes the object under the cursor. Undo comes free from the
  document.

## Docs

`time_signatures.md` (sections 2 and 4: the song's signature is the
starting one; changes live in the arrangement), `scenes.md` (recording),
`commands.md` (`ZTxx` vs tempo objects), `glossary.md` (arrangement object,
tempo change, signature change), `song-model.md`, `user-interface.md`.

## Order of work

1. Move locators under the arrangement (schema, `Song` forwarding, loader and
   saver, convert songs). No behaviour change; existing tests cover it.
2. Tempo/signature objects: schema, views, `Arrangement`, file, piecewise
   `BarGrid`, callers of `getArrangementBars()`.
3. Playback: `PlaybackContent`, `SongState` tempo/bars, seek rescan.
4. Recording in `ClipPlayer::assign`.
5. UI markers and commands, docs.

## Tests

Round trip of the three object kinds (save/open, undo/redo); piecewise bar
numbering and `roundUpToBar` across a change; offline render with a 3/4 to
4/4 change and a tempo change (sample position of a note after the change);
seek past a change recomputes tempo; scene-launch recording writes the
objects and a plain launch writes none; the Mesomedes demo's 12/8 bar becomes
a real `signature` object (drop the "three 2/4 bars" note in its song notes).
