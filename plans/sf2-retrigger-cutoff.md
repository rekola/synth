# SF2 retrigger cutoff + exclusive-class enforcement

## Context

`rekola/synth` plays some instruments via SoundFont (SF2). Long SF2
release tails cause voices to pile up under rapid note entry: retriggering
the same note repeatedly (or, for percussion, hitting hi-hats that should
choke each other) leaves every prior occurrence ringing out its full
authored release instead of being reclaimed, since nothing currently
recognizes "this new attack already masks/replaces that old voice."
Voice budgeting/stealing (a polyphony cap with LRU-style voice theft) is
explicitly out of scope for this pass - this is about recognizing
specific, well-defined cases where an old voice is either inaudible under
the new one or must be silenced by SF2 convention, not about capping
total voice count.

A track holds exactly one instrument, so all matching described below -
both identity-based and exclusive-class-based - is scoped to a single
track. Nothing here reaches across tracks.

## Step 1 findings - exclusive class (SF2 gen 57): parsed, never enforced

- **Parsing** (`SoundFont.cpp`): `tsf_region` has a `group` field
  (`unsigned int group, offset, end, loop_start, loop_end;`, line 52).
  The generator table documents it explicitly: `{ GEN_GROUP,
  _TSFREGIONOFFSET(unsigned int, group) }, //57 ExclusiveClass` (line
  323), applied via `case GEN_GROUP: region->group = amount->wordAmount;
  return;` (line 344) - a plain absolute assignment, correct per spec
  (not a delta-style generator). Gen 57 **is** correctly read into every
  parsed region's `group` field.
- **Enforcement: absent.** The only other place `region.group` is read
  is `SoundFontInstrument::playNote()` (`SoundFont.cpp:1427`):
  ```
  if (region.group) {
    // FIXME: here we should end all voices with the same instrument and group
  }
  ```
  A literal no-op stub - the condition is checked, nothing happens
  inside it. No other file references exclusive class at all (confirmed
  via repo-wide search).
- **GM percussion choking (open/closed/pedal hi-hat): does not work**,
  as a direct consequence - GM drum kits implement this choking via
  exclusiveClass grouping, and since the class is parsed but never acted
  on, two notes that should choke each other both ring freely instead.
  `PercussionTrack` (`PercussionTrack.h`) is a thin marker subclass of
  `InstrumentTrack` that only changes tuning selection (`Tuning::
  PERCUSSION`, for MIDI-note-number frequency mapping) - no choke logic
  anywhere.
- **Where enforcement can't live:** the FIXME's own location is a
  stateless per-note factory method with no visibility into any other
  voice, so it fundamentally cannot reach across two separate note-on
  events (open hi-hat struck, then later a closed hi-hat struck) - which
  is exactly what choking requires. Real enforcement has to live where
  the identity-based cutoff below also lives: `InstrumentTrackState`,
  the only object that can see every currently-active voice across every
  column of the track.

## Note identity representation (background for the cutoff below)

`Note::getValue()` (`Note.h`) is a single `short` used as **both** the
31-EDO integer step (pitched instruments) and the MIDI note number
(percussion) - the same storage, disambiguated entirely by which
`Tuning` the caller resolves for the track. `Tuner::getFrequency
(Tuning::PERCUSSION, note)` (`Tuner.h:22`) treats the value as a literal
MIDI note number; any pitched tuning (e.g. `Tuning::TET31`) converts it
via that EDO's own step math instead.

The percussion-vs-pitched choice is made **upstream of voice creation**,
at both places a note-on originates, both gated on `TrackType::
PERCUSSION_CONTROL`:
- `Player.cpp:59` - `auto tuning = track->getType() ==
  TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();`
- `SongState.h`'s row-scheduling loop - same check.

By the time a note-on reaches `InstrumentTrackState`/`Player::
handlePlaybackControlEvent`, the raw integer identity (`ev.getNoteValue()`
in the chunked path, `midi_note`/`note.getValue()` in `Player.cpp`'s
`PLAY_NOTE` handler) is already the correct per-track-type identity
value. `InstrumentTrackState` holds no `TrackType`/tuning reference and
doesn't need one for either mechanism below - plain `int ==` comparison
on the value already flowing through both call sites is exactly the
"31-EDO step or MIDI note, as appropriate" identity needed, with no
percussion/pitched branching required inside `InstrumentTrackState` at
all.

`TrackState::getNoteValue()` (`TrackState.h:273`, recurses into
children, returns first non-negative) / `InstrumentVoice::getNoteValue()`
(`InstrumentVoice.h:48`, returns stored `note_value_`, set once in
`playNote()` and never touched by `stopNote()`/`killNote()`) already
exposes this identity for any currently-held `TrackState*`, including a
multi-region SF2 group (every region child shares the identical value,
since one `playNote()` call constructed them all). This machinery
already works today (used by `getAllActiveVoices()` for LED feedback)
and needs no changes.

## Existing release primitives - reuse, don't reinvent

`SoundFont.cpp`'s `SoundFontVoice` already has almost exactly the
fast-release primitive both mechanisms below need, just dead/unwired:
```
void stopNoteQuick() {          // line 1061 - currently unused anywhere
  ampenv_.parameters.release_ = 0.0f;
  ampenv_.nextSegment(EnvelopeState::SUSTAIN);
  modenv_.parameters.release_ = 0.0f;
  modenv_.nextSegment(EnvelopeState::SUSTAIN);
}
```
`EnvelopeState::nextSegment(SUSTAIN)` (`EnvelopeState.h:133`) computes
the RELEASE segment's duration as `parameters.release_ <= 0 ?
TSF_FASTRELEASETIME : parameters.release_`, and `TSF_FASTRELEASETIME`
(`EnvelopeState.h:37`) is `0.01f` - 10 ms, in the requested 5-10 ms
range. Forcing `release_` to 0 before the transition reuses the existing
envelope machinery for a short, real (non-hard-cut) release - same
exponential/linear decay shape as a normal release, just compressed. No
new envelope logic needs to be written; `stopNoteQuick()` just needs
fixing up and exposing through the virtual interface (see below).

### New virtual method: `fastRelease()`

Add to the `TrackState`/`InstrumentVoice`/`SoundFontVoice` hierarchy,
mirroring exactly how `stopNote()`/`killNote()` are already layered:

- **`TrackState::fastRelease()`** (new, `TrackState.h`) - default
  recurses into children (`for (auto & [id, child] : getChildren())
  child->fastRelease();`), same shape as the existing `stopNote()`/
  `killNote()` defaults. Makes a multi-region SF2 group's
  `fastRelease()` call correctly cascade to every region child.
- **`InstrumentVoice::fastRelease()`** (new override, `InstrumentVoice.h`)
  - default `{ stopNote(); }`. `InstrumentVoice::stopNote()` already
  calls `killNote()` (instant cutoff) for every non-SF2 leaf type, so
  this is already effectively "fast" for oscillator-based instruments -
  keeps the method meaningful for every instrument type a track might
  hold, not just SF2.
- **`SoundFontVoice::fastRelease()`** (new override, `SoundFont.cpp`) -
  built from `stopNoteQuick()`'s body, with two fixes needed to expose
  it safely:
  1. Guard with `if (!ampenv_.isDone())` / `if (!modenv_.isDone())`
     before each `nextSegment(SUSTAIN)` call - the same guard added to
     `stopNote()` earlier this session, for the same reason: an
     unconditional `nextSegment(SUSTAIN)` resurrects an already-`DONE`
     envelope, and `fastRelease()` can be called on a voice that's
     already finished (e.g. two mechanisms both matching the same
     already-done voice - see composition notes below).
  2. End with `TrackState::killNote()` (not `stopNote()`/`fastRelease()`)
     to immediately finish any modulator children - same reasoning
     already documented on `SoundFontVoice::stopNote()`: a modulator
     child's envelope only advances via `process()`, called exclusively
     from `SoundFontVoice::render()` on its *own* envelopes, never
     propagated to children, so a child left in RELEASE would never
     reach DONE on its own.
  - Keep the `loop_mode == TSF_LOOPMODE_SUSTAIN` -> `loopEnd_ =
    loopStart_` adjustment from `stopNote()`, for consistency, even
    though the tail is short.
  - Retire `stopNoteQuick()` - fold its body into `fastRelease()` rather
    than keeping both.

## Identity-based retrigger cutoff

**Rule** (exact-`int`-equality, scoped to one track):
- Same identity as a voice still sounding anywhere in this track -> that
  prior voice gets `fastRelease()`'d. It's masked by the new attack,
  inaudible either way; the only effect is reclaiming the voice.
  Exact-step equality only - a 31-EDO cluster (chord) has different
  integer values per note and never self-matches.
- Different identity replacing the column's current note -> **no** fast
  release. Normal `stopNote()` (natural release/ring-out) - this is
  already exactly what `stopVoices()` does today; unchanged.

**Where it's wired in.** Both existing note-on call sites already funnel
through `InstrumentTrackState`, which already owns `voices_`
(`unordered_map<int /*column*/, vector<unique_ptr<TrackState>>>`) - the
one place with visibility into every active voice in the track:
1. `InstrumentTrackState::render(frames, instruments, context)`'s
   pending-events loop (`InstrumentTrackState.h`, the `if
   (!portamento_done)` branch, ~line 57-61) - pattern-driven note-on.
2. `Player::handlePlaybackControlEvent()`'s `PLAY_NOTE` case
   (`Player.cpp:63-65`) - live-performance (Launchpad/keyboard) note-on.

Both currently do `stopVoices(column)` then `addVoice(column,
move(voice))`. Add `InstrumentTrackState::retriggerVoices(int column,
int note_value)` and have both call sites call it in place of their
current `stopVoices(column)` call, immediately before constructing the
new voice. (`NOTE_OFF`/`STOP_NOTE` keeps calling plain `stopVoices()` -
no new identity to compare against, unchanged.)

**`retriggerVoices(column, note_value)` body** - one pass over every
column in `voices_` (not just `column`), so same-identity matches are
caught regardless of which column they live in:
- For every column `K` in `voices_`, for every active voice `v`:
  - If `v->getNoteValue() == note_value`: `v->fastRelease()`. Covers the
    track-wide case, including a same-column exact repeat.
  - Else if `K == column`: `v->stopNote()` - existing natural-release
    behavior, unchanged.
  - Else: leave alone.
- Preserve `stopVoices()`'s existing side effects on a retrigger:
  `column_pressure_.erase(column); broadcastChannelPressure();`.

Structuring it as one unified pass (not "run the scan, then separately
call the old `stopVoices(column)`") is what avoids double-processing the
same voice: without this, a same-column same-identity repeat would get
`fastRelease()`'d by the scan and then *also* hit the old unconditional
`stopNote()` a moment later. Harmless given `stopNote()`'s `isDone()`
guard (a redundant reset of an already-short countdown), but the
unified pass makes "already handled" and "needs a normal stop" mutually
exclusive by construction.

**Can the prior same-identity voice ever be more than one?** Yes -
handle it as a scan that acts on every match, not just the first.
Nothing in `Pattern`/`Note` prevents the same integer value appearing in
two different note-columns of the same row (deliberate unison entry, or
two near-simultaneous Launchpad presses landing the same key in
different chord slots), and `voices_[column]` is itself a vector that
can briefly hold more than one still-releasing voice before
`clearFinishedVoices()` reaps it. `retriggerVoices()` must iterate all
of `voices_` unconditionally rather than early-returning on the first
hit.

**Scope confirmations:**
- **Portamento branch** - removed entirely, see below; no longer a
  concern once removed.
- **Percussion vs. pitched** - no branching needed inside
  `InstrumentTrackState`; see the identity section above.

## Exclusive-class enforcement

A genuinely separate mechanism from identity-based cutoff, not a
variant of it - it matches on the SF2 region's `group` field
(exclusiveClass), not on note value/pitch. Two hi-hat regions choke each
other precisely because they're *different* MIDI keys with the *same*
class; identity-based matching would never catch that pair.

**New accessor:** a way to ask an arbitrary `TrackState*` "which
exclusive classes do you belong to," mirroring `getNoteValue()`:
- `TrackState::getExclusiveClasses()` (new virtual, default: union of
  `getChildren()`'s own results, empty if none) - works transparently
  through a multi-region group, and correctly no-ops for every non-SF2
  instrument without any type-checking in `InstrumentTrackState`.
- `SoundFontVoice::getExclusiveClasses()` override - returns
  `{voiceRegion_->group}` when `voiceRegion_->group != 0`, else empty (0
  is the spec's "no class" sentinel, same convention the existing dead
  FIXME guard already uses).
- Returns a **set of classes, not a single value**: a multi-region
  group's regions could in principle carry different class values per
  region (inconsistently authored velocity layers) - same "don't assume
  there's only one" diligence as the identity-match-count question
  above. In practice GM hi-hat regions all share one class, but the
  accessor shouldn't assume that structurally.

**Sequencing at note-on**, inside the same `InstrumentTrackState`
note-on handling as `retriggerVoices()` (both call sites):
1. `retriggerVoices(column, note_value)` - runs first (needs only the
   raw note value, not the constructed voice).
2. Construct the new voice: `instrument->playNote(...)`.
3. **New:** read the new voice's `getExclusiveClasses()`. If non-empty,
   scan every column in `voices_` (same whole-track shape as
   `retriggerVoices()`, not scoped to the target column - class values
   are only meaningful within one preset/instrument, and "a track holds
   exactly one instrument" makes track-scoping both consistent with the
   identity cutoff *and* the only scoping that's actually correct per
   spec) for any active voice whose `getExclusiveClasses()` intersects
   the new voice's classes, and call `fastRelease()` on each match.
4. `addVoice(column, move(voice))` - as before.

The new voice is never in `voices_` yet when step 3's scan runs, so
there's no self-choke risk from a multi-region group choking its own
sibling regions.

**Termination style:** reuse `fastRelease()` (the same ~10 ms
primitive), not a separate hard-cut - one release mechanism to reason
about/tune instead of two, and avoids reintroducing the click the
identity cutoff explicitly rules out.

**How this composes with identity-based cutoff, concretely** (the
original cross-cutting concern): a voice can legitimately receive
*both* a call from step 1 (`retriggerVoices`'s normal `stopNote()`, if
it had a different identity than the incoming note) *and* a call from
step 3 (`fastRelease()`, if it shares an exclusive class with the
incoming note) - in that order. Not a bug to guard against - it's the
correct precedence: exclusive-class choke is a stricter rule that
should win over "let it ring" whenever both apply, exactly matching
real hi-hat behavior (a closed-hat choke must cut the open hat short
even though they're different pitches and would otherwise ring per the
identity cutoff's own rule). No shared bookkeeping is needed between the
two mechanisms to make this safe - `fastRelease()`/`stopNote()` are
idempotent under repeated calls (the `isDone()` guard), so whichever
mechanism touches a voice first, the second is always a harmless
no-op-or-redundant-reset, never a resurrection.

**FIXME cleanup:** remove (or repoint) the dead stub in
`SoundFontInstrument::playNote()` (`SoundFont.cpp:1427`) once
enforcement moves to `InstrumentTrackState` - it does nothing today and
would be actively misleading left in place once real enforcement exists
elsewhere.

**Implementation note:** `retriggerVoices()` and the exclusive-class
choke pass share the same shape - "walk every column in `voices_`, test
each active voice against a predicate, `fastRelease()` on match." Worth
factoring into one shared private helper taking a predicate, rather than
writing the walk twice.

## Remove the existing portamento mechanism

Unused (no song in `songs/` sets it), only partially implemented (no
actual pitch glide - just envelope-skip-on-retune; see below), and its
presence otherwise forces the retrigger-cutoff wiring to coexist with a
second conditional note-on path. Remove it as part of this pass, before
wiring in `retriggerVoices()`, since removal simplifies that change: the
note-on branch collapses from "if portamento glides in place, else
retrigger" down to a single unconditional path.

**What it currently does, for reference:** on a note-on, if
`portamento_ >= 0.0f` for the track and there's already an active voice
in that same column, it calls `playNote()` again on the *existing*
voice instead of creating a new one. Because `InstrumentVoice::
playNote()`/`SoundFontVoice::playNote()` only initialize the envelope
the first time they're called on a voice, the second call skips
envelope (re)construction - just updates pitch/velocity/gain in place,
no new attack. If no active voice exists in the column, it falls
through to the normal path (identical to portamento being off). The
`portamento_` value itself is stored/round-tripped as a float via the
XML `portamento` attribute, but only its sign (`>= 0.0f`) is ever read -
the magnitude is dead data, never consumed as a glide time/rate
anywhere. Not a real pitch glide, in other words - a no-retrigger
legato toggle.

**Files/sites to remove:**
- `InstrumentTrackState.h:44-61` - delete the whole `bool
  portamento_done = false; if (portamento_ >= 0.0f) { ... }` block and
  the `if (!portamento_done)` wrapper, leaving just the unconditional
  `retriggerVoices(...); addVoice(...)` sequence.
- `InstrumentTrackState.h:285` - remove the `float portamento_;` member.
- `InstrumentTrackState.h:15-16` - drop the `float portamento`
  constructor parameter.
- `InstrumentTrack.h:67` - remove the `float portamento_ = -1.0f;`
  member.
- `InstrumentTrack.cpp:11` - drop the `portamento_` argument from the
  `createState()` call into `InstrumentTrackState`.
- `InstrumentTrack.cpp:25` - remove the `portamento_ =
  input.getFloat("portamento", -1.0f);` load.
- `InstrumentTrack.cpp:43` - remove the `if (portamento_ >= 0.0f)
  output.set("portamento", portamento_);` store.
- `tests/SF2ModulatorTests.cpp:642` - already-committed test constructs
  an `InstrumentTrackState` directly and passes `/*portamento=*/-1.0f`
  positionally; update the call to match the trimmed constructor
  signature.

**Compatibility:** an old song XML with a stray `portamento="..."`
attribute is just silently ignored on load once `loadParameters()` no
longer reads it - same as any other unrecognized attribute in this
codebase's `ParameterSource`. No song in `songs/` uses it, so nothing to
migrate either way.

**Explicitly out of scope, left alone:** the `-Gxx` glide-to-note
pattern effect command documented in `docs/commands.txt` (has no
implementation anywhere in the code - confirmed, no `'G'` effect-command
handling exists) and the related `todo.txt` backlog entries ("PORTAMENTO
UP", "note slide / portamento", "Glide to next node (Portamento?)").
Those describe a different, future, per-note glide feature; deleting
today's crude toggle doesn't foreclose implementing that properly later
- if anything it removes a half-built stand-in that might otherwise be
mistaken for it.

## Files touched (complete list)

- `TrackState.h` - new `fastRelease()` and `getExclusiveClasses()`
  virtuals (both default-recurse into children).
- `InstrumentVoice.h` - new `fastRelease()` override (`{ stopNote(); }`);
  no `getExclusiveClasses()` override needed (inherits the empty-by-
  default base, correct for every non-SF2 leaf).
- `SoundFont.cpp` - `SoundFontVoice::fastRelease()` and
  `::getExclusiveClasses()` overrides; retire `stopNoteQuick()`; remove/
  repoint the dead FIXME in `SoundFontInstrument::playNote()`.
- `InstrumentTrackState.h` - new `retriggerVoices(column, note_value)`
  and exclusive-class choke pass (possibly sharing one predicate-based
  walk helper); remove the portamento branch/member/constructor
  parameter.
- `InstrumentTrack.h` / `InstrumentTrack.cpp` - remove `portamento_`
  member and its XML load/store/constructor-argument plumbing.
- `Player.cpp` - `PLAY_NOTE` case calls `retriggerVoices(column,
  midi_note)` instead of `stopVoices(column)`.
- `tests/SF2ModulatorTests.cpp` - update the already-committed
  `InstrumentTrackState` constructor call site for the trimmed
  signature.

## Verification

1. `cmake --build build -j` clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` 100% pass.
3. New unit tests (reusing the existing `writeMinimalSf2()` fixture
   builder in `tests/SF2ModulatorTests.cpp`):
   - Same-identity retrigger: play note N, then retrigger note N again
     before the first finishes releasing - assert the first voice
     reaches `!isActive()` within ~`TSF_FASTRELEASETIME` (not its full
     authored release time).
   - Different-identity replace: play note N in a column, then a
     different note M in the same column - assert the old voice is
     *not* fast-released (still audible/ringing per its normal release
     envelope shape, i.e. takes its full authored release time to reach
     `!isActive()`, not the fast one).
   - Cross-column same-identity: note N in column 0, then note N again
     in column 1 (chord-slot scenario) - assert column 0's voice gets
     fast-released even though it's a different column.
   - 31-EDO cluster non-self-cut: a chord of adjacent-but-distinct
     integer note values entered together - assert none of them
     fast-release each other.
   - Exclusive class: two regions/presets sharing a non-zero
     `exclusiveClass` (`GenSpec{57, N}` in the test fixture builder),
     different key ranges (different note values) - triggering one
     after the other fast-releases the first even though their note
     values differ. A third region with `exclusiveClass = 0` (or
     omitted) triggered alongside either must *not* be touched.
   - Composition: a same-track voice that both differs in identity
     *and* shares an exclusive class with the new note - assert it ends
     up fast-released (the stricter rule wins), not left on its normal
     release.
   - Multi-region group: fast-releasing a group correctly cascades to
     every child region (reuse the `region_count`-based fixture pattern
     already in the test file).
4. Manual: hold/repeat a single note rapidly on a real GM pad patch and
   confirm the voice count in the status bar (`InfoLine`) stays bounded
   instead of climbing: with a GM drum kit, strike an open hi-hat then
   a closed hi-hat and confirm the open hat audibly cuts off.
