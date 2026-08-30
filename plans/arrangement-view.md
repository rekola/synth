# Arrangement view: shared clips, variable-length scenes, clip-block rendering

Supersedes `pattern-grid.md` (deleted) - its own "what we have"/"what's
missing" record is folded in below, sorted into the phase it now belongs
to, or into "Future / uncertain" when it doesn't fit any phase cleanly (or
dropped entirely where a phase below now supersedes it outright).

## Context

Session view's live clip-triggering (already built - see "Current state"
below) and a proper arrangement - a visual, editable record of what
actually got played, in the same spirit as a DAW's own Arrangement View -
are the same underlying mechanism, not two separate features. Triggering
a clip with Record Arm on already writes it into the current scene;
"recording an arrangement" is just doing that while playback runs forward
through scenes, no new mechanism needed for that equivalence itself.
What's actually missing is a *view* of the result that reads as an
arrangement (variable-length, titled scenes; clips rendered as spanning
blocks) instead of today's `PatternMatrix` (uniform-length scene rows, one
undifferentiated cell per track) - and the data model underneath it,
which today has no notion of a clip as a distinct, shareable object at
all.

Phases A through D, in this order because the data model has to exist
before there's anything to draw; Phase E is a freestanding per-track clip
management surface, sequenced late deliberately (see its own section);
Phase F is additional/later too, sequenced after A but not blocking
B/C/D:

- **Phase A** - shared/referenced clips (data model).
- **Phase B** - variable-length, titled scenes (data model).
- **Phase C** - the arrangement grid itself (view), including removing
  today's `PatternMatrix`.
- **Phase D** - visual unification with the drum machine's own rendering.
- **Phase E** - a per-track clip viewer (rename/delete/loop-toggle).
- **Phase F** - nested Effect automation captured into clips too, not
  just a leaf track's own notes.

## Current state (already implemented, unaffected by any phase below unless noted)

- **`DrumMachineTrack` content is per-scene**, not track-global: a
  `DrumMachineTrack` column is a real `Pattern` like any other track's,
  step data included (a step is a `Note`).
- **`PatternEditor`'s compact step-sequencer rendering** for
  `DrumMachineTrack`: one narrow cell per lane instead of a wide
  NOTE/VELOCITY/DELAY triplet, typeable/toggleable directly, not just via
  the Launchpad. Phase D revisits this rendering's relationship to clip
  rendering, but keeps its own compact, no-velocity/no-delay shape either
  way - see Phase D below.
- **The Launchpad session/launch view** (`GridMode::SESSION`): rows are a
  track's own clips, columns are the shared track cursor. Record Arm
  gates trigger-live vs. assign-into-the-current-scene, same as ordinary
  note entry - this assign path *is* arrangement recording, per the
  Context above. Every launch/swap/stop, on any track, is quantized to
  one shared grid (`Song::getRowsPerBar()`, default 16 rows/bar - also
  drives a stronger bar-boundary highlight in `PatternEditor`, on top of
  its existing every-4-rows beat one) measured from a shared origin set
  the moment the first pattern anywhere launches into an otherwise silent
  session. `Clip::isLooping()` (default true) makes a clip a one-shot
  when false. CC95 (Session)/CC96 (Note)/CC97 (Custom, `DRAW` mode) are a
  trio of exclusive per-device mode buttons. Stop Clip (CC49) is a held
  modifier: holding it and pressing any pad in a column stops that
  column's own track. The drum machine's own configuration (tap = picker
  latch, hold = clear step data) is on CC98.
- **A real `Clip` object** (`src/model/Clip.h`, `Song::getClips(track_id)`/
  `addClip()`, `std::unordered_map<int, std::vector<Clip>>`) - reusable
  named clips per track, outside any one scene position, replacing what
  used to be a plain pattern pool (`Song::getPooledPatterns()`/
  `addPooledPattern()`, a flat `vector<Pattern>`). A `Clip` owns its own
  name/loop/length directly, rather than delegating to its leaf
  `Pattern` - which only ever carries note/command content, plus its own
  unrelated `length_` for the scene-inline/drum-machine tiling case (see
  `Pattern.h`). XML: a `<clips>` element groups `<trackClips
  track="...">` per track, each holding `<clip name="..." loop="..."
  length="...">` entries wrapping a `<pattern>` for note/command content.
  This is the data-model half of Phase A - already landed. What's still
  outstanding (the rest of Phase A, below): the background/instance layer
  model, `copy-to-clip`/`insert-clip`, bar alignment, and stable
  identity/usage tracking - a clip is still addressed by (track_id,
  vector position) only, same as the old pool.
- **Defaults**: the app opens on `PatternMatrix` (Session/overview) rather
  than straight into note entry, 31-EDO is the default tuning, and
  `songs/welcome.xml` opens automatically with no file given.
- e2e coverage: `tools/e2e/verify_launchpad_session.py`,
  `verify_launchpad_notecustom.py`, `verify_launchpad_stopclip.py` (the
  last hits a documented, pre-existing environmental issue in sandboxed
  test runs - see `docs/known_bugs.md`).

## Phase A: shared/referenced clips ("pooled pattern" renamed to "clip")

### The layer model

A track's arrangement is two layers, not one:

- **The background** - exactly what exists today (`Scene::
  patterns_by_track_id_`, one ordinary, independent `Pattern` per track
  per scene). Untouched by this phase - pre-existing song structure
  never becomes a clip, and plain note entry directly into a scene (no
  clip involved at all) stays exactly as it is today. This is
  deliberately still how an arrangement gets finished: small, one-off
  content that isn't worth reusing just gets typed straight into the
  background, same as always.
- **Clip instances**, layered on top. An instance is a placement (which
  clip, which track, which scene, which bar it starts at) - like a
  Photoshop layer, it fully *hides* whatever the background has
  underneath its own span, for that span's entire length, including any
  row within the clip that's empty/rest - an empty row inside a clip
  instance still hides the background there; it doesn't let it show
  through.

Every instance of the same clip is **live-linked**: editing the clip's
content through any one instance updates every instance immediately -
matching the reasoning that already drove the drum machine's own
per-scene pattern design (a beat fixed in one place needs to actually be
fixed everywhere it recurs, not just where you happened to be editing).
This is a real, new capability - not just a rename - since today's pool
pattern assignment (`Song::getPooledPatterns()`/`scene.
setPatternForTrack()`) is always a one-shot independent copy, no link
back to the source.

**Resolved: no compile/flatten step.** Real (transport) playback plays
the layered background+instances model directly - `SongState::
renderBlock()`'s note scheduler has to actually know about instance
placements, not just read `Scene::patterns_by_track_id_` the way it does
today.

That's simplified down to a cheap check rather than real layer
compositing/priority logic, though: **placing an instance destructively
clears the background's own content for that exact span** (reusing
`Pattern`'s existing per-row clearing, the same mechanism `kill-region`
already uses), right when the instance is placed - not just visually
masked while the old content secretly lingers underneath. So by playback
time there's no ambiguity to resolve: the background is only ever
genuinely present where nothing covers it, and only ever genuinely empty
where something does. The scheduler's own job becomes "does an instance
cover this row - if so read the clip, otherwise read the background,"
never a real multi-layer stack.

**Instances can't overlap.** A track plays exactly one thing at a time -
never split across note columns with, say, column 1 playing clip 0 while
column 2 plays clip 1. Placing a new instance clears away whatever
portion of an already-placed instance it overlaps, the same way it
clears plain background content - but (same as editing never touches the
background it once cleared) this never modifies the *clip* being
overlapped, only that one placement's own visible extent on this track.

**Instance boundaries always stop, unconditionally.** An instance must
never leave a voice playing past its own end - proposed rule: the moment
an instance's own span ends, the scheduler fires the exact same natural
release already built for Session-view stops (`InstrumentTrackState::
stopAllVoices()`/`STOP_ALL_NOTES` - the instrument's own authored release
tail, not a hard cut), regardless of what follows (silence, background
content, or another instance starting immediately). This needs no special
case for "does something else start right away" - the ending instance's
own voice just fades out on its natural tail while whatever comes next
starts fresh on top, the same as any other stop already does. Also
redundant-safe against a clip whose own content already ends with an
explicit note-off (stopping an already-stopped voice is already a no-op
elsewhere in this codebase), so firing it unconditionally at every
instance boundary costs nothing extra.

**Continuous vs. one-shot instances - no resizing, ever; the tracker
idiom instead.** Placing *either* kind of instance follows one single
rule: it clears every track from the insertion point onward (through the
current scene's own end - never past it, per Phase C's "never crosses a
scene boundary"). What differs is only how far the content that follows
lets each kind actually play, mirroring how a held note already works in
a tracker - it keeps sounding until an explicit off or a new note
interrupts it, never a resizable block:

- A **one-shot** clip's instance needs nothing extra to bound it - it
  already has an intrinsic stopping point (its own native, whole-bar
  length), and the unconditional instance-boundary stop above already
  covers what happens when it's reached.
- A **continuous** (looping) clip's instance has no intrinsic stopping
  point at all - it just keeps reading `row % clip_length` (the exact
  same repeat mechanism `Pattern::getEffectiveRow()` already has)
  indefinitely, exactly like a held note, until something *extrinsic*
  bounds it: writing a note or an explicit off directly into the track at
  a later row, placing a new clip instance later on the same track
  (either one already covered by "instances can't overlap" above - the
  later content simply clears whatever continuous instance was still
  running through that point), or an explicit terminate-to-this-row
  gesture with no replacement content (candidate binding: Backspace/
  Delete, TBD) for ending it without starting anything new there.
- There is deliberately no independent "instance length" to drag-resize
  in either case - an earlier draft of this plan proposed letting a
  looping clip's instance be stretched to an explicitly chosen length,
  Photoshop-layer style; that idea is dropped in favor of the above,
  which needs no separate length field on a placement at all and matches
  this codebase's own tracker lineage (Emacs/step-sequencer, not a DAW's
  own block-resize-handle idiom) more directly.

### Authoring: `copy-to-clip` / `insert-clip`

Named after Emacs's own **register** commands (`copy-to-register`/
`insert-register`, `C-x r s`/`C-x r i`) rather than the kill-ring/`yank`
family - registers are Emacs's own mechanism for named, persistent,
repeatedly-retrievable stored content, which is exactly what a clip is
(unlike the kill ring, which is a single transient unnamed slot). Worth
actually trying `C-x r s`/`C-x r i` in real Emacs while designing these,
for a concrete feel of the two-step shape being mirrored here. Candidate
keybindings: analogous to Emacs's own `C-x r s`/`C-x r i` chords, exact
chord TBD (not yet reserved/implemented).

- **`copy-to-clip`** - saves the current selection as a new named clip.
  No placement at all - the direct keyboard equivalent of today's only
  authoring path (hand-editing the `<patterns>` XML), for building up a
  clip library without necessarily placing an instance yet.
- **`insert-clip`** - places an existing clip's content (a new,
  live-linked instance) at the cursor.
- "Paste as a new clip" (create + place in one step) is just
  `copy-to-clip` then `insert-clip` in sequence, the same way real Emacs
  registers don't offer a single fused "copy region to register and
  insert it too" command either - a convenience command chaining the two
  could still be added later, but the two primitives are the real
  interface.
- What happens to the *original* selection follows the ordinary cut/copy
  distinction, unchanged: `kill-region` (cut) removes it, `kill-ring-
  save`/plain copy leaves it exactly as it was - ordinary, independent
  background content, not automatically converted into an instance.
  `insert-clip`'s own instance can land anywhere the cursor is (a
  different track/scene/bar than the original selection) - it isn't
  "duplicate in place."
- **`copy-to-clip` always operates at whole-track scope**
  (`SelectionScope::TRACK`) - every note column plus the effect column,
  for whatever row range is marked - regardless of what's actually
  selected when it's invoked (a single note column out of several, or
  just the effect column alone): it widens automatically rather than
  refusing, the same precedent `kill-region` already has for the
  analogous case (a cursor on the effect column already widens the
  region to the whole row). A clip is a whole-track thing, never a
  sub-track (one note column) or multi-track selection.
- **Storage is forward-shaped for Phase F**: a clip's own content is
  keyed `track_id -> Pattern`, not a single bare `Pattern`, even though
  Phase A only ever populates the leaf track's own one entry - see Phase
  F below for why.

### Bar alignment

Every clip instance placement (`insert-clip`, and any future Arrangement-
view placement gesture) starts on a bar boundary, unconditionally - this
is what keeps Phase C's "one grid row is one bar" rendering and Session
view's own shared quantization meaningful. Two different positions are
actually in play, resolved two different ways:

- **Where an instance gets placed** (`insert-clip`'s destination): snaps
  the cursor's row *down* to the start of whatever bar it's currently in
  before placing - deterministic, no refusal/error needed, the same
  "always lands on the grid" behavior a real DAW's own clip placement
  already has.
- **Where a clip's own content was selected from** (`copy-to-clip`'s
  source): if the selection doesn't start exactly on a bar boundary
  either, using its own first row as the clip's own row 0 verbatim would
  shift every note's phase-within-a-bar the moment the clip is placed
  somewhere else - the desync this section's own name is about. Instead,
  the new clip's row 0 becomes the start of the bar the selection's first
  row falls in, and the selected notes land shifted forward by however far
  into that bar the selection actually started - i.e. the clip is
  automatically front-padded. Needs no explicit writes for the padding
  itself: `Pattern`'s existing sparse row storage already reads an absent
  row as rest, so padding is just an offset applied while copying, not
  real data. The clip's own total length is rounded up to the next whole
  bar the same implicit way (back-padded).

### Recording target (depends on Phase B)

Session-view recording (Record Arm assign) changes where it writes:
today it assigns into `session_.cursor_scene_idx` - `PatternMatrix`'s own
cursor position, independent of what's actually playing. Once this phase
exists, recording instead targets whichever scene is *actively playing*
at that moment (`PlaybackInfo::getPatternIndex()`), extending that
scene's own length as needed to fit - never confined to a fixed
pre-existing length. This is genuinely Phase B's own feature (extending
a scene's length presupposes a scene already has one of its own, which
today it doesn't - see Phase B below) even though it's a Session-view/
recording behavior change; sequenced after both A and B land, not
delivered by A alone.

### Other consequences

- **Supersedes the pattern pool wholesale** (see "Current state" above) -
  a clip *is* what Session view triggers; there's no separate "pool
  pattern" concept alongside it. Session view's own rows become "this
  track's own clips" instead of "this track's own pooled patterns" - same
  UI, same trigger/quantize/assign mechanics already built, different
  backing model underneath.
- **Length is always a whole multiple of 1 bar** (`Song::getRowsPerBar()`)
  - never an arbitrary row count. This is what makes Phase C's own
  "one grid row is one bar" rendering possible at all: a clip's length is
  always expressible as a whole number of grid rows.
- Also satisfies, and retires as separate backlog items: "no in-app way to
  author a pooled pattern" (`copy-to-clip` *is* the authoring path) and
  "cross-track cell assignment/instrument-compatibility checking" (a
  clip's instrument-compatibility check on placement is inherent to this
  phase's own design, not a separate gap).
- **Open questions** (deliberately not resolved yet, matching the
  original plan's own "needs usage tracking" deferral):
  - Stable identity: `Pattern` already gets an internal id for free
    (`SongObject`) - does a clip reuse that directly, or does an instance
    need its own explicit id scheme independent of in-memory object
    identity (for XML round-tripping, if nothing else)?
  - Instrument-compatibility check specifics: same tuning check Phase -1
    of the old drum-machine plan already built for copy/paste (`Note::
    getValue()` meaning differs by tuning), or something broader (kit
    identity for percussion, e.g.)?

## Phase B: variable-length, titled scenes

`Scene` currently has neither a name nor a length of its own - it's an
implicit, uniform slice sized entirely by the song-wide `patternRows`
(`Song::getPatternLength()`). This phase gives each `Scene` its own title
and its own length, in bars (`Song::getRowsPerBar()`).

- Until this phase lands, scenes stay uniform-length via the existing
  `patternRows` attribute, exactly as today - no interim half-measure.
- Session-view recording extending the actively-playing scene as needed
  (Phase A's own "Recording target" above) depends on this phase existing
  first - a scene has nothing to extend until it has its own length.
- No separate mechanism for *grouping* several scenes into one named
  unit (verse/chorus/bridge) is needed once scenes are freely variable-
  length - a scene can just be as long as one of those needs to be, one
  scene per section of the song, rather than several small fixed-length
  scenes glued together after the fact. (An earlier draft of this plan
  cited a `Section` class as an existing-but-unfinished stub solving this
  - there is no such class anywhere in the current codebase; that claim
  was wrong, inherited from an old plan document without verifying it
  against the actual code.)

## Phase C: the arrangement grid

Replaces `PatternMatrix` outright with a real arrangement view.

- **One grid row is one bar** (not one raw pattern row, and no longer one
  full scene either) - a scene now spans as many rows as its own Phase B
  length says.
- A clip instance renders as a colored block spanning its own length in
  rows (Phase A guarantees a whole number of bars), confined to the one
  scene it's placed in - a clip never crosses a scene boundary.
- Each clip block shows a single hexadecimal digit - its own ordinal
  position in that track's clip list, the exact same ordinal Launchpad
  Session view's own rows already address (today: `pool_index = 7 - y`;
  post-Phase-A, whatever the equivalent clip-list position becomes) - so
  a block in the arrangement grid and a row on the Launchpad agree on
  which physical pad would trigger it.
- Track ordinals (today's bold numeric column header) are removed.
- Carries forward from `PatternMatrix`, still true here: shared track/
  scene cursor with `PatternEditor`/Launchpad, the virtual "one past the
  last scene" row.
- **No copy/paste in the arrangement grid itself, at least for now** -
  unlike `PatternMatrix`, which had its own single-cell `kill-region`/
  `kill-ring-save`/`yank`. Placing/moving clip content is entirely
  `copy-to-clip`/`insert-clip`'s job (Phase A), done from `PatternEditor`;
  the grid is placement/overview only.
- **You can only edit an instantiated clip, never the arrangement grid
  directly** - editing means going to any one of a clip's own instances
  (all live-linked, per Phase A) and editing it there in `PatternEditor`,
  the same as any other track content. The arrangement grid itself has no
  note-editing surface of its own, matching `PatternMatrix` never having
  had one either.

## Phase D: visual unification with the drum machine

Converges the drum machine's per-lane step cells and the new clip-block
style on one shared visual language where it makes sense to - but not
totally: the drum machine's own compact rendering (no velocity/delay
columns, one narrow cell per lane) stays exactly that, since a step grid
and a clip block are showing different things and forcing one rendering
onto both would lose real information. Exact scope of what *does*
converge (color language? border/fill convention?) is undecided - revisit
once Phase C's own clip rendering actually exists to converge toward.

- Also where `PatternEditor`'s own row rendering starts showing a clip
  instance's hexadecimal id right next to its content, not just the
  coarser Arrangement grid (Phase C) - deliberately not part of Phase A
  despite clips existing from that point on, so it converges on whatever
  single-hex-digit convention Phase C's own clip blocks settle on
  (already specified there: a clip's ordinal position in its track's own
  clip list) rather than inventing a separate one first and having to
  reconcile the two later.

## Phase E: clip viewer

A dedicated per-track clip list, showing every clip in a track's own
`Song::getClips(track_id)` (name, loop flag, length) with commands to
rename one, delete one outright, and toggle its loop flag - not
`PatternEditor` and not the Arrangement grid (Phase C):

- **Not `PatternEditor`.** A clip's own name/loop/length are properties
  of the clip object itself, not of any one row of its content - there's
  no natural row/cell in the note grid to anchor an edit command to (this
  was the original plan for the loop toggle specifically; reconsidered
  mid-Phase-A once it was clear it doesn't actually belong there).
- **Not the Arrangement grid either** (Phase C) - a clip block there
  shows a *placement*, not the underlying clip list itself, and this
  phase needs to exist independently of whether an instance of a given
  clip has even been placed anywhere yet.
- **The loop toggle is no longer optional polish**, same reasoning as
  when it was (briefly, mid-session) slated for Phase A instead: with
  continuous-vs-one-shot central to how a placed instance behaves ("The
  layer model" in Phase A above), some surface has to be able to flip
  `Clip::isLooping()` - it's just this phase's own surface doing it, not
  `PatternEditor`'s. Already useful before Phase A's own
  instance-placement work lands, too - Session view's live
  clip-triggering already reads it today.
- **Delete has no precedent to follow** - there's no existing way to
  remove a clip at all today, short of hand-editing it out of the XML.
  What happens to an already-placed instance of a deleted clip is an
  open question, deferred along with the rest of this phase.
- **Deliberately sequenced here (just before Phase F), not right after
  Phase A** despite the loop toggle's own urgency above - what this
  phase's own widget should actually look like isn't clear yet, and
  working through Phases B-D first will surface real usage patterns (how
  many clips a track realistically accumulates, how they get browsed/
  picked today via Session view) worth having before designing a
  dedicated surface for them, rather than guessing now.
- **Open:** exact widget shape - a new `UIElement` sibling of
  `PatternMatrix`/`InstrumentList`, or reusing `HierarchyView`'s
  currently-dead tree-list machinery (`UI.cpp`'s own `#if 0`-wrapped
  construction) - undecided.

## Phase F: nested Effect automation in clips

A clip's full/eventual form is one `Pattern` per relevant `track_id`, not
just the leaf track's own - the leaf track itself, plus any number of
ancestor Effect tracks in between it and the song's own global
`MasterTrack`. **The master track itself is deliberately excluded** - its
own content (song-level automation, e.g. tempo changes if those ever
exist - see the open question below) stays purely background, always
hand-edited directly in `PatternEditor` when finishing an arrangement,
never bundled into a clip. Effect-track content is captured as a deep
copy at `copy-to-clip` time, not live-linked the way the leaf track's own
Pattern is - a clip's live-linking is a property of the clip object
itself; nested Effect automation on its own isn't musically meaningful
without accompanying notes anyway (a filter sweep or send-level ramp with
nothing sounding underneath it is useless - it would make no sense for
Session view to be able to launch "just the automation, no notes"), so it
always travels bundled with the leaf content it was captured alongside,
never as an independent thing.

Genuinely hard enough to need its own phase, sequenced after Phase A
rather than inside it - Phase A's own clip storage is already shaped
`track_id -> Pattern` in anticipation (see its own "Storage is
forward-shaped for Phase F" note), so this phase only has to add
population/capture logic, not migrate the underlying representation.

- **Open:** if two leaf tracks share an ancestor Effect track and their
  own clips' deep copies of that Effect's automation disagree, what
  actually happens when both are placed such that they'd affect it at
  overlapping rows? One plausible starting intuition - whichever clip's
  own automation gets (re-)written into the Effect's own scene slot later
  simply overwrites the other, the same "later placement wins" rule
  already governing ordinary note-content overlap elsewhere in this plan
  - but this isn't resolved, just a starting point for when this phase is
  actually tackled.
- **Resolved: the master track stays out of clips entirely**, for now.
  Today it doesn't actually carry any automation at all anyway - it's a
  near-empty stub (`MasterTrack.h`), and tempo (`Song::bpm_`) is a single
  whole-song scalar with no per-row change mechanism. `docs/commands.md`
  now lists a `3Txx` ("set tempo to `xx` BPM") command under "Planned"
  for clarity, but it stays a no-op until this gets implemented, and even
  once it is, per-row song-level automation like this stays purely
  background - added by hand directly in `PatternEditor` while finishing
  an arrangement, the same way any other small, not-worth-reusing content
  already is (Phase A's own "layer model") - never something a clip
  captures, carries, or could silently drag along by
  being moved or duplicated.

## Future / uncertain

Carried over from `pattern-grid.md`, not clearly belonging to any phase
above - revisit and prune once the phases above are further along; some
of these may turn out unwanted. (Editing note content from the
arrangement grid, and copying nested Effect automation, were also on this
list - both now settled, in Phase C and Phase F respectively, not
repeated here. The clip loop toggle was too - briefly promoted into
Phase A's own "Authoring" section mid-session, then moved again once it
was clear it isn't `PatternEditor` work at all: it now lives in Phase E's
own clip viewer instead.)

- Numeric blend-factor tuning for LED/terminal "currently playing"
  highlights on real hardware/a real terminal - the schemes themselves
  are implemented and unrelated to the arrangement work (`PatternMatrix`'s
  own 0.15 playhead-row blend becomes moot once Phase C replaces it
  outright; `LaunchpadManager`'s Session-view 0.5/0.25 triggered/queued
  blends stay relevant regardless of any phase here), just never
  eyeballed against real Launchpad LEDs, only reasoned about.
- Phase continuity across a scene boundary, revisited given the clip
  model: mostly resolved for the common case - a clip re-placed fresh at
  the start of each scene it recurs in naturally keeps correct phase, for
  free, from bar-alignment plus every instance always starting fresh at
  its own row 0. Only remains a real (if narrow) gap when a clip's own
  length doesn't evenly divide the scene's own length - the leftover
  partial repeat still just cuts off at the scene's own end rather than
  continuing its own phase into a fresh instance placed in the next
  scene, the same "cut off wherever the context ends" behavior
  `Pattern.h` already documents for the pre-clip model. Not worth solving
  given how narrow the case is, but not fully gone either.
