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

Four phases, in this order because the data model has to exist before
there's anything to draw:

- **Phase A** - shared/referenced clips (data model).
- **Phase B** - variable-length, titled scenes (data model).
- **Phase C** - the arrangement grid itself (view), including removing
  today's `PatternMatrix`.
- **Phase D** - visual unification with the drum machine's own rendering.

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
  track's own pooled patterns, columns are the shared track cursor.
  Record Arm gates trigger-live vs. assign-into-the-current-scene, same as
  ordinary note entry - this assign path *is* arrangement recording, per
  the Context above. Every launch/swap/stop, on any track, is quantized to
  one shared grid (`Song::getRowsPerBar()`, default 16 rows/bar - also
  drives a stronger bar-boundary highlight in `PatternEditor`, on top of
  its existing every-4-rows beat one) measured from a shared origin set
  the moment the first pattern anywhere launches into an otherwise silent
  session. `Pattern::isLooping()` (default true) makes a pooled pattern a
  one-shot when false. CC95 (Session)/CC96 (Note)/CC97 (Custom, `DRAW`
  mode) are a trio of exclusive per-device mode buttons. Stop Clip (CC49)
  is a held modifier: holding it and pressing any pad in a column stops
  that column's own track. The drum machine's own configuration (tap =
  picker latch, hold = clear step data) is on CC98.
- **A shared pattern pool** (`Song::getPooledPatterns(track_id)`/
  `addPooledPattern()`, `std::unordered_map<int, std::vector<Pattern>>`,
  a new top-level `<patterns>` XML element) - reusable named `Pattern`s
  per track, outside any one scene position. **Superseded by Phase A**,
  which replaces this with a real clip object (stable identity, usage
  tracking) rather than a plain positional vector - see Phase A below.
  Until Phase A lands, this is the pool Session view addresses.
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
- `PatternEditor`'s own row rendering shows a clip instance's hexadecimal
  clip id right next to its content, not just the coarser Arrangement
  grid (Phase C) - needed from the moment clips exist at all, so it's
  visually obvious at the note level that a row belongs to a clip (and
  which one) while editing, before Phase C's own grid even exists.

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
- Carries forward from `PatternMatrix`, still true here: single-cell
  copy/paste under the `kill-region`/`kill-ring-save`/`yank` names,
  shared track/scene cursor with `PatternEditor`/Launchpad, the virtual
  "one past the last scene" row. Multi-cell rectangular copy/paste
  (`PatternMatrix`'s old single-cell-only limitation) is superseded by
  Phase A's own cut/copy/paste-as-clip workflow rather than being solved
  as a separate feature.
- Editing note content directly from the arrangement grid is still out of
  scope, same as it was for `PatternMatrix` - note-level editing stays in
  `PatternEditor`.

## Phase D: visual unification with the drum machine

Converges the drum machine's per-lane step cells and the new clip-block
style on one shared visual language where it makes sense to - but not
totally: the drum machine's own compact rendering (no velocity/delay
columns, one narrow cell per lane) stays exactly that, since a step grid
and a clip block are showing different things and forcing one rendering
onto both would lose real information. Exact scope of what *does*
converge (color language? border/fill convention?) is undecided - revisit
once Phase C's own clip rendering actually exists to converge toward.

## Future / uncertain

Carried over from `pattern-grid.md`, not clearly belonging to any phase
above - revisit and prune once the phases above are further along; some
of these may turn out unwanted.

- Editing note content from the arrangement grid itself (see Phase C) -
  still just overview + block copy/paste, not a second pattern editor.
- Copying a leaf track's own nested Effect automation alongside a
  clip/cell - silently left behind today; no single owning clip to fold
  it into when the Effect is shared by more than one leaf track.
- Multi-Launchpad tiling for extended real estate (more octaves in NOTES
  mode, a larger grid in SESSION mode by spreading one logical view
  across several devices) - unrelated to the arrangement work, orthogonal.
- Numeric LED/blend tuning for the Launchpad's own playhead-row highlight
  on real hardware - scheme implemented, factors never eyeballed against
  a real device.
- Real decoupled lane identity for a drum lane (routing to a custom
  instrument with no GM meaning) - the numeric GM note is still the
  underlying identity everywhere (`DrumRankTable`, the Launchpad picker,
  kit resolution).
- Phase continuity across a scene boundary - a repeating triggered
  pattern always restarts at row 0 the moment a new scene starts, during
  real transport playback.
- A UI hook for a clip's own loop toggle (`Pattern::isLooping()`) from
  `PatternEditor` - exists (tests, XML round-trip) but nothing interactive
  reaches it yet. (A clip's own *length* gets a hook for free once Phase
  A/C's cut/paste-as-clip workflow exists - this is just the separate
  loop-vs-one-shot toggle.)
