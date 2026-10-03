# Swing (MVP) and the Grooves -> Rhythms rename

Fixes the `docs/known_bugs.md` entry on the "Swing"/"Boogie" library
entries: swing becomes a real song-level setting applied at playback time,
and library entries carry a swing of their own.

Out of scope: per-clip grooves, a no-swing track flag, groove templates
(velocity/timing maps), swing on automation commands.

## Terms

- **Swing**: a timing feel. Notes are grouped in pairs (two eighths, or two
  sixteenths). Straight timing puts the second note of each pair exactly
  halfway; swing delays it, giving a long-short, lilting feel. The amount is
  the share of the pair taken by the first note: 50% is straight, about 67%
  is triplet swing (jazz, shuffle, boogie), 75% is very heavy. Only the
  *second* note of each pair moves; on-beat notes never do.
- **Groove**: the general word for a rhythmic feel, i.e. how a pattern's
  timing (and often accents/velocity) deviates from a rigid grid. Swing is
  the simplest groove. DAWs go further: Ableton's *Groove Pool* holds
  groove templates, per-position timing and velocity maps that you apply to
  clips, while Renoise's *Groove* panel has four per-line amounts that
  repeat every four lines and apply song-wide at playback, the model this
  plan's song-level swing follows. Here, "groove" means timing feel only, and for now that is just
  swing. Per-clip grooves and richer templates are out of scope.
- **Rhythm** (what this codebase currently calls a "Groove" in the Library):
  a pre-written drum pattern such as Waltz, Funk or Bossa Nova. It is
  *content* (which drum hits on which rows), where swing is *feel*
  (when those rows sound). The two are independent: the same rhythm can be
  straight or swung. Hence the rename below.
- **Row**: this engine's grid step, a sixteenth note (4 rows per beat). A
  swing "pair" is 4 rows for eighth-note swing or 2 rows for sixteenth-note
  swing.

## 0. Naming (do first, as its own mechanical commit)

"Groove" collides with Ableton's Groove Pool, which is a timing/velocity
template applied to existing clips. Our library entries are pre-written
drum patterns (what arranger keyboards call rhythms/styles). Rename the
library entries to **Rhythms** and reserve "groove"/"swing" for timing feel.

- `GroovePatternLibrary.{h,cpp}` -> `RhythmPatternLibrary`;
  `GroovePatternTemplate`/`GroovePatternHit` -> `RhythmPattern*`;
  `findGroovePattern()` -> `findRhythmPattern()`.
- `PlaybackControlEvent::PREVIEW_GROOVE` -> `PREVIEW_RHYTHM`;
  `Player` `preview_groove_*` -> `preview_rhythm_*`.
- `OutlineView`: `LIBRARY_GROOVE` -> `LIBRARY_RHYTHM`, group heading
  "Grooves" -> "Rhythms", `addSelectedLibraryGrooveToSong()` ->
  `...RhythmToSong()`.
- Tests (`GroovePatternLibraryTests.cpp`, `PlayerMultiBufferTests.cpp`),
  CLAUDE.md, `docs/known_bugs.md`, `plans/outline-library-followups.md`.
- Leave `songs/welcome.xml` alone: its "groove" is just a clip name.

## 1. Model

- `Song`: `swing` (percent of a pair given to its first note, 50 = straight,
  clamped 50..75, default 50) and `swing_rows` (the pair length: 4 = eighth
  notes, 2 = sixteenths, default 4). Both are `<song swing="" swingRows="">`
  attributes, read/written beside `tempo`/`rowsPerBar` in `Song.cpp`. Omitted
  attributes load as straight, so old songs are unchanged.
- Setters bump the major version (`Song::getMajorVersion()`), as the audio
  thread already keys on that.
- Shared helper, e.g. `swingOffsetRows(int row, int swing, int swing_rows)`:
  `0` unless `row % swing_rows == swing_rows / 2`, else
  `(swing / 100.0 - 0.5) * swing_rows` rows. At 67% on the eighth grid that
  is 0.67 of a row. The max (75%, 4 rows) is exactly 1 row.

## 2. Engine

- `SongState`: cache swing/swing_rows next to `song_structure_version_`
  (refresh in the existing `getMajorVersion()` branch of `renderBlock()`),
  so a swing edit lands within a block, with no new event type.
- In the note loop (`SongState.h`, the `delay_samples` line): add the swing
  offset to the note's own delay before converting to samples. This also
  moves note-offs, which share the path, so lengths are preserved.
- Parity key: the transport row (`row_idx`) for arrangement playback and
  `session_clock_` for taken-over/launched clips, never the clip-local row.
  Clips launch on bar boundaries so both agree, and an odd-length clip keeps
  swinging against the bar, not against its own start.
- Check that `RenderContext` pending events tolerate a frame beyond the next
  row: swing (<= 1 row) plus an explicit note delay (<= 1 row) can reach 2.
  If not, extend it (the old max was 1 row).
- Not swung in the MVP: sample-track clip starts (bar-aligned anyway),
  effect/command automation, live-played notes (they land where played).
  The Launchpad step-grid audition clock is a follow-up.

## 3. Library entries carry swing

- `RhythmPattern` gains `swing` and `swing_rows` (default 50 / 4).
  A swung entry's length must be a multiple of `swing_rows` (test it).
- **Swing**: rewrite the ride onto the straight grid, `"X...x.x.x...x.x."`
  (beats 1-4 plus the "and" of 2 and 4), swing 67 on the eighth grid. The
  current `"X..x.X..x.X..x.."` is the hand-spaced approximation.
- **Boogie**: pattern data already is straight eighths; just set swing 67.
- **Jazz Waltz**: its description says "swung" but the data is straight;
  swing 67 (12 rows = 3 pairs, fits).
- Everything else stays straight. "Slow Rock"/"Shuffle Blues" already use
  real 12/8 triplet rows and must not also be swung. Funk/16 Beat could
  take a light 16th swing later; not now.
- The outline description for a swung entry shows its swing ("swing 67%").

## 4. Using and previewing a rhythm

- **Preview** (`Player::renderPreview()`): add the swing offset, from the
  template's own swing, to `hit_frame` (`row * interval + offset * interval`).
  Preview never reads the song's swing, so it sounds the same in any song.
  Hits are still started at the top of the block that contains them (as
  today), so swing is as accurate as the preview already is.
- **Add to Song** (`addSelectedLibraryRhythmToSong()`): a swung entry sets
  the song's swing/swing_rows to its own and says so on the status line
  ("Swing set to 67%"); a straight entry leaves the song's swing alone, so
  adding a straight rhythm never silently removes swing set earlier.
  (Decision to confirm: see questions below.)

## 5. Setting swing by hand

- Commands in `UI::initializeCommands()` (backend-neutral, M-x reachable):
  `swing-increase` / `swing-decrease` (1% steps within 50..75) and
  `toggle-swing-grid` (eighths/sixteenths). No keybinding needed for MVP.
- Show the swing next to the tempo in `InfoLine.h` when it isn't 50.

## 6. Launchpad gesture: shift + Pan (CC79) = Swing

Shift (CC91 held) already relabels the eight right-side buttons; Volume
(Duplicate) and Solo (Draw) are taken and the other six do nothing, so one of
those is free to claim.

- `LaunchpadManager::handleRawButton()` shift branch: CC79 cycles the song
  swing through presets 50 -> 54 -> 58 -> 62 -> 67 -> 71 -> 75 -> 50, one per
  press, in every `GridMode`, through the same `Song` setter the commands use
  (so terminal and device always agree). Status line reports the value.
- LED, shown only while shift is held (like Duplicate/Draw): dark for 50,
  amber growing brighter with the amount. The other free buttons stay dark.
- Eighth/sixteenth grid is not on the device in the MVP (command only).
- Update CLAUDE.md's Extra-button layout bullet (shift list) and
  `tools/e2e/README.md`.
- e2e: `tools/e2e/verify_launchpad_swing.py`, modelled on
  `verify_launchpad_shift_highlight.py`: hold CC91, tap CC79 repeatedly,
  confirm the InfoLine swing readout steps through the presets, wraps back to
  50, and that the LED lights only while shift is down.

## 7. Tests and docs

- Unit: `swingOffsetRows()` (parity, 50 = 0, 75 = 1 row, 2 vs 4 rows);
  Song XML round trip + old files load straight; clamping.
- Render (`tests/RenderTests.cpp` style): a fixture with eighth notes at
  swing 67 puts the off-eighth about 2/3 of a row late and the on-beat
  unmoved; swing 50 output is bit-identical to today's; a note-off keeps its
  note's length.
- Library: every swung entry's length is a multiple of its `swing_rows`;
  Swing/Boogie/Jazz Waltz are swung, all others straight.
- Preview: with a swung entry, hits on off rows start later than the same
  row's straight time; a straight entry is unchanged.
- Docs: remove the known_bugs entry; CLAUDE.md (naming, swing in the Song
  model/engine, the Launchpad gesture); `docs/commands.md` untouched.

## Order of work

1. Rename commit (no behavior change, tests green).
2. Model + engine + unit/render tests.
3. Library data + preview + Add to Song.
4. Commands + InfoLine.
5. Launchpad gesture + e2e + docs, and delete this file.

## Questions

1. Add to Song with a swung rhythm: overwrite the song's swing (proposed), or
   only when the song is still straight?
2. Is the swing-grid choice (eighths/sixteenths) worth it in the MVP, or
   should it be eighths only?
3. Is CC79 (Pan) the right button for the gesture, or would you rather a
   different one of the six free ones?
