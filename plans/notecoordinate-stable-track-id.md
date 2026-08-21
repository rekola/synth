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
  needed to hit the same bug.

`NoteCoordinate.h`'s own doc comment anticipated *one* class of non-reproducibility
(replaying the same song twice within one process, at different points in playback
history) and solved it. It did not anticipate this second class - the same song
loaded in two different processes (or twice in the same process, after unrelated
`SongObject`s were constructed in between) getting a different `track_id` for the
identical, authored track - because the "stable" identity it built on
(`getInternalId()`) was never actually process-independent to begin with.

## Design

The file format already has what's needed: `SongObject::id_` (the authored,
optional XML `id="..."` string attribute - `id="0"` in the fixtures above) plus
`trackReferenceText()`/`resolveTrackReference()` (`Song.cpp`) already prefer it
over the raw internal id specifically for stable round-tripping. The bug is that
none of the four sites above lean on that existing mechanism; they all re-derive
`track_id` from the transient `getInternalId()` after the stable reference has
already been resolved.

Two candidate fixes, not yet chosen between:

1. **Use the track's position in `song.getRootTrackIds()` (file order) as the
   `NoteCoordinate` track_id**, computed once at load/song-open time into a
   `Track* -> stable_index` lookup, independent of both the textual `id=` attribute
   (which may be absent) and `getInternalId()` (process-dependent). Simplest to
   reason about - file order is already deterministic and already how
   `getRootTrackIds()`/`collectRootTrackIds()` work - but changes what "track_id"
   *means* everywhere it's read back (UI, Launchpad live-note dispatch, XML
   references), so needs auditing every `getTrackByInternalId()` call site, not
   just the four `NoteCoordinate` ones.
2. **Add a narrower, NoteCoordinate-only stable id**: keep `Scene`'s
   `patterns_by_track_id_`/UI/Launchpad code on `getInternalId()` unchanged (it's a
   fine opaque runtime key for those - nothing there feeds it to a hash function),
   and thread a *separate* stable value (derived the same way as option 1, or from
   the textual `id=` when present) through only the four sites listed above. Larger
   diff (two id concepts to keep straight) but zero blast radius outside the
   randomization path.

Leaning toward (2) for the smaller, better-contained diff, but this needs a closer
look at how many other places already conflate "internal id" with "stable
identity" before committing to either.

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
- Full `ctest` suite green.

## Constraints honored

- No change to `HashField`'s own hashing/mixing (`dsp/HashField.h`) - the bug is
  entirely in what value gets fed into `NoteCoordinate`, not how it's hashed.
- No behavioral change to *within-one-process* determinism (looping the same song
  twice in one already-loaded process must keep producing identical output, per
  `NoteCoordinate.h`'s existing invariant) - only the *across-process* value
  changes.
