# Swing (MVP) and the Grooves -> Rhythms rename

Fixes the `docs/known_bugs.md` entry on the "Swing"/"Boogie" library
entries: swing becomes a real song-level setting applied at playback time,
and library entries carry a swing of their own.

Also adds live tempo editing and Tempo/Swing views on the Launchpad (section
6), copying Novation's Launchpad Pro MK3 views.

Out of scope: per-clip grooves, a no-swing track flag, groove templates
(velocity/timing maps), swing on automation commands, negative swing.

## Terms

- **Swing**: a timing feel. Eighth notes are grouped in pairs. Straight timing puts the second note of each pair exactly
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
  swing "pair" is two eighth notes, 4 rows.

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

- `Song`: `swing` (percent of an eighth-note pair given to its first note,
  50 = straight, clamped 50..75, default 50), saved as `<song swing="">`
  beside `tempo`/`rowsPerBar` in `Song.cpp`. An omitted attribute loads as
  straight, so old songs are unchanged. Swing works on eighth-note pairs
  only (a pair is the constant `kSwingPairRows` = 4 rows); sixteenth-note
  swing is not needed by any library rhythm or by the device views, and can
  be added later as one more attribute.
- Setters bump the major version (`Song::getMajorVersion()`), as the audio
  thread already keys on that.
- Shared helper, e.g. `swingOffsetRows(int row, int swing)`: `0` unless
  `row % 4 == 2`, else `(swing / 100.0 - 0.5) * 4` rows. At 67% that is
  0.67 of a row. The max (75%) is exactly 1 row.

## 2. Engine

- `SongState`: cache swing next to `song_structure_version_`
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

- `RhythmPattern` gains `swing` (default 50). A swung entry's length must
  be a multiple of 4 rows (test it).
- **Swing**: rewrite the ride onto the straight grid, `"X...x.x.x...x.x."`
  (beats 1-4 plus the "and" of 2 and 4), swing 67 on the eighth grid. The
  current `"X..x.X..x.X..x.."` is the hand-spaced approximation.
- **Boogie**: pattern data already is straight eighths; just set swing 67.
- **Jazz Waltz**: its description says "swung" but the data is straight;
  swing 67 (12 rows = 3 pairs, fits).
- Everything else stays straight. "Slow Rock"/"Shuffle Blues" already use
  real 12/8 triplet rows and must not also be swung. Funk/16 Beat could
  take a light 16th swing later, which would need the sixteenth-note
  option described in section 1; not now.
- The outline description for a swung entry shows its swing ("swing 67%").

## 4. Using and previewing a rhythm

- **Preview** (`Player::renderPreview()`): add the swing offset, from the
  template's own swing, to `hit_frame` (`row * interval + offset * interval`).
  Preview never reads the song's swing, so it sounds the same in any song.
  Hits are still started at the top of the block that contains them (as
  today), so swing is as accurate as the preview already is.
- **Add to Song** (`addSelectedLibraryRhythmToSong()`): a swung entry sets
  the song's swing to its own, overwriting whatever was there, and says so
  on the status line ("Swing set to 67%"). A straight entry leaves the song's
  swing alone, so adding a straight rhythm never silently removes swing set
  earlier.

## 5. Setting swing by hand

- Commands in `UI::initializeCommands()` (backend-neutral, M-x reachable):
  `swing-increase` / `swing-decrease` (1% steps within 50..75),
  and `tempo-increase` / `tempo-decrease` (1 bpm steps within 20..300).
  No keybinding needed for MVP.
  All go through `Controller::setSwing()` / `setTempo()` (section 6).
- Show the swing next to the tempo in `InfoLine.h` when it isn't 50.

## 6. Launchpad: Tempo and Swing views (Novation's Launchpad Pro MK3 views)

The behavior being copied is Novation's own, from the Launchpad Pro MK3
documentation (not Ableton Live's): hold shift and press a button to enter a
view. The **Tempo** view (blue/white) shows the current bpm as a number on
the pad grid. The **Swing** view (orange/white) shows the swing value the
same way. The middle digit is drawn in white and the other digits in the
view's color. The up and down arrow buttons on the left change the value,
and holding one cycles through values quickly. Per Novation, swing above 50
makes off-beat notes late and swing below 50 makes them early; the plan
keeps only the positive half (see Questions).

How it maps onto this project's buttons:

- **Entering.** Shift is CC91 (held). The Pro MK3's Device and Stop Clip
  buttons have no exact twin on the X/Mini, so use two of the six right-side
  shift-alternate buttons that do nothing today (the right column is
  otherwise Duplicate on Volume and Draw on Solo): **shift + Stop Clip
  (CC49) = Swing**, as on the Pro MK3, and **shift + Pan (CC79) = Tempo**
  (stand-in for Device; confirm in Questions). Their LEDs show orange
  (Swing) and blue (Tempo) while shift is held, like Duplicate and Draw do.
- **A view, not a hold.** Like DRAW, the two views are new `GridMode`
  members (`TEMPO`, `SWING`), per-device, reachable from any other
  `GridMode`, and part of the same exclusive group as Session/Note/Custom:
  pressing CC95/96/97 leaves the view, and shift + the other view's button
  switches straight to it. Repeating the entry gesture while already in the
  view leaves it (back to the mode shown before), as DRAW's gesture does.
  The value lives in the `Song`, so two connected devices in these views
  always agree.
- **Arrows.** CC91 and CC92 (move-row-up/down, printed as arrows) become
  +1/-1 while a view is showing, the same repurposing the step grid already
  does for CC91-94. A press steps once. Holding repeats after about 400 ms,
  every 100 ms (a timer in `LaunchpadManager`, the same polling the mixer
  hold-preview uses). The value clamps at its ends, with no wrap. Shift's own
  meaning is unaffected: the arrows are only repurposed while no shift
  combination is in flight.
- **Number on the grid.** The 8x8 pads can't fit three digits, so only the
  **middle digit** is drawn in full, in white, centered, and the other digits
  are drawn beside it in the view's color (blue for Tempo, orange for Swing),
  clipped by the grid edge. For example 120 shows a white 2 flanked by a blue
  1 and 0. This is the one detail not spelled out in the description, so
  treat the layout below as an assumption:
  - 3x5 pixel digits, one blank column between digits, the middle digit in
    columns 2-4 (the grid is vertically centered, rows 1-5).
  - For a number of n digits the middle digit is index `n / 2` (120 -> the 2;
    75 -> the 5, with the 7 beside it).
  - A pure function `renderNumber(value) -> 8x8 array of {off, side, middle}`
    in its own header, so it is unit-testable without a device, and the same
    function can later draw the number in the terminal.
- **Other buttons.** In either view every button with no meaning there goes
  dark (as in the step grid), except CC91/92 (lit white as the arrows) and
  the view's own entry button (lit in its color). Session Record keeps its
  record indicator, like the step grid.
- **Setting the value** goes through new `Controller::setTempo()` and
  `Controller::setSwing()` (clamp, write the `Song`, bump the version, tell the
  audio thread), the same ones the M-x commands use, so the terminal and
  every device always agree. Status line reports the new value.
- Update CLAUDE.md's Extra-button layout and GridMode bullets (the shift
  list, the new modes, the arrows) and `tools/e2e/README.md`.
- e2e: `tools/e2e/verify_launchpad_tempo_swing.py`, modelled on
  `verify_launchpad_shift_highlight.py`: shift + CC79 enters Tempo (entry LED
  blue, arrows lit white), CC92 then CC91 step the bpm down and up and the
  InfoLine readout follows, a held arrow repeats, shift + CC49 switches to
  Swing, CC95 leaves. Pad LEDs are checked against `renderNumber()`.

### 6a. Live tempo (prerequisite for the Tempo view)

The tempo is currently only read when a song loads: `SongState` copies it
(`tempo_`, `SongState.h` initialize) and nothing changes it afterward, so
the Tempo view needs a real live-tempo path.

- `PlaybackControlEvent::SET_TEMPO` (or reuse the song-version check the
  swing refresh uses) so the audio thread picks up the new bpm within one
  block. Only the row in flight keeps its old length.
- Audit every use of `tempo_` in `SongState.h` that is cached at init
  rather than read per use: `render_context_.setBpm(tempo_)`, the
  per-effect `setRowDuration()` call for bus effects (MultiTapDelay), the
  row duration at line 75, and `SampleTrackState`'s own use of the bpm.
  Each must be refreshed on a tempo change.
- Sample clips are stretched against the song tempo
  (`SampleContent::stretched_song_tempo_`); a clip already playing keeps
  its old stretch until retriggered, which is acceptable for the MVP and
  worth a status-line-free comment in the code.
- Range 20-300 bpm (three digits). `Song::setTempo()` clamps.

## 7. Tests and docs

- Unit: `swingOffsetRows()` (parity, 50 = 0, 75 = 1 row);
  Song XML round trip + old files load straight; clamping.
- Render (`tests/RenderTests.cpp` style): a fixture with eighth notes at
  swing 67 puts the off-eighth about 2/3 of a row late and the on-beat
  unmoved; swing 50 output is bit-identical to today's; a note-off keeps its
  note's length.
- Library: every swung entry's length is a multiple of 4 rows;
  Swing/Boogie/Jazz Waltz are swung, all others straight.
- Preview: with a swung entry, hits on off rows start later than the same
  row's straight time; a straight entry is unchanged.
- Docs: remove the known_bugs entry; CLAUDE.md (naming, swing in the Song
  model/engine, the Launchpad gesture); `docs/commands.md` untouched.

## Order of work

1. Rename commit (no behavior change, tests green).
2. Model + engine + unit/render tests.
3. Library data + preview + Add to Song.
4. Commands + InfoLine (swing first).
5. Live tempo path (6a) and tempo commands.
6. Launchpad: `renderNumber()` + Swing view, then Tempo view, + e2e + docs,
   and delete this file.

## Questions

Decided: Add to Song overwrites the song's swing; swing is eighth-note only.

1. The Pro MK3's Device button has no twin on the X/Mini. Is shift + Pan
   (CC79) acceptable for Tempo, or would you rather one of the other free
   buttons (Send A, Send B, Mute, Record Arm)?
2. Negative swing (off-beats early, as Novation's view allows) cannot be done
   by the row scheduler as it is: a note can be delayed within its row but
   not fired before the row is reached, so it would need one row of
   lookahead. The plan keeps swing at 50..75 and clamps there. Is that
   acceptable for the MVP?
3. Novation's description doesn't say how a two-digit number (swing 50..75)
   is laid out. Is "middle digit = index n/2" (so the units digit is the
   white one for two digits) right, or should swing be shown padded to three
   digits (050) so the tens digit is the white one?
