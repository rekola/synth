# Drum machine: per-scene patterns

Three phases: two prerequisite fixes to clipboard state that predate
this work entirely (Phase -1), a general `Pattern` capability this
drum-machine work turns out to need (Phase 0), then the drum-machine
change itself, now a thin consumer of both (Phase 1).

## Phase -1: clipboard safety - cross-tuning paste, and pasting across songs

Both bugs below are pre-existing, not introduced by this drum-machine
work - they live in copy/paste clipboard state (`PatternMatrix`'s
`cell_clipboard_`, `PatternEditor`'s `ClipboardEntry`) that Phase 0/1
build further on top of, so they're worth fixing first rather than
inheriting into new code.

### Cross-tuning paste

A `Note::getValue()` means something completely different depending on
tuning: a GM percussion key for `Tuning::PERCUSSION` (`PercussionTrack`,
and after Phase 1, `DrumMachineTrack`), a scale-degree in whatever
temperament for everything else - and that "whatever temperament" part
matters here too: `Tuning` has a distinct value per EDO (`TET12`/`TET19`/
`TET31`/`TET53`), not just one generic "pitched" bucket. Nothing in
either clipboard today checks this - copying a cell from one track and
pasting it onto a track with a different tuning (or, per "Pasting
across songs" below, a different *song* using a different temperament)
silently reinterprets the same raw integers as a different kind of value
entirely.

**`PatternMatrix`**: its clipboard remembers the tuning the copy came
from alongside the `Pattern` itself (`Tuning cell_clipboard_tuning_`,
next to the existing `cell_clipboard_`/`cell_clipboard_track_id_`). `yank`
checks the destination column's own tuning against it; a mismatch
refuses the paste (a clear log message, e.g. "Cannot paste: incompatible
tuning") and leaves the clipboard untouched, so a wrong-track paste
attempt doesn't cost you the copy.

**`PatternEditor`**'s own clipboard (`ClipboardEntry`/`kill-region`/
`kill-ring-save`/`yank`) gets the same fix, shaped for its row×track
rectangle instead of a single cell: `ClipboardEntry` gains
`std::vector<Tuning> track_tunings` - one entry per track actually
captured (`NOTE_COLUMN`/`COMMAND`: exactly one, for that single
`track_id`; `TRACK`/`EVERYTHING`: one per track in `[track_lo, track_hi]`,
parallel to `PatternBlock`'s own track-offset dimension; `ANNOTATION`:
none, it isn't track-scoped at all). `yank` checks every track-offset
it's actually about to write (after whatever `pastePatternBlock()`'s own
clipping already limits it to) against the recorded tuning at that same
offset - any mismatch refuses the *whole* paste, not a partial one that
silently drops just the incompatible columns.

This is a different failure mode than `transposePatternBlock()`'s own
existing `is_percussion` handling, deliberately: transpose *skips*
percussion tracks within a range and still transposes the rest (a range
operation acting in place - skipping what doesn't apply is the natural
behavior), while a paste either lands as a whole or not at all (bringing
in data from elsewhere - a silent partial success would leave it unclear
what actually arrived). Not an inconsistency, two different operations
calling for different failure shapes.

"This track's tuning" (`PERCUSSION` for `PERCUSSION_CONTROL`/
`DRUM_MACHINE`, the song's own tuning otherwise) is already computed in
at least two places (`Song.cpp`'s `<pattern>` reader and writer) and now
needs a third and fourth (`PatternMatrix`, `PatternEditor`) - worth
factoring into one shared helper, e.g. `Song::getTuningForTrack(const
Track &)`, instead of a fourth copy of the same three-way check.

### Pasting across songs

This app supports several songs open at once, Emacs-buffer-style
(`Controller::openSong()`/`cycleBuffer()`/`switchToBuffer()`,
`UI.cpp`'s own `open-song`/`next-buffer`/`select-named-buffer`
commands) - and every `SongObject` (`Track` included) draws its
`internal_id_` from one global, monotonically-increasing counter shared
across *every* open `Song`, not a per-song one (`SongObject::next_id`).
So a track's id from song A can never coincidentally collide with
anything in song B - which is exactly why `PatternMatrix::yank()`'s
existing guard, `if (!song.getTrackByInternalId(cell_clipboard_track_id_))
return;`, currently refuses to paste at all the moment you've switched to
a different song since the copy: it was written to catch "the
originating track was deleted since the copy" (a real, same-song
concern - see its own comment), but "the track's id doesn't exist in the
current song" is indistinguishable from that check's point of view
whether the track was genuinely deleted or you're simply looking at an
entirely different song now.

Fix: remember which song a copy came from too -
`int cell_clipboard_song_id_` (`Song::getInternalId()`, free since `Song`
already extends `SongObject`) alongside `cell_clipboard_track_id_`. At
`yank` time:
- Same song as the copy (`song.getInternalId() == cell_clipboard_song_id_`):
  today's exact behavior - paste back into the remembered track_id,
  refusing if that specific track was genuinely deleted meanwhile.
- A different song: "paste back into the same track" is no longer a
  coherent instruction (no such track exists there) - fall back to the
  cursor's own current column instead, the same target `PatternEditor`'s
  own paste already always resolves to.

The cross-tuning check above still applies regardless of same-song or
cross-song - a percussion cell still can't land on a pitched column,
whichever song it came from; comparing the *full* `Tuning` value (not
just a percussion/pitched split) is what makes this the same mechanism
that also blocks pasting between two songs written in different
temperaments, with no separate check needed for that case.

`PatternEditor`'s own clipboard needs no equivalent fix here - its
`yank` already always targets `current_cursor.track` (an index into
whichever song is currently active), never a remembered track_id, so
switching songs was never mechanically blocked there to begin with.

### Testing

Implemented and passing:
- `Song::getTuningForTrack()` unit tests (`tests/SongTests.cpp`, one per
  track type).
- `tools/e2e/verify_patterneditor_cross_tuning_paste.py` -
  `PatternEditor`'s own clipboard: a same-tuning paste still works, a
  cross-tuning paste is refused and leaves the clipboard intact for a
  later, compatible paste.
- `tools/e2e/verify_cross_tuning_paste.py` - `PatternMatrix`: a same-song
  yank still always lands back on the track it came from regardless of
  the cursor (unaffected by this phase); opening a second song
  (`cross_tuning_paste_test_song_b.xml`) as a new buffer and yanking
  there exercises both the cross-song fallback and the tuning refusal
  together, confirmed against the old silent-no-op failure mode.

## Phase 0: pattern length (general, not drum-specific)

### Context

A `Pattern` has a length. Nothing today makes that explicit - every
`Pattern` implicitly spans the song's own global `pattern_length_`,
because nothing shorter was ever possible - but once a `Pattern` can be
given its own, shorter length, repetition isn't a separate feature to
design: it's simply what reading past a pattern shorter than the span
it's played across already means. A 16-row bassline `Pattern` in a 64-row
song just plays 4 times, the same way it always would if you'd copy-pasted
it by hand - there's no "loop mode" to turn on.

This is not the same thing `plans/pattern-matrix.md`'s "Future" section
already ruled out ("per-track/per-pattern variable row length or
independent looping") - that ruling is about a *scene's own length*
varying per track, which would desync tracks against each other. This
keeps every track's `Pattern` spanning the exact same absolute row range
as every other track in that scene; only the *content* can be shorter and
tile within it. That doc's wording gets corrected once this lands, so it
doesn't read as silently contradicted.

`DrumMachineTrack::loop_length_` (today's own bespoke, track-global
version of exactly this idea) is retired in Phase 1 below in favor of
this - a drum loop becomes nothing more than an ordinary `Pattern` with
its own length, which also makes it strictly more flexible than the
original draft of this plan: length can now vary **per scene**, not be
one fixed value for the whole track.

### Data model

`Pattern` gains `int length_ = 0` plus `getLength()`/`setLength(int)`. `0`
isn't a "looping is off" flag - it means this particular `Pattern` was
never given a length of its own, so it takes on whatever length the
context reading it provides (in practice, always the song's own
`pattern_length_` today), the same implicit default every `Pattern`
already has right now. Giving it a shorter length explicitly is the only
thing that changes.

```cpp
int getEffectiveRow(int row, int context_length) const {
  auto len = length_ > 0 ? length_ : context_length;
  return len > 0 ? row % len : row;
}
```

Deliberately resolved at read/write time against a caller-supplied
`context_length`, not baked into the `Pattern` at creation time - a
`Pattern` that was never given its own length should keep tracking the
song's `pattern_length_` live if that ever changes (`Song::
setPatternLength()`), the same way it implicitly does today; baking a
snapshot of it in at creation time would silently desync the moment the
song's own length changed afterward.

No divisibility requirement between `length_` and `context_length` -
`row % length_` is well-defined either way. A 5-row pattern inside a
64-row scene just plays twelve full repeats plus one partial one (rows
0-3), cut off wherever the scene ends - the same thing that would happen
if you stopped a real loop mid-cycle, not a case that needs rounding or
rejecting.

Polyrhythms fall out of this for free, not as a separate feature: every
track's own `Pattern` wraps independently against the same shared,
absolute `row_idx` clock the whole scene plays against - nothing
synchronizes one track's loop boundary to another's. A 5-row pattern on
one track against a 7-row pattern on another just drift in and out of
phase, realigning every 35 rows, exactly like a genuine 5-against-7
polyrhythm would. `PatternEditor`'s own dimming (see "UI" below) is
already per-track/per-column for the same reason - there's no scene-wide
"everyone shares one loop point" assumption anywhere in this design.

`getNotes(row)`/`getNote(row, col)`/`getCommand(row)` and the write
accessors (`setNote`/`setNotes`/`pushNote`/`deleteNote`/`setCommand`) all
take this same effective row instead of a raw one - callers compute it
once via `pattern.getEffectiveRow(row, song.getPatternLength())` before
reading or writing (`SongState.h`'s playback loop, `PatternEditor`'s
rendering, `PatternBlockOps.cpp`'s copy/paste each need this one extra
call, not a signature change to `Pattern` itself). Writes go through the
same remap as reads - editing row 20 of a 16-row pattern actually mutates
row 4's entry, and every repeat of it updates at once. This isn't just
the friendlier UX (matches how a loop conceptually works) - it's the only
option that can't produce a dead write: without it, writing to a literal
out-of-range row creates data nothing ever reads back, since every read
already resolves through the same modulo.

`insertRow()`/`deleteRow()` (whole-row shift, e.g. `insert-row`/`kill-row`)
operate on the pattern's own raw row range, not through the remap -
shifting rows around inside a shorter pattern's real content is just
normal editing of that content; `length_` itself is untouched by either.

Scope of the remap: only the four content accessors above, via their
caller-computed effective row. `getLength()`/`setLength()` and the raw
`notes_`/`commands_` maps (e.g. `Song.cpp`'s XML writer, which needs the
pattern's own real, un-duplicated rows) stay untouched - the writer emits
the pattern's real rows once, `length_` as one attribute, and loading it
back re-establishes the identical repeat structure rather than requiring
every repeated row to be written out literally.

### XML format

`<pattern track="N" length="16">` - new, optional attribute; absent or
`0` is today's exact behavior (tracks the song's own pattern length, no
repeat) for perfect backward compatibility. `<note>` children are only
ever the pattern's own real `0..length-1` rows.

### UI (`PatternEditor`)

- Rows at or past the current track's own pattern length (in the scene
  currently shown) render dimmed - a straightforward per-row check in
  `render()`, not a new selection/highlighting concept.
- Editing at a dimmed row (typing a note, kill/yank, ...) transparently
  redirects to the real row, since that's already true just by going
  through `Pattern`'s own accessors - `PatternEditor` doesn't need its own
  awareness of the remap for editing, only for the dimming itself.
- Setting/changing a pattern's own length has no UI hook yet - deferred,
  see "Explicitly out of scope." `setLength()` is reachable
  programmatically (tests, XML) from day one regardless.
- Whether to also draw a divider at the loop boundary, beyond plain
  dimming, is minor polish, not blocking.

### Implementation notes

`Pattern`'s own accessors keep their existing raw-row signatures
unchanged, exactly as designed above - every caller resolves the
effective row itself first. `Scene::getEffectiveRow(track_id, row,
context_length)` is the convenience most callers actually use (they hold
a `track_id`, not already a `Pattern &`): falls back to `row` unchanged
for a track with no `Pattern` here yet, otherwise delegates to that
`Pattern`'s own `getEffectiveRow()`. Wired through every real read/write
site, not just playback:
- `SongState.h`'s playback loop (resolved once per track per row).
- `PatternEditor.cpp`: every note/command/velocity/delay entry site
  (raw keyboard and MIDI), `renderRow()` (both the read *and* the dimming
  of rows past a track's own length - `bg`/`fg` blended toward black
  per-track, since two tracks in the same row can have different lengths,
  unlike `is_neighboring_pattern`'s own whole-row dim).
- `Controller.cpp`'s auto-record subsystem (`ensureRowCleared()`,
  `writeReleaseOff()`) - shared by `PatternEditor`'s and
  `LaunchpadManager`'s own live-take recording paths.
  `ensureRowCleared()`'s own `cleared_rows` dedup set is keyed by the
  *effective* row, not the raw one - two raw rows repeating the same
  underlying content must dedup together, or sweeping past the second one
  during a live take would clear (and lose) a note the first one just
  wrote, one loop iteration earlier in the same take.
- `LaunchpadManager.cpp`'s own pad-press note entry.
- `PatternBlockOps.cpp`'s whole family (`copy`/`clear`/`transpose`/
  `paste`, both the whole-track and note-column-scoped siblings) - each
  gained a `context_length` parameter (the paste family already had one,
  `num_rows`, doing double duty); `kill-region`/`kill-ring-save`/`yank`/
  `kill-row`/`transpose-region-up`/`-down` in `PatternEditor.cpp` all pass
  `song.getPatternLength()` through. A selection or paste range straddling
  a shortened pattern's own length boundary reads/writes the real,
  repeating content at each row independently - not a contiguous range in
  the underlying storage, but each row's own effective target is
  resolved on its own, so the block's row-offset sequencing (source row i
  -> destination row i) still lines up correctly regardless.

### Testing

- `Pattern`/`Scene` unit tests (`tests/SceneTests.cpp`):
  `getEffectiveRow()` wraparound, no-divisibility-required wraparound,
  write-redirect correctness (writing row 20 reads back from both row 4
  and row 20, and from another repeat further out), XML round-trip with
  `length` set and unset, `Scene::getEffectiveRow()`'s own fallback for
  an unknown track.
- `tests/PatternBlockOpsTests.cpp`: copy and paste each reading/writing a
  repeated row through a track's own shorter length.
- `tests/RenderTests.cpp`
  (`pattern_shorter_than_song_repeats.xml`/
  `render_pattern_shorter_than_song_repeats`): a track's own 4-row
  `<pattern length="4">` (note on at row 0, off at row 2) in an 8-row song
  actually repeats at rows 4-7, confirmed via `windowedRms()` across all
  four quarter-pattern intervals - not just "does it not crash."

## Phase 1: drum machine content becomes per-scene patterns

### Context

Today a `DrumMachineTrack`'s step data (`steps_`, an `unordered_map<int,
uint8_t>` keyed by GM note) is track-global: one loop plays identically in
every `Scene`, computed purely from `(pattern_row % loop_length_)` with no
scene awareness at all (`DrumMachineTrack::getHitNotesForRow()`,
`SongState.h`'s own drum-machine block). This is why every
`DrumMachineTrack` column in `PatternMatrix` (`plans/pattern-matrix.md`)
is permanently the not-applicable `✕` glyph - there's no per-(scene,
track) content to show or copy/paste.

This phase replaces the single global loop with a real per-scene pattern
per track - not a new bespoke step/lane data type, but the *same*
`Pattern` every other track already uses (Phase 0's own `length_` already
supplies the repeat, so there's nothing drum-specific left to build for
that part at all). A GM note number is already a raw `Note::getValue()`
for a percussion note, so a "step" is nothing more than a `Note` at a
given row. This resolves the fork `plans/pattern-matrix.md` left open the
same way the earlier draft of this plan did: an empty scene means silence
for that track, the same convention a regular track's absent `Pattern`
already has - not an Ableton-style "keep playing whatever was already
going."

### What moves per-scene vs. what stays on the track

`DrumMachineTrack` keeps everything that describes the **kit** - which
drums this track can play, not what triggers when:
- `lane_notes_`/`addLane()`/`removeLane()`/`clearAllLanes()`/
  `seedDefaultKit()`/`hasLane()`/`getLaneNotes()` - the lane list, always
  `DrumRankTable`-ordered.

`sequence_id_`/`getSequenceId()`/`setSequenceId()` are removed outright,
not carried forward inert. They existed only to let a future "reusable
named sequence" feature reference a drum groove by id without a later
format break - but once `Pattern` content is per-scene (this phase) and
`plans/pattern-matrix.md`'s own deferred referenced-pattern-pool work
eventually lands, that capability arrives for free, generically, for
every track type, not as a drum-specific mechanism. Keeping a
drum-only placeholder for it now is dead weight for something the
general mechanism will already cover.

`loop_length_` is retired from `DrumMachineTrack` entirely - it's now just
Phase 0's `Pattern::length_`, read from whichever scene's pattern is
currently in play. This means loop length is no longer one fixed value
for the whole track - two different scenes can genuinely have
different-length drum loops for the same track.

`steps_` is retired outright, not replaced with a per-scene sibling type.
A `DrumMachineTrack`'s step content becomes an ordinary `Pattern` living
in `Scene::patterns_by_track_id_[track_id]` - the exact same map, and the
exact same `Pattern` type, every other track already uses. `Scene` and
`Song::getOrCreateScene()` need **no changes at all** for this phase - the
write path a drum-step edit needs already exists.

### A step is a `Note`

A lane hit at a given step is just a `Note` at that row, `getValue()` = the
GM note number and `Tuning::PERCUSSION` - exactly what a percussion note
already looks like elsewhere in this codebase. Several simultaneous lane
hits on one step are just several note-columns on that row, the same
chord mechanism every other track already has - no bitmask, no
lane-index-to-column mapping.

This is also why the symbolic-identity question resolves for free: a
`PERCUSSION`-tuned `Note` already round-trips through `Note::toString()`/
`Note::stringToKey()`'s existing GM-percussion mnemonic table (`Note.h`'s
`percussion_names` - "BD", "SD", "CH", ... one per GM key 27-82) rather
than a bare integer, the exact mechanism `PercussionTrack` XML already
uses. A `<note>` under a `DrumMachineTrack`'s `<pattern>` gets this
automatically once `Song.cpp`'s two tuning-resolution sites (the `<pattern>`
reader and writer, both currently `track->getType() ==
TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning()`)
recognize `DRUM_MACHINE` alongside `PERCUSSION_CONTROL` - no new
mnemonic table, no new parsing code. The numeric GM value stays the
underlying identity (nothing about custom-instrument-per-lane is solved
here - that's real future work, and would need its own decoupling of
lane identity from GM note number if it ever lands), but nothing in this
phase - XML, the step-grid, `PatternMatrix` - ever has to look at that
number directly instead of its mnemonic.

Editing a step (toggling it on/off) means finding whether the target row
already has a note-column whose value equals the target GM note - a
linear scan, trivially cheap at ≤8 lanes - then deleting that column
(`Pattern::deleteNote()`) or pushing a new one (`Pattern::pushNote()`).
Unlike the retired bitmask, *which* column index a given lane's note ends
up at is not stable across rows (`Pattern` assigns columns independently
per row, first-empty-slot) - fine for storage/editing, since identity is
the note's value, not its column index, but this matters for playback -
see below.

### Playback (`SongState.h`)

Stays a dedicated small drum-machine block, not folded into the general
per-track note loop above it in the same function: that loop's
`addPendingEvent()` call uses each note's column index as the "column"
key `retriggerVoices()`/`chokeExclusiveClasses()` use for voice identity,
which needs to be stable and unique per lane (`DrumMachineTrack.h`'s own
existing rationale) - and a `Pattern`'s column index for a given lane's
note isn't stable across rows (previous section). So this block keeps
using the GM note value itself as that key, exactly like today, rather
than the note's incidental column index within its row.

The block now reads `scene.getPatternsByTrack()`'s entry for this
track_id (nothing/no hits if the track has no `Pattern` in this scene,
exactly like a regular track with nothing recorded there) and scans it
for defined notes at Phase 0's own effective row, collecting each one's
`.getValue()` - but still filtered to `lane_notes_`, exactly like today's
`hasLane()`-gated `setStep()`/`setSteps()`: a note present in the row
whose value isn't a currently-configured lane never fires. This isn't
just preserved for its own sake - it's what keeps a cross-track paste
(see "`PatternMatrix` integration" below) non-destructive: pasting in
content that happens to name a GM note this kit hasn't added a lane for
leaves it sitting inertly in the `Pattern`, silent until a matching lane
exists, rather than either erroring or auto-creating a lane no one asked
for. A small shared helper - `DrumMachineTrack::getHitNotesForRow(const
Pattern & pattern, int pattern_row, int context_length) const` (an
ordinary member again, not static, since it now needs `lane_notes_`) -
resolves `pattern.getEffectiveRow(pattern_row, context_length)`, scans
that row once, and keeps only notes matching a lane. Reused by
`SongState.h` (passing `song.getPatternLength()` as `context_length`,
exactly like every other track's own read path), the Launchpad
step-grid's own "was this step already hit" toggle check, and the
free-running audition clock (`LaunchpadManager.cpp`'s
`triggerAuditionStep()`).

### XML format

Because a scene's drum content is now an ordinary `Pattern`, it's written
and read exactly like any other track's content - a `<pattern
track="N" length="8">` inside `<scene>` (Phase 0's own attribute),
with plain `<note>` children, no new element at all for this part.
`<drumMachineTrack>` keeps only kit data:

```xml
<drumMachineTrack id="0">
  <lane note="BD"/>
  <lane note="SD"/>
  ...
</drumMachineTrack>
```

- lanes list which drums exist, `note` the same GM-percussion mnemonic
  (`Note::keyToString()`/`stringToKey()`, `Tuning::PERCUSSION`) as a
  `<note>` element's `value` - nothing about steps or loop length lives
  here any more.

This is still a hard format break, not a dual-read migration - the old
`<drumMachine>`/`<lane note steps="...">` shape goes away entirely.
Migration, for every file below: for each old lane's step string, write
the equivalent `<note row="R" value="BD" velocity="default">`-style
entries (mnemonic, not the raw GM number the old `note="36"` attribute
used) into a `<pattern track="T" length="8">` (the old `loop_length`
attribute moves onto `<pattern>`, renamed) in *every* scene the song has (the old
single loop played identically in all of them, so this preserves current
playback exactly). Files: `songs/haze_demo2.xml`,
`songs/scaletest_7limit_simple.xml`, `tests/fixtures/
drum_machine_track.xml`, `drum_machine_track_no_sequence_element.xml`,
`drum_machine_track_32rows.xml`, `drum_machine_track_20rows.xml`,
`drum_machine_track_explicit_empty_sequence.xml`,
`drum_machine_two_patterns.xml`, `drum_machine_retrigger.xml`,
`tools/e2e/drum_machine_stepgrid_test.xml`.

### `PatternEditor` integration

Today a `DrumMachineTrack` column renders as a plain placeholder in
`PatternEditor` (a fixed-width run of `x`/blank per row, `renderRow()`'s
own `track->getType() == TrackType::SAMPLE || ... DRUM_MACHINE` branch)
and refuses typed note entry outright (the same type check, in
`offerInput()`'s raw-key handling) - both written back when a
`DrumMachineTrack` had no per-scene `Pattern` content of its own to show
or write into at all. Once this phase lands, that's no longer true - a
`DrumMachineTrack` column should render and accept entry exactly like a
`PERCUSSION_CONTROL` one (real note/velocity/delay/command display, real
typed entry), not stay a placeholder:

- `renderRow()`'s placeholder branch keeps only `SAMPLE` (which still has
  no per-row `Pattern` content) - `DRUM_MACHINE` falls through to the
  ordinary `NOTE`/`VELOCITY`/`DELAY`/`EFFECT` rendering below, same as
  every pitched/percussion track.
- `offerInput()`'s type-exclusion guard drops `DRUM_MACHINE` for the same
  reason - typing now writes through normally.
- Both of `renderRow()`'s/`offerInput()`'s own inline `track->getType() ==
  TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning()`
  checks (display formatting, MIDI-note resolution) become
  `song.getTuningForTrack(*track)` calls - two more sites of the same
  duplicated check Phase -1 already consolidated elsewhere, now including
  `DRUM_MACHINE` for free.

Whatever the step-grid/Launchpad editing conventions turn out to be
(note-per-lane, chord support - see "A step is a `Note`" above),
`PatternEditor` shows and accepts exactly that same content - one
`Pattern`, one rendering path, no drum-specific special case left.

### `PatternMatrix` integration

Glyph logic (🗏/🗌/blank) needs no special-casing any more - a
`DrumMachineTrack` column is, from that point of view, just another
track's `Pattern`. The `DRUM_MACHINE` early-return in `render()`'s
glyph-selection and the `✕`/`error_fg_color` branch are simply deleted,
not replaced with anything.

Copy/paste itself needs no new work here - Phase -1's cross-tuning fix
already covers this column type the same as any other (`PercussionTrack`
<-> `DrumMachineTrack` paste is fine, same value semantics; either <->
a pitched track is refused), and an unconfigured lane on the destination
from a `PercussionTrack` paste just leaves that note inert per the
`lane_notes_` filter above, non-destructive.

### Removing a lane

`DrumMachineTrack::removeLane(note)` already deletes that note's step data
outright, no confirmation, no undo. With steps now living in every
scene's own `Pattern` instead of one track-global map, this becomes
"delete every note-column with this value from every scene's `Pattern`
for this track" - needs a `Song &` to reach every scene, where it
previously only needed `this`. A small `Pattern`-level helper (something
like `deleteNotesWithValue(int value)`) is the natural place for the
per-`Pattern` half of this; exact placement/name to settle while
implementing.

### Testing

- `tests/`: the moved `getHitNotesForRow(pattern, row, context_length)`
  helper's own unit tests (a couple of length/row-index combinations
  including a seek past the first loop iteration, and a row with several
  simultaneous lane hits), plus a `removeLane()` test confirming the
  fan-out clears that note's columns from more than one scene at once.
- `tests/RenderTests.cpp`: a fixture with different step content (and
  different `length`) in two different scenes, confirming playback
  actually differs between them - today's fixtures only ever exercise one
  loop played across however many scenes.
- Every fixture/e2e file listed under "XML format" moves to the new
  shape (`drum_machine_two_patterns.xml` needs to actually author two
  *different* patterns to keep testing anything meaningful, now that its
  old name's meaning - two Scenes, one shared loop - no longer applies).
- `tools/e2e/verify_launchpad_stepseq.py` (existing) needs a pass to
  confirm step edits land in the current scene, not track-globally.

## Phase 2: step-sequencer rendering in `PatternEditor`

### Context

Phase 1 makes a `DrumMachineTrack` column render/accept entry exactly
like a `PERCUSSION_CONTROL` one - real `NOTE`/`VELOCITY`/`DELAY`/`EFFECT`
columns, one full-width triplet per voice. That's the right *data* model,
but the wrong *display* for a track whose voices are a fixed, known set
of up to `DrumMachineTrack::kMaxLanes` (8) lanes rather than an open-ended
chord: 8 full `NOTE VEL DEL` triplets side by side is far wider than a
terminal column has any business being. `DrumMachineTrack` gets its own
compact rendering instead - a real step-sequencer view, one narrow cell
per lane rather than one wide triplet per voice.

### Rendering

- One cell per lane (`lane_notes_` order, same as the Launchpad grid),
  not one triplet - velocity and delay are not shown at all, only
  whether that lane is hit at this row (matches the Launchpad step-grid's
  own display, which already ignores velocity/delay - `DrumMachineTrack.h`'s
  own history).
- The cell's own content is the same GM-percussion mnemonic every other
  display of this data already uses (`Note::toString(Tuning::PERCUSSION)`,
  Phase 1's own "A step is a `Note`" section) - not a new glyph/symbol
  convention. Same width and layout as an ordinary `NOTE` column already
  has (3-character mnemonic, space-padded, plus the usual trailing
  separator space `renderRow()`'s per-column loop already draws) - a hit
  cell is literally what a `NOTE` column already renders for that note,
  just with a cyan background instead of the plain one, so a lane at rest
  is simply blank (no mnemonic shown at all, matching a `NOTE` column's
  own "nothing defined here" blank).
- The per-row effect/command column stays, same position and width as
  today (`renderRow()`'s existing `EFFECT` column) - a `DrumMachineTrack`
  can still carry per-row `Command` data (pattern break, azimuth slide) on
  the exact same `Pattern` as any other track, and losing the ability to
  see/enter it would be a real functional regression, not just cosmetic
  narrowing.
- `PercussionTrack` keeps its current, regular (wide) rendering -
  step-sequencer mode is keyed off having a fixed, ordered lane list
  (`DrumMachineTrack::getLaneNotes()`) to lay narrow columns out against;
  `PercussionTrack` has no such list (free-form, unbounded multi-voice
  entry), so there's no fixed column set to compact against. Revisit only
  if `PercussionTrack` ever grows its own kit/lane concept.

### Editing

In scope for this phase, not deferred to a later one: typing/toggling a
step directly in `PatternEditor` (not just via the Launchpad) - Phase 1
already allows typed note entry into a `DrumMachineTrack` column
structurally, so this phase wires the compact per-lane cells up to it
rather than leaving them display-only.

## Phase 3: Launchpad session/launch view - shared, triggerable patterns

Speculative and genuinely unsettled - realizes two ideas
`plans/pattern-matrix.md`'s own "Future: the full Matrix" section already
named but deferred (a referenced pattern pool, and Ableton-style Session
mode), now given concrete shape. Recorded here because it's the natural
next step after Phase 2, not because the design below is finished.

### Context

Today's Launchpad `GridMode::OVERVIEW` mirrors the arrangement: rows are
scenes, columns are tracks, pressing a pad commits that (track, scene)
position (`UI::commitOverviewCell()`) - navigation, not performance. This
phase replaces it with a session/launch view: rows become a track's own
available *shared* patterns instead of scenes, and pressing a pad
*triggers* that pattern to start playing live for that track immediately,
independent of the arrangement's playhead - the same "audition, not
commit" flavor the drum step-grid's own free-running audition clock
already has (`audition_clock_`), generalized from one drum kit's steps to
any track's own pattern.

### Data model

- `Song` gains a flat, id-addressable pattern pool
  (`plans/pattern-matrix.md`'s own "referenced pattern pool" idea) - a
  shared `Pattern` lives once, referenced by id, not owned by any one
  `Scene`. This coexists with, not replaces, today's model: a `Scene`'s
  own `<pattern track="...">` content stays owned/inline exactly as it is
  (the everyday arrangement/`PatternMatrix` case); a track can *additionally*
  have any number of named/pooled patterns available to trigger live,
  unconnected to any specific scene position. Exact XML shape for the
  pool itself not yet drafted.
- Patterns enter the shared pool only via hand-edited XML for now - no
  in-app way yet to author one directly or to *promote* an existing
  scene's own owned `Pattern` into the pool. Revisit once there's a
  concrete need for either.
- Cross-tuning/cross-instrument safety: a pooled pattern is authored
  against one specific track's own tuning/kit, same concern
  `plans/pattern-matrix.md`'s own pattern-pool note already raised -
  triggering/assigning it against an incompatible track should reuse
  Phase -1's own cross-tuning refusal mechanism, not a second one.

### Launchpad session/launch view

- Rows: a track's own shared/pooled patterns. Columns: tracks, same
  layout as today's `OVERVIEW`.
- Pressing a pad triggers that pattern for the pressed track - starts
  playing live, looping at that pattern's own length (Phase 0's
  `Pattern::length_`), independent of the arrangement playhead and of any
  other track's own currently-triggered pattern.
- Each track needs its own runtime "currently triggered pattern" pointer -
  pure playback-session state, never persisted in the song file (mirrors
  Ableton's own per-track clip slot - description per the request that
  prompted this phase, not first-hand confirmed against real Ableton
  behavior).
- Empty-cell semantics: per `plans/pattern-matrix.md`'s own already-recorded
  call, triggering nothing new for a track should leave whatever it's
  already playing alone, not fall silent - Session view's own convention,
  distinct from the Matrix/arrangement's "explicit everywhere" one.
- Launches are quantized, not immediate: pressing a pad queues that
  pattern rather than starting it the instant the pad is pressed - it
  actually starts at the next quantization boundary, matching Ableton's
  own launch-quantization behavior. Exact boundary (the next bar? the
  currently-playing pattern's own loop end?) still to settle.

### Open questions

1. Exact relationship to today's `GridMode::OVERVIEW` - replaced outright
   (losing direct arrangement-navigation-via-Launchpad entirely), or a
   second, separate mode a device switches into alongside it?
2. XML shape for the pool - not yet drafted.

## Explicitly out of scope

- Session-mode "empty means keep playing" semantics - the design fork
  this phase resolves explicitly rejects that for the Matrix/arrangement
  view; a future live-performance Session mode
  (`plans/pattern-matrix.md`'s own "Future" section) could reasonably
  want different empty-cell semantics, decided then.
- Real decoupled lane identity (a lane routing to a custom instrument
  with no GM meaning at all) - this phase makes every GM percussion
  reference symbolic in the file format/UI, but the numeric GM value
  stays the underlying identity everywhere (`DrumRankTable`, the
  Launchpad picker, `InstrumentPool`'s kit resolution). A genuinely
  decoupled lane id is bigger than this plan and would need its own.
- Phase continuity across a scene boundary - today (and in this phase,
  unchanged) a repeating pattern's phase always restarts at row 0 the
  moment a new scene starts, because `SongState.h` feeds `getEffectiveRow()`
  a scene-relative row index. Keeping a repeating pattern's phase running
  continuously through a scene change instead would mean feeding it a
  monotonic "rows played since the transport last actually started/
  seeked" counter, with a real open question of whether *any* scene
  change resets it or only an explicit jump/seek. Deferred.
- Per-pattern end mode (stop vs. repeat) - `length_` always repeats
  (`row % length_`); there's no "play these rows once, then go silent"
  one-shot alternative. Mostly only useful for authoring a one-shot on a
  short, step-grid-style editing surface (the general case already gets
  "play once" for free - just don't set a short length and write notes
  directly at the rows you want). Would also reintroduce an explicit
  mode flag onto `Pattern`, working against "a pattern just has a
  length, looping is implicit" being simple by not needing one. Deferred.
- A UI hook for setting/changing a pattern's own length (Phase 0) - no
  command, keybinding, or effect mnemonic yet. `Pattern::setLength()`
  itself lands regardless (tests, XML round-trip); only the interactive
  way to reach it from `PatternEditor` is deferred.

## Open questions

None currently - the two live ones (cross-tuning `PatternMatrix` paste,
lane-filtered playback) are resolved above; the pattern-length UI hook is
deferred, see "Explicitly out of scope."
