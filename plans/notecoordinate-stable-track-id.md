# Make NoteCoordinate's track_id independent of process history

## Context

`tests/RenderTests.cpp`'s `render_golden_hash_catches_randomization_regressions`
(added in `cc28c32`, "Migrate every randomness call site to HashField/NoteCoordinate")
pins exact FNV hashes of three fixtures' rendered output, on the theory that
`NoteCoordinate`-seeded randomization (`src/model/NoteCoordinate.h`) is fully
reproducible given the same song content and build. That theory turned out to be
wrong: CI started failing on all three hashes (2026-08-20) with no code change to
any of the three fixtures' own render path. Root-caused in two parts - the first is
already fixed, the second (this plan) is not.

**Part 1 (fixed, `src/model/TreeNode.h`):** `TrackState`/`VoiceState` children were
stored in `std::unordered_map<int, std::unique_ptr<Derived>>`, keyed by
`SongObject::getInternalId()` - a single counter (`std::atomic<int> next_id`,
`SongObject.h`) shared by the *entire process*. Bucket order for an
`unordered_map<int, ...>` depends on the actual id value, so
`renderChildren()`'s floating-point sum over siblings was reordered whenever the
counter's starting value differed - which happens whenever any unrelated
`SongObject` is constructed earlier in the same process (e.g. by another test in
the suite). Fixed by switching `children_` to a plain, insertion-ordered
`std::vector<std::pair<int, std::unique_ptr<Derived>>>`, keeping the id paired
alongside the child rather than stamping it onto the child itself - the id names
which model object this state was built for, a fact about the parent's
relationship to the child, not something the (otherwise identity-less) state
object should carry as its own field.

**Part 2 (this plan, not fixed):** that alone did not make the golden hash stable.
Confirmed with a burn-in test - construct N unrelated `Song`/`InstrumentProvider`
objects (to advance the global id counter) before rendering the same fixture, and
observe the hash change with N even after Part 1's fix:

```
burn=0:  ambisonic_envelopefilter_notemultiplier.xml hash=0x5d96df29cca44900
burn=1:  ambisonic_envelopefilter_notemultiplier.xml hash=0x2ba2bf0ae92a2268
burn=5:  ambisonic_envelopefilter_notemultiplier.xml hash=0xdf42ddc5109273f3
burn=50: ambisonic_envelopefilter_notemultiplier.xml hash=0x3167ebcb63494560
```

(same pattern for `tape_degradation_all_presets.xml`/`arpeggiator_pattern_chord.xml`,
including on fixtures whose trees never have more than one child anywhere, ruling
out Part 1's mechanism as the cause here).

**Root cause:** `NoteCoordinate`'s `track_id` field - documented as "a stable
per-note coordinate... Built from the note's *authored* position only" - is in
every real construction site actually `Track::getInternalId()` or a value derived
from it, i.e. the same global, process-wide counter Part 1 just fixed for a
different data structure:

- `src/model/Song.cpp:472` - parsing `<pattern track="N">` resolves the XML
  reference to a `Track&` (already preferring the track's authored textual
  `id="..."` attribute over any numeric fallback - see `resolveTrackReference()`),
  then immediately throws that stability away: `scene.setNote(row, track->getInternalId(), ...)`
  keys the in-memory `Scene::patterns_by_track_id_` map by the volatile runtime id.
- `src/state/SongState.h:163,200` - pattern-driven playback reads that same key
  back out of `scene.getPatternsByTrack()` and passes it straight into
  `NoteCoordinate(track_id, absolute_row, column)`.
- `src/playback/Player.cpp:242` - the live-playback path builds
  `NoteCoordinate(instrument_track.getInternalId(), live_note_counter_++, column)`,
  same issue.
- `src/effects/TapeDegradation.cpp:571` - builds its own per-instance seed as
  `NoteCoordinate(getInternalId(), 0, 0)` directly, no intermediate track lookup
  needed to hit the same bug. Not quite the same shape as the other three,
  though: `TapeDegradation`'s own `TrackType` is `EFFECT`, not one of the four
  types `collectRootTrackIds()` (`Song.cpp`) treats as a leaf - the walk that
  builds `getRootTrackIds()` recurses *past* a `TapeDegradation` node into its
  wrapped `<track>` child and collects *that* id instead. So
  `getInternalId()` here was never in the same id-space `getRootTrackIds()`/
  `patterns_by_track_id_` use at all; it's a distinct wrapper-node identity
  that happens to also be counter-derived, not literally the same value with
  the same bug. See "How a track gets its ordinal" below for what it should
  use instead.

`NoteCoordinate.h`'s own doc comment anticipated *one* class of non-reproducibility
(replaying the same song twice within one process, at different points in playback
history) and solved it. It did not anticipate this second class - the same song
loaded in two different processes (or twice in the same process, after unrelated
`SongObject`s were constructed in between) getting a different `track_id` for the
identical, authored track - because the "stable" identity it built on
(`getInternalId()`) was never actually process-independent to begin with.

Already flagged, independently of this plan: `docs/known_bugs.md` (the
`NoteCoordinate::track_id_` entry) covers the same root cause for the
`SongState.h` pattern-playback sites, found while verifying an unrelated
`InstrumentProvider` alias-table rename - removing 4 incidental startup object
constructions there shifted every later track's internal id by 4, changing the
rendered audio of every song using the affected jitter/decorrelation paths
(including songs with no SoundFont content at all). That's the same failure mode
as the burn-in test above, caught by hand rather than by a repeatable check. That
entry doesn't cover the `Player.cpp`/`TapeDegradation.cpp` sites above, which hit
the bug directly rather than through `SongState.h`'s relay - a full fix needs all
four, though (per the `TapeDegradation` note just above) not all four resolve the
same way.

## Design

**Decided: the track's ordinal position** becomes *the* stable track identity for
this purpose, shared with `plans/per-track-patterns-scenes-matrix.md`'s future
Pattern Matrix (rows = sequence positions, columns = tracks - the same ordering).
One shared concept, one source of truth, rather than two independently-invented
stable-id schemes that happen to agree today and could drift apart later.

**Mechanism: a `TrackOrdinalRegistry`, threaded through `createState()`/
`createStateTree()`**, not a field stamped onto `Track` by `Song`. Named for
what it actually does (assigns ordinals by walking the tree once, then answers
lookups against that), not `TrackContext`/`*Context` - this codebase already
has a `RenderContext` (`src/state/RenderContext.h`, holding pending events/
azimuth ticks/bpm) threaded through the exact same neighborhood of code
(`TrackState::render(frames, instruments, context)`, right next to
`createState()`/`createStateTree()`); a second, differently-scoped `*Context`
in that same call chain would just create "which `context` is this" confusion
at every call site that ends up needing both. A first draft of this plan also
proposed a field stamped onto `Track` by `Song` (`Song` recomputing and writing
a cached `Track::ordinal_` whenever the track list's structure changed);
rejected as too different a shape from how this codebase already threads new
per-note/per-build information through this exact call chain
(`needs_decorrelation`'s addition to `playNote()` is the direct precedent - see
"Fix SF2/file-sample click on note-on..."). Concretely:

- A `TrackOrdinalRegistry` is built once per state-tree build
  (`SongState::initialize()`/whatever (re)builds a `SongState` from a `Song` -
  see the open question below on rebuild frequency), by walking the track tree
  once, assigning sequential ordinals to every qualifying track in encounter
  order - the same walk `collectRootTrackIds()` (`Song.cpp`) already does,
  generalized (see "What qualifies" below).
- `Track::createState()`/`createStateTree()` (`Track.h`) both gain a `const
  TrackOrdinalRegistry &` parameter, threaded through exactly like
  `needs_decorrelation` was: every override (`~14` files - `Amplifier`,
  `BiquadFilter`, `Chorus`, `Compressor`, `EnvelopeFilter`, `ResonantFilter`,
  `Tremolo`, `Distortion`, `TapeDegradation`, `Arpeggiator`, `InstrumentTrack`,
  `DrumMachineTrack`, plus `Track`'s and `Song`'s own default bodies) just
  forwards it unchanged; only the handful that actually need their own ordinal
  (any `Effect` subclass wanting a `NoteCoordinate` at construction time -
  `TapeDegradation` is the concrete site found so far, not the only one this
  applies to - see below) calls `registry.getOrdinalFor(*this)`.
- Narrower blast radius than the first draft's "redefine `getTrackByInternalId()`
  everywhere" idea: `Scene::patterns_by_track_id_`, `getTrackByInternalId()`,
  and every UI/Launchpad/Controller call site that resolves a `track_id` back to
  a `Track*` keep using `getInternalId()` exactly as today - nothing there feeds
  a hash function, so the volatile counter is genuinely fine as an opaque runtime
  key for those. Only the actual `NoteCoordinate(...)` construction sites
  (`SongState.h:163,200`, `Player.cpp:242`, `TapeDegradation.cpp:571`) change, to
  resolve `track_id`/`this` to an ordinal via the registry immediately before
  building the coordinate, instead of using the raw internal id directly.
- Since the registry is available for the whole build/session, not just torn
  down after one `createState()` pass, `SongState.h`'s per-block event-dispatch
  loop and `Player.cpp`'s live-note path can hold onto it (or a reference to
  it) and query it exactly the same way a track queried it during
  construction - one registry, read at both construction time and per-note-
  event time.

**What qualifies for an ordinal.** Not a fixed `TrackType` list any more, and not
derived from what `getRootTrackIds()`/`fill_track_info()` currently expose as UI
columns either - the registry's walk is the *canonical, future-complete* rule,
which today's UI is allowed to lag behind, not the other way around. Rule:
every ordinary leaf track type as today, **plus every `Effect`-typed track
unconditionally** (`TapeDegradation`, `Chorus`, `Compressor`, `Distortion`,
`EnvelopeFilter`, `BiquadFilter`, `ResonantFilter`, `Tremolo`, `Amplifier` -
every subclass of `Effect`, wrapped or not), not just the currently-childless
case a first draft of this plan proposed. Each isn't going to stay a
pass-through wrapper indefinitely: each is getting its own effect command
column in the UI regardless of whether it wraps an instrument (controlled the
same way either way), so each should get its own ordinal slot the same way,
now, rather than only once that UI work lands. Its `createState()` then just
queries the registry for its own ordinal directly
(`registry.getOrdinalFor(*this)`) like any other qualifying track - no
delegation to a wrapped child, no case-splitting on whether one exists.

A bare `TrackType::EFFECT` check almost mis-qualified something with this rule:
`NoteMultiplier` (`src/instruments/NoteMultiplier.h` - note the directory,
*not* `src/effects/` alongside every real one above) carries `TrackType::EFFECT`
today despite being nothing like them - a voice-generation-time wrapper
(unison/detune spread, authored once as instrument-definition attributes), not
a processing effect with parameters worth an automatable command column. That's
a mistagging bug in its own right, independent of this plan, and the real fix
is there, not a special case in the rule above: give `NoteMultiplier` its own
`TrackType` (something like `NOTE_MULTIPLIER` - exact name not load-bearing)
distinct from `EFFECT`, so "every `Effect`-typed track" stays a correct,
literal check rather than needing "`Effect` minus this one exception" carved
out of it.

**Two separate trees, and the registry only ever walks one.** Worth stating
explicitly, since `NoteMultiplier` raised it: this codebase has two distinct
track trees, both built from the same `Track`/`createTrack()` machinery
(`Song.cpp` - one shared factory parses both sections) but serving different
roles - `song.getTracks()` (the `<tracks>` section: `InstrumentTrack`/
`PercussionTrack`/`DrumMachineTrack`/`SampleTrack`/`Group`/per-track `Effect`s -
what `collectRootTrackIds()` walks and what appears as pattern-editor columns)
and the instrument-definition pool (the `<instruments>` section, referenced by
`InstrumentTrack::getInstrumentId()` - `Instrument` and its leaves
`GenericInstrument`/`Oscillator`/`Noise`/`FileInstrument`/`SoundFontInstrument`,
plus wrapper types like `NoteMultiplier` nested inside, walked only when
constructing voices via `playNote()`). The registry's walk is scoped to the
first tree only, same as `collectRootTrackIds()` today - most instrument-pool
leaves are straightforwardly out of its reach, not deliberately excluded.
`Group` *is* reachable (it lives in the first tree) and, matching
`collectRootTrackIds()`'s existing behavior, this plan keeps it a pure
recursive pass-through with no ordinal of its own - but that's this plan
defaulting to today's behavior, not a considered decision that `Group` should
stay that way, and it's explicitly not this plan's issue to settle either way.
Worth recording the reasoning that came up while discussing it, though, for
whoever does: a `Group` could plausibly want its own effect command column
eventually (an alternative for a user who wants more command columns without
restructuring their tracks), but the stronger argument is mute/solo - a group
of tracks getting its own mute/solo buttons is a natural, expected feature,
and mute/solo state needs somewhere stable to live, which argues `Group`
should eventually be independently addressable (an ordinal-bearing track in
its own right) rather than a transparent pass-through. Not resolved here -
flagged so it isn't accidentally treated as settled by this plan's default.

**`LFO` specifically isn't settled enough to place confidently either way.**
It's `Instrument`-derived (instrument-pool-only) today, but the intent is for
an LFO to eventually modulate another track's own parameters (a lowpass
filter's cutoff was the example given) - which could mean it needs to be
reachable from outside the one instrument definition it's nested in, or it
could end up in a separate modulation-routing table (envelopes and inputs
alongside it) rather than belonging to either tree as currently structured.
Genuinely undecided, not this plan's call to make - flagged so a future
implementer doesn't read the "instrument-pool leaves are out of reach" framing
above as having already settled `LFO`'s case too.

**Single source of truth, not a deferred one - `getRootTrackIds()` and
`fill_track_info()` move together, in this plan, not later.** An earlier draft
of this section let `getRootTrackIds()`/`fill_track_info()` keep their current,
narrower behavior until some future effect-command-column UI work caught up.
Rejected: that's exactly the "two schemes that happen to agree today and could
drift apart" shape this plan has been explicitly steering away from since the
Matrix-sharing decision above, just deferred rather than avoided. Concretely:

- `Song::getRootTrackIds()` (`Song.cpp`) stops having its own
  `collectRootTrackIds()` walk and instead returns the `TrackOrdinalRegistry`'s
  own ordinal-ordered id list - one walk, not two copies of "which nodes are
  the real addressable tracks" that could quietly diverge.
- The registry's single walk also builds each qualifying track's *baseline*
  `VisibleTrackInfo` (column shape from its `TrackType`/own settings -
  `has_note_column_`/`has_velocity_columns_`/`has_delay_column_`/
  `has_effect_column_` - exactly what `fill_track_info()`'s type-switch decides
  today, generalized the same way the ordinal rule was: any `Effect` subclass,
  not just `TapeDegradation`, gets the one-column `has_effect_column_ = true`
  shape `DRUM_MACHINE`/`SAMPLE` already get as a placeholder). Not the same
  thing as `VisibleTrackInfo`'s *dynamic* half, though - `Scene::
  getTrackInformation()`'s `updateSubtrackInfo()` call, which widens
  `num_subtracks_` from whatever note data is actually present in the
  *currently visible* rows, stays exactly where it is, in `PatternEditor`'s own
  per-call `getTrackInformation()`. That pass depends on scroll position and
  live pattern content, neither of which the registry (built once per
  structural change, not per frame/scroll) should know about; it already runs
  as a separate pass *before* `fill_track_info()`'s static one in the current
  code (`PatternEditor.cpp`), so replacing just the static half with a copy of
  the registry's baseline, then layering the dynamic widening on top exactly
  as today, is a direct swap, not a new shape.
- Without the static half moving too: since `getRootTrackIds()` starts
  including every `Effect`-typed track unconditionally as of the point above,
  leaving `fill_track_info()` unchanged would fall through to its existing
  fallback for any id it has no entry for - a default-constructed, nonsensical
  plain-note `VisibleTrackInfo` - an active regression on every existing
  per-track-effect in every existing song, not a deferred inconsistency.
- Real, visible, and worth stating plainly rather than treating as incidental:
  every song using a per-track effect (`<tapeDegradation>`, `<chorus>`,
  `<compressor>`, ... - the existing `tape_degradation_all_presets.xml` fixture
  included) gains one genuinely new, usable effect-command column in the
  pattern editor the moment this ships - a small piece of real UI capability
  riding along with the identity fix, not just an internal renumbering.
  `PatternEditor`/`LaunchpadManager`'s cursor/column-index code doesn't
  hardcode which `TrackType`s appear in `track_ids`, so it shouldn't need
  further changes beyond the registry/`fill_track_info()` split above - but
  this needs verifying, not assuming.
- **Explicitly not in scope: mute/solo for effects.** `isSolo()`/`isMuted()`
  (`InstrumentTrack.h`) live only on `InstrumentTrack` today - `Effect` has no
  mute/solo concept at all, and this plan's single, minimal
  `has_effect_column_` doesn't add one. That's real, separate design work (own
  state - on `Effect`, or shared with `Group` if mute/solo become a general
  "any ordinal-bearing track" concept rather than an `InstrumentTrack`-specific
  one, tying back to the `Group` discussion above - plus UI rendering and a
  playback-side answer to "what does soloing one effect even do") this plan
  doesn't take on. It only makes the effect *addressable* (ordinal, column);
  it doesn't make it *controllable* the way an `InstrumentTrack` already is.

**Not folded in: `get_track_parents()`** (`PatternEditor.cpp`) - a third
tree-walk, but answering a different question ("who is this node's parent,"
for *every* node, presumably for group mute/solo propagation) than "which
nodes are ordinal/column-bearing." Related duplication, not the same rule -
noted so it isn't silently assumed in scope here.

**Resolved: ordinals are expected to change on structural edits.** Not a
"slot" that survives a sibling's removal - a fresh, dense 0, 1, 2, ...
renumbering on every add/remove/reorder, matching `NoteCoordinate`'s own need
(reproducibility given the *same* song content, nothing stronger). Consequence
for the Matrix plan, worth carrying over there rather than re-deriving: a
Matrix column reshuffling when a track is added/removed elsewhere is expected,
by-design behavior under this scheme, not something to engineer around with a
separate stable-slot mechanism - the same way inserting a column in a
spreadsheet shifts everything after it.

## Verification

- A standalone rebuild of the golden-hash burn-in check (constructing N unrelated
  `Song`s before rendering each of the three existing fixtures) must show the hash
  independent of N, for N up to at least 50 - the same check used to find this bug.
- `tests/RenderTests.cpp`'s existing golden-hash constants get re-pinned once, and
  should not need re-pinning again by an unrelated test addition anywhere else in
  the suite - spot check by adding and removing a throwaway `TEST()` elsewhere and
  confirming the three hashes don't move.
- Existing `NoteCoordinateTests`/`HashFieldTests`/`ArpeggiatorStateTests` stay
  green unchanged (this should not alter *within-one-render* reproducibility, only
  *across-process* reproducibility).
- `getTrackByInternalId()` itself is unchanged (per the narrower-blast-radius
  decision above) - what needs checking instead is the new
  `TrackOrdinalRegistry`/`getRootTrackIds()`/`fill_track_info()` consolidation:
  `PatternEditor`/`LaunchpadManager` cursor-to-track and pad-to-track
  resolution, and a save/load round-trip of a song with a mid-file track
  deleted (or a `<group>`'s nested children, which
  `getRootTrackIds()`/`collectRootTrackIds()` already flatten recursively)
  still resolve to the correct `Track`, now via the registry's list instead of
  `collectRootTrackIds()`'s own.
- Every song using a per-track effect renders a real, correctly-shaped
  effect-command column (not the `fill_track_info()` fallback's bogus plain
  note column) - the regression the "single source of truth" consolidation
  above exists to avoid.
- Full `ctest` suite green.

## Constraints honored

- No change to `HashField`'s own hashing/mixing (`dsp/HashField.h`) - the bug is
  entirely in what value gets fed into `NoteCoordinate`, not how it's hashed.
- No behavioral change to *within-one-process* determinism (looping the same song
  twice in one already-loaded process must keep producing identical output, per
  `NoteCoordinate.h`'s existing invariant) - only the *across-process* value
  changes.
