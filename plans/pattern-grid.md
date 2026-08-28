# Pattern grid: PatternMatrix, per-scene drum machine, Launchpad session view

Combines two completed plans (`pattern-matrix.md`, `drum-machine-per-scene-patterns.md`)
into one brief status record: what's implemented, and what each of them
left open. Superseded by this file - the originals are deleted.

## What we have

- **`PatternMatrix`** (`src/ui/PatternMatrix.h`/`.cpp`): an always-visible
  scenes×tracks grid in the scope row - four-state cell glyphs (sounding /
  has-content / empty / not-applicable for a `DrumMachineTrack`), scroll,
  a virtual "one past the last scene" row that only actually grows the
  song on paste, and single-cell copy/paste (`kill-region`/
  `kill-ring-save`/`yank`, always a deep copy - no shared/referenced
  patterns). Track/scene selection is one piece of state
  (`PatternEditor::getCursorTrackIndex()`/`setCursorTrack()`) shared by
  `PatternEditor`, `PatternMatrix`, and every connected Launchpad, with
  boundary-crossing navigation between the terminal widgets
  (`requestOverviewFocus()`/`exitOverview()`) on both keyboard and
  Launchpad edges.
- **`DrumMachineTrack` content is per-scene**, not track-global: a
  `DrumMachineTrack` column is a real `Pattern` like any other track's,
  step data included (a step is a `Note`), so it's copy/paste-able and
  drives `PatternMatrix`'s own cell glyphs correctly.
- **`PatternEditor`'s compact step-sequencer rendering** for
  `DrumMachineTrack`: one narrow cell per lane instead of a wide
  NOTE/VELOCITY/DELAY triplet, typeable/toggleable directly, not just via
  the Launchpad.
- **A shared pattern pool** (`Song::getPooledPatterns(track_id)`/
  `addPooledPattern()`, `std::unordered_map<int, std::vector<Pattern>>`) -
  reusable named `Pattern`s per track, outside any one scene position, in
  a new top-level `<patterns>` XML element.
- **The Launchpad session/launch view** (`GridMode::SESSION`): rows are a
  track's own pooled patterns, columns are the shared track cursor.
  Record Arm gates trigger-live vs. assign-into-the-current-scene, same as
  ordinary note entry. Launches and stops are quantized to the
  currently-playing pattern's own loop end; an unassigned pad queues a
  stop, releasing the track's voices through their natural `stopNote()`
  tail (`InstrumentTrackState::stopAllVoices()`); repressing the active
  pad, or a track whose pool fills every row, queues the same quantized
  stop instead of cutting immediately. CC95 (Session)/CC96 (Note)/CC97
  (Custom, `DRAW` mode) are a trio of exclusive per-device mode buttons,
  fully decoupled from terminal UI focus - `grid_mode` defaults to
  `SESSION`. Stop Clip (CC49) is a held modifier, not a plain press:
  Session view shows several tracks as columns with no visible "current"
  one to target, so holding it and pressing any pad in a column stops
  that column's own track. The drum machine's own configuration (tap =
  picker latch, hold = clear step data) moved to CC98 (reused from
  "Capture MIDI") to make room.
- **Defaults**: the app now opens on `PatternMatrix` (Session/overview)
  rather than straight into note entry, 31-EDO is the default tuning, and
  `songs/welcome.xml` opens automatically with no file given.
- e2e coverage: `tools/e2e/verify_launchpad_session.py`.

## What's missing

From `pattern-matrix.md`:

- Multi-cell rectangular copy/paste in `PatternMatrix` - single cell only.
- Editing note content from `PatternMatrix` itself - overview +
  whole-cell copy/paste only, not a second pattern editor.
- Cross-track cell assignment / instrument-compatibility checking on
  paste - deliberately refused, always same-column only.
- Copying a leaf track's own nested Effect automation alongside its cell
  - silently left behind today; no single owning cell to fold it into
    when the Effect is shared by more than one leaf track.
- `Section` wiring (a named, ordered group of scenes - verse/chorus/
  bridge) - exists in name only, load path an unfinished stub.
- Multi-Launchpad tiling for extended real estate (more octaves in NOTES
  mode, a larger grid in SESSION mode by spreading one logical view
  across several devices).
- Numeric LED/blend tuning for the playhead-row highlight on real
  hardware - scheme implemented, factors never eyeballed against a real
  device.

From `drum-machine-per-scene-patterns.md`:

- No in-app way to author a new pooled pattern or promote an existing
  scene's own `Pattern` into the pool - hand-edited XML only.
- Real decoupled lane identity for a drum lane (routing to a custom
  instrument with no GM meaning) - the numeric GM note is still the
  underlying identity everywhere (`DrumRankTable`, the Launchpad picker,
  kit resolution).
- Phase continuity across a scene boundary - a repeating triggered
  pattern always restarts at row 0 the moment a new scene starts.
- Per-pattern end mode - `Pattern::length_` always loops; no "play once,
  then go silent" alternative.
- A UI hook for setting/changing a pattern's own length from
  `PatternEditor` - `Pattern::setLength()` exists (tests, XML round-trip)
  but nothing interactive reaches it yet.
