# Arpeggiator timing fixes

Three reported problems with `ArpeggiatorState` (`ArpeggiatorState.h`/`.cpp`),
all timing-related, none touching `rebuildStepPool()`'s pitch-ordering logic
itself:

1. **Live chord entry starts from whichever key hits first, not the root.**
   Confirmed live-only (pattern playback is fine as-is - a pattern row's
   several note columns already land in the same `pending_events_` frame and
   get batched before the first step fires; note-delay columns give explicit
   control over ordering there if ever needed). Holding a chord by hand,
   notes physically land at different instants. `noteOn()`'s `was_empty`
   check triggers the very first step (`step_index_ = -1;
   samples_until_next_step_ = 0;`) the instant the chord goes from empty to
   non-empty - at that moment the pool contains only whichever single note
   arrived first, so that note (not necessarily the lowest/"root") becomes
   step 0, regardless of mode.

2. **Song playback: chord changes drift and aren't seamless.** When an
   `arpeggiatorTrack` pattern holds a chord across several rows and a later
   row changes it (typically by re-using the same note columns - the usual
   tracker idiom, not an explicit note-off/note-on pair), `noteOn()`'s
   `was_empty` is already `false`, so the pool is rebuilt but
   `step_index_`/`samples_until_next_step_` are left alone (existing,
   deliberate "adding a note doesn't reset the step position" behavior -
   see the class comment and
   `arpeggiator_state_steps_ascending_through_held_chord_with_gaps`). The
   step clock is a free-running countdown that only ever gets re-zeroed on a
   from-empty transition, so a mid-cycle chord change takes effect only
   whenever the *previous* chord's already-scheduled step boundary happens
   to elapse - anywhere from 0 samples to a full step length late, and that
   offset varies with wherever in the cycle the row happens to land. Not
   literally growing drift, but exactly "not seamless" / "sometimes early,
   sometimes late" from a listener's perspective. A plain (non-arp) track's
   pattern-driven note-on already always retriggers
   (`InstrumentTrackState::retriggerVoices()`) - every pattern row is a
   deliberate, exactly-timed onset, never a "sustain" - the arpeggiator
   doesn't yet apply that same convention to its own step clock.

3. **Seeking, then resuming, leaves the arp out of phase.** Player keeps
   rendering every `TrackState` (including `ArpeggiatorState`) every block
   regardless of `isPlaying()`, so envelopes/tails/the arp's own step timer
   all keep advancing while stopped - whether that's actually desired is an
   open question (see "Related, out-of-scope issue" below), not something
   this plan settles; it's simply today's behavior, left as-is here. But
   `SongState::setPosition()` (the landing spot for both
   `MOVE_POSITION`/`SET_POSITION` - reached only while stopped, per
   `Player.cpp`'s own comment - and internally for `ZBxx` pattern-break
   jumps during real playback) only touches `sample_pos_`/`absolute_pos_`;
   nothing tells any track's internal clock that the transport just jumped.
   `ArpeggiatorState::samples_until_next_step_` keeps counting down from
   wherever it was, so after a seek-and-resume the first step (and every
   one after it) lands at an arbitrary offset from the row it's actually
   meant to align with.

## Design

### `NoteOrigin` (new, small header)

```cpp
enum class NoteOrigin { LIVE, PATTERN };
```

Threaded through `InstrumentTrackState::noteOn()`'s existing virtual
signature (an added trailing parameter, no default - both call sites become
explicit). Base `InstrumentTrackState::noteOn()` ignores it entirely (its
`retriggerVoices()`-based behavior is already origin-independent).
`noteOff()`/`notePressure()` are untouched - neither problem here involves
them.

- `InstrumentTrackState::render(frames, instruments, context)`'s
  pending-events loop passes `NoteOrigin::PATTERN`.
- `Player::handlePlaybackControlEvent()`'s `PLAY_NOTE` case passes
  `NoteOrigin::LIVE`.

### Fix 1 - live chord-collect window

`ArpeggiatorState` gets a short, fixed real-time window (proposed: 30ms,
sample-rate-derived, a `static constexpr` in the `.cpp` - not tempo-relative,
this is about human hand-timing, not song timing). On a `LIVE` `noteOn()`
that transitions the chord from empty to non-empty, instead of the current
immediate trigger (`samples_until_next_step_ = 0`), arm the window
(`samples_until_next_step_ = kChordCollectWindowSamples`) and mark that the
*next* `triggerNextStep()` is still an onset (reuses `step_index_ == -1` as
that marker, unchanged). Further `LIVE` notes arriving before the window
elapses join the pool the same way they already do (`rebuildStepPool()`,
`was_empty` now `false`) and do **not** re-arm/extend the window - a slow
hand-roll still resolves, just against whatever's landed by the deadline.
When the window elapses, `triggerNextStep()` fires exactly as it does today,
now picking step 0 out of a pool that (for any chord pressed within ~30ms of
itself) has already collected every note - so the true lowest (UP) /
highest (DOWN) note starts the pattern, independent of press order.

`PATTERN`-origin onsets keep the current immediate-when-possible trigger,
now routed through the same `resyncIfNothingRinging()` PATTERN's other
resync points use (see Fix 2's "never cut an already-sounding note"
revision) rather than forcing it unconditionally - a `was_empty`
transition can still land while an earlier release's tail is ringing, and
song playback must never cut that either. `LIVE`'s own branch is simpler
and deliberately stays unconditional (always the chord-collect window,
never deferred by a still-ringing tail): live playing is allowed some
imprecision a song's own playback is not, and there's no separate ringing
step to protect here in the first place - a `was_empty` transition only
happens when `held_notes_` genuinely was empty a moment ago.

### Fix 2 - pattern-driven chord changes resync only when the whole chord is restated

Resyncing on *every* `NoteOrigin::PATTERN` `noteOn()` unconditionally is too
coarse: a pattern row can either restate the whole held chord (every column
that's currently sounding gets a fresh note this row - the scenario the
reported drift/lag came from) or edit just one voice while the rest keep
sustaining from earlier rows untouched (dropping one note and replacing it
with another, everything else held). Only the first case should resync -
the second should behave exactly like the existing live "adding a note
doesn't reset" continuation, or a mid-cycle restart would be audible (and
wrong) every time a single voice of an otherwise-sustained chord changes.

Telling these apart needs the *whole row's* picture, not a single column's
own event - a row's several note-on calls already land in one
`pending_events_` batch (same frame, all dispatched back-to-back before any
render happens), but `noteOn()` today only ever sees one column at a time.
`InstrumentTrackState` gains a new virtual, `endPatternRow()` (default
no-op), called once per processed frame from `render(frames, instruments,
context)`'s pending-events loop, right after that frame's whole batch of
note-on/off/aftertouch calls has been dispatched:

```cpp
if (i == it->first) {
  for (auto & ev : it->second) { ... noteOn()/noteOff()/notePressure() ... }
  it = pending_events.erase(it);
  endPatternRow();
}
```

`ArpeggiatorState::noteOn(..., NoteOrigin::PATTERN)` records the touched
column id into a small per-row scratch list (`touched_columns_this_row_`)
instead of deciding anything immediately. `ArpeggiatorState::endPatternRow()`
then makes the one, whole-row decision: if every note currently in
`held_notes_` has a matching id in `touched_columns_this_row_` (the row
refreshed the entire chord, not just part of it), *try* to resync - see
"Revision: never cut an already-sounding note" below for why this isn't an
unconditional `step_index_ = -1; samples_until_next_step_ = 0;` any more.
Otherwise leave the step clock alone, exactly like today.
`touched_columns_this_row_` is cleared unconditionally at the end of
`endPatternRow()`, whichever branch ran. Not called at all from the live
path (`Player.cpp` never calls it), so it never affects live chord-holding.

The `was_empty`-triggered reset in `noteOn()` is untouched in shape and
still fires for both origins - a chord starting from total silence should
always restart clean, live or pattern-driven - though PATTERN's own version
of it goes through the same revised, never-cut resync path described below
too.

`endPatternRow()` is called unconditionally for every processed frame,
including a row that only contains note-offs (no note-ons at all) -
deliberately not special-cased, since the general check already resolves
correctly for that case without help: `noteOff()` never populates
`touched_columns_this_row_` (only `noteOn(..., PATTERN)` does), so a
note-off-only row leaves it empty, and the "every held note was touched"
check then splits exactly the right way on its own - if some notes survive
the note-off(s), the check is `false` (non-empty `held_notes_` against an
empty touched list) and nothing resyncs; if the note-off(s) empty the chord
out entirely, the check is vacuously `true` over the now-empty
`held_notes_`, but that's inert - `renderVoices()`'s trigger check is
gated on `!held_notes_.empty()` first, so the resync it sets never actually
gets read before some future `noteOn()`'s own `was_empty` branch overwrites
it anyway (and `rebuildStepPool()` already sets `step_index_ = -1` on an
empty pool independently). Worth a code comment at the vacuous-true branch
so it doesn't read as an oversight later.

#### Revision 1: never cut an already-sounding note (also confirmed wrong by hands-on testing)

The design above, as first implemented, forced the resync unconditionally
(`step_index_ = -1; samples_until_next_step_ = 0;`) whenever `full_replace`
was true - which fixed the reported lag, but introduced a worse bug: if the
old chord's current step was still ringing (its gate not yet closed - the
common case whenever `gate` is close to `noteDuration`) when the row hit,
the forced immediate trigger created a *second* voice right alongside the
still-sounding one. Most audible exactly when the old and new chords share
a note (e.g. the same root, changed voicing above it): that note briefly
played twice at once.

First fix attempt: cut the old voice short (`fastReleaseVoices()`, a new
`InstrumentTrackState` method paralleling `retriggerVoices()`'s existing
`fastRelease()` use) right before forcing the resync. This was also wrong,
and reverted: **a note that has already started playing must never be cut
short** - if it's too late to change what's already sounding, the
correct move is to leave it alone and make sure whatever's *scheduled
next* reflects the new chord, not to truncate it. This applies without
exception to song/pattern playback; some imprecision from a similar cause
is tolerable for live playing (see Fix 1's own note on this), but for a
song, nothing gets cut and nothing gets silently skipped.

Final (shipped) design: a new private method,
`ArpeggiatorState::resyncIfNothingRinging()`, is the one place every
PATTERN/transport-driven resync point (`noteOn()`'s `was_empty` branch for
`PATTERN`, `endPatternRow()`'s full-row replace, `resyncPlayhead()`)
funnels through, rather than each forcing the two member variables
directly:

```cpp
void ArpeggiatorState::resyncIfNothingRinging() {
  if (pending_gates_.empty()) {
    step_index_ = -1;
    samples_until_next_step_ = 0;
    return;
  }
  // still ringing - do nothing beyond the clamp below. The resync request
  // itself is not remembered or applied later (see Revision 2 below for
  // why an earlier version that did try this was reverted).
  for (auto & gate : pending_gates_) {
    samples_until_next_step_ = std::max(samples_until_next_step_, gate.samples_remaining);
  }
}
```

`closeElapsedGates()` is the only thing that ever gets to stop a step's
voice, on its own originally-scheduled deadline - resyncing never bypasses
that. `step_pool_` is still always current by the time any of this runs
(every `noteOn()` call rebuilds it unconditionally via `rebuildStepPool()`),
so even when this is a no-op, the in-flight step's own natural advance/
gate-close - whenever that next happens - picks its note from the *current*
chord, never stale data; the chord update is never lost, just not always
applied instantaneously. The `samples_until_next_step_` clamp itself
exists so the *ordinary* `samples_until_next_step_ <= 0` trigger check
(`renderVoices()`) can't fire early and double the still-ringing step -
see the `.cpp` for the exact reasoning (it matters for `noteOn()`'s
`was_empty` branch specifically, where this value can otherwise be stale).

#### Revision 2: a deferred resync being applied later, tried and reverted (real drift)

Confirmed by hands-on testing ("if I seek the song, the arp is no longer
in sync") that silently doing nothing when something's ringing meant a
seek often had no effect at all, since something is ringing at the moment
of a resync fairly often (any config where `gate` is close to
`noteDuration`, including the default `gate = noteDuration = 1`). Tried
fixing it by *remembering* the resync (a `resync_pending_` flag) and
applying it - as a fresh restart at the pool's own first index - the
instant the still-ringing step actually closed, rather than waiting for
whatever the old, pre-resync schedule's own next boundary was.

This was also reverted, on further hands-on testing: it introduced its own
real, reported drift between the arp and the song. Applying a deferred
resync always meant restarting fresh, landing at a moment that was only
ever meaningful relative to *when the ringing happened to stop* - not one
that meant anything relative to the row grid. `endPatternRow()`'s own
full-chord-replace made this worse in practice, since it can fire on
essentially every row of an ordinary sustained chord (any row that
restates the same notes) - each one queuing up another fresh, unrelated
restart once its own predecessor's ringing step finally closed. Simply
doing nothing (the shipped design above) is the safer default: this class
has no notion of the pattern's own note-event history, only whatever the
most recent row said, so it can't aim a deferred resync anywhere
meaningfully better than "wherever the sequence already was" - see
"Related, deferred future work" below.

### Fix 3 - `resyncPlayhead()` on transport start

New virtual on `TrackState` (default recurses into children, same shape as
`isActive()`):

```cpp
virtual void resyncPlayhead() {
  for (auto & [ id, child ] : getChildren()) child->resyncPlayhead();
}
```

`ArpeggiatorState` overrides it as a thin call to `resyncIfNothingRinging()`
(see Fix 2's "never cut an already-sounding note" revision) - the same
"trigger fresh on the very next render()" reset `noteOn()` already uses for
a from-empty chord, but, like every other resync point in this class,
never at the cost of cutting a step that's still genuinely ringing.
Deliberately leaves `held_notes_`/`pending_gates_` untouched either way: a
chord still held (e.g. a live take paused mid-arpeggio) keeps sounding
through the transition exactly as before; only the step clock's own phase
gets pinned back to the transport, and only once nothing's in the way of
doing that cleanly.

**Revised call site, round 1** (originally planned for
`SongState::setPosition(int)` - confirmed wrong by hands-on testing):
`setPosition()` is also what `MOVE_POSITION`/`SET_POSITION` drive for
*plain cursor navigation while stopped* (`Player.cpp`'s "Row navigation
while stopped" comment) - every arrow-key press in the pattern editor while
the song is paused fires one of these, so resyncing there yanked a
still-running arpeggiator back to step 0 on every such keypress, not just
an actual restart. Hooked into `PlaybackControlEvent::PLAY` instead
(`Player.cpp`, right after `state_.setIsPlaying(true)`) - the one event
that only ever fires on an actual stopped-to-playing transition, never on
mere cursor movement. `jumpToPatternBreak()`'s own internal `ZBxx` handling
(via `setPosition()`, during already-active playback) is *not* hooked - a
mid-song break isn't a stopped-to-playing transition, and the reported
problem never involved it.

**Revised call site, round 2** (still too aggressive - also confirmed
wrong by hands-on testing): calling it unconditionally on every PLAY yanked
a still-cycling arpeggiator back to step 0 even when resuming from *exactly
the same row* it was paused at, which is only actually correct when
resuming means "start this phrase over" - i.e. the playhead itself moved
during the stop. Gated behind `SongState::resyncPlayheadAfterStop()`
instead: `PlaybackControlEvent::STOP` snapshots `getPositionEditSeq()` via
`notePlaybackStopped()`, and `PLAY` only calls `resyncPlayhead()` if that
value has since changed - `getPositionEditSeq()` already bumps on every
`setPosition()`/`movePosition()` call (real per-row playback advance
included), but per that comment's own reasoning `movePosition()` only ever
runs from `renderBlock()`'s `if (isPlaying())`-guarded loop, so nothing
bumps it while genuinely stopped except an explicit
`SET_POSITION`/`MOVE_POSITION` - i.e. exactly a real seek, not the
passage of stopped time itself.

**A real wiring bug, not a design gap** (found and fixed alongside the
above, separately from anything below): `Player.cpp` was still calling the
old, unconditional `state_.resyncPlayhead()` directly - round 2's
`resyncPlayheadAfterStop()`/`notePlaybackStopped()` pair was added to
`SongState.h` and covered by `tests/ResyncPlayheadTests.cpp` (which drives
`SongState` directly), but the actual `PlaybackControlEvent::PLAY`/`STOP`
handlers were never updated to call them. So the position-gating from
round 2 was never actually active in the running app at all - every real
PLAY unconditionally resynced, exactly the round-2 bug, still live. Found
only by re-reading `Player.cpp` for an unrelated reason - a reminder that a
fix living only in `SongState.h` plus a test that talks to `SongState`
directly doesn't prove the feature is reachable from the real
control-event path. Fixed and **kept** (`Player.cpp`'s PLAY/STOP cases now
call `resyncPlayheadAfterStop()`/`notePlaybackStopped()`) - this one was a
genuine bug, not a design choice that later needed reverting.

**Tried and reverted: recovering "the correct step" for a seek, not just
restarting.** Confirmed wrong by hands-on testing twice over - first as
its own regression ("it resets when I seek, but it should consider the
song position and pick the correct arp position too"), then again once
implemented ("the resync with correct step recovery doesn't work very
well... the arp position drifts and doesn't match the song"). The attempt:
thread the transport's row position down into `resyncPlayhead(int
absolute_row)`, and compute the step an uninterrupted cycle would actually
be at by that row as a closed-form function of "how many step-durations
have elapsed" (reimplementing `advanceIndex()`'s per-mode sequence
directly, anchored at the song's row 0) instead of always restarting at
the pool's own first index.

This looked sound in isolation (and passed dedicated tests for it - see
git history around this plan) but caused real drift once actually used.
The fundamental problem: this class has no notion of the pattern's own
note-event *history* - only whatever the most recent row said - so
anchoring "which step is correct" purely at the song's row 0 silently
assumes the currently-held chord has been running, unbroken, since exactly
that anchor point. Any chord that started later, was replaced, or had a
column edited along the way invalidates the assumption, and there's no way
for this class to tell the difference from a single seek event. This is
the same fundamental issue Revision 2 above ran into for the *deferred*
resync case - both attempts were trying to make this class smarter about
"where should the arp be" than the information actually available to it
supports. **Reverted entirely** - `resyncPlayhead()` is back to no
arguments, and always restarts at the pool's own first index, exactly like
every other resync point in this class (see "Related, deferred future
work" below for what an actually-correct version would need).

## Files touched

- `NoteOrigin.h` (new)
- `InstrumentTrackState.h` - `noteOn()` signature, new `endPatternRow()`
  virtual, pending-events loop's call sites for both
- `Player.cpp` - `PLAY_NOTE` call site (`NoteOrigin::LIVE`);
  `PlaybackControlEvent::PLAY`/`STOP` call `resyncPlayheadAfterStop()`/
  `notePlaybackStopped()` (the wiring bug fix above)
- `Arpeggiator.h`/`.cpp` - untouched (no new authored parameter; window
  length is a fixed constant, not user-configurable, at least initially)
- `ArpeggiatorState.h`/`.cpp` - `noteOn()` signature/logic, new
  `endPatternRow()`/`resyncPlayhead()` overrides plus the private
  `resyncIfNothingRinging()` they (and `noteOn()`'s own `PATTERN` branch)
  share, `touched_columns_this_row_`, chord-collect-window constant/
  accessor; also fixes the stale "Phase 2, not yet wired" comment in
  `render()`'s doc comment (pattern playback has been wired since
  `7a3aa94` - the comment predates that but survived the commit that made
  it wrong)
- `TrackState.h` - new `resyncPlayhead()` virtual
- `SongState.h` - `position_edit_seq_at_stop_`, `notePlaybackStopped()`,
  `resyncPlayheadAfterStop()`
- `tests/ArpeggiatorStateTests.cpp`, `tests/ResyncPlayheadTests.cpp` (new),
  `tests/CMakeLists.txt` - see Tests below

Tried during development and reverted, not present in the final diff:
`InstrumentTrackState::fastReleaseVoices()` (a cut-the-old-voice-short
helper - Revision 1 above), `resync_pending_`/the deferred-resync-catch-up
machinery (Revision 2 above), `computeStepForRow()`/row-aware
`resyncPlayhead(int)` (immediately above).

## Tests

- `tests/ArpeggiatorStateTests.cpp`: existing `noteOn()` calls gain an
  explicit `NoteOrigin::LIVE` argument (they're exercising the live-audition
  API shape, per the file's own top comment). New cases:
  - live chord-collect window: notes arriving within the window in a
    non-root-first order still start on the pool's true step 0.
  - a note arriving *after* the window closed does *not* retroactively
    join the already-triggered step 0 (only affects future steps) -
    boundary check.
  - pattern-origin (`NoteOrigin::PATTERN`) chord change that refreshes every
    held column mid-cycle, with nothing currently ringing, resyncs
    immediately rather than waiting for the old step to elapse.
  - pattern-origin chord change that only refreshes *some* columns (one
    note dropped and replaced, the rest untouched) does *not* resync - the
    step clock keeps running exactly as if nothing happened.
  - the reported "root note played twice" regression itself: a
    full-chord-replace whose new chord shares a note with the old one,
    while the old chord's step is still ringing (legato) - the shared note
    is never doubled, and once the old step's own natural boundary
    arrives, the *new* chord's data is what plays (the chord update was
    never lost, just applied at the next natural boundary rather than
    instantaneously - the stepper otherwise continues its own unbroken
    sequence, not reset to step 0).
  - `resyncPlayhead()` mid-chord, with nothing currently ringing, forces the
    next `renderVoices()` call back to step 0 without dropping
    `held_notes_`.
  - `resyncPlayhead()` arriving while a step is still ringing (legato) does
    not cut that note, and does not force a restart once the sequence does
    eventually advance either - it just continues exactly as if the resync
    had never been requested (the counterpart to Revision 2's own revert
    above - this is the behavior that replaced the "remember and catch up
    later" one that caused drift).
- `tests/ResyncPlayheadTests.cpp` (new): `SongState::resyncPlayheadAfterStop()`'s
  own gating, driven end-to-end through a real `Song`/`SongState` (the
  `ArpeggiatorState`-level test above only exercises `resyncPlayhead()`
  directly, bypassing the "did the position actually move while stopped"
  decision) - one case confirming a same-position stop/resume leaves a
  still-cycling arpeggiator alone, one confirming a stop/seek/resume
  resyncs it.
- `tests/RenderTests.cpp`: not extended in this pass - the existing
  single-onset `arpeggiator_pattern_chord.xml` end-to-end test still
  covers the pattern-driven path; a second-chord-row end-to-end case would
  mostly duplicate what `tests/ArpeggiatorStateTests.cpp`'s own
  pattern-origin cases already check more directly.

## Explicit non-goals

- Not changing "the arp keeps stepping while playback is paused" - this was
  never actually confirmed as desired, just left alone as out of scope for
  this plan (see "Related, deferred future work" below).
- Not making the chord-collect window user-configurable - fixed constant for
  now; revisit if 30ms turns out wrong in practice.
- Not touching `rebuildStepPool()`'s pitch-sort or `advanceIndex()`'s
  mode logic - none of the three problems are about step *ordering*, only
  about *when* the first/next step fires.
- Not building the scheduling lookahead a genuinely correct chord-change/
  seek resync would need (tried twice in smaller forms - Revision 2, and
  the row-aware `resyncPlayhead()` attempt - both reverted; see "Related,
  deferred future work" below) - deliberately deferred to a separate,
  later phase.

## Related, deferred future work

**Envelope/effect state also drifts while stopped**, a related but
separate issue from anything in this plan. Every `TrackState` (not just
`ArpeggiatorState`) keeps rendering, and therefore keeps advancing
whatever internal state it has (envelopes, LFOs, effect tails, this
class's own step timer), for the entire time playback is stopped -
confirmed real, not something this plan changes. Whether that's actually
the right behavior is genuinely undecided (not "confirmed intentional" - a
prior draft of this plan mischaracterized it that way). The same
underlying class of bug this plan works through for the arpeggiator's step
clock also affects any voice with a real envelope: its envelope position
keeps progressing during a stop the same way the row/pattern clock does
not, so a long-held note's envelope can be well past where the (frozen)
transport position implies it should be by the time playback resumes - see
`docs/known_bugs.md`. Two ways to actually resolve this, not attempted
here:

- Freeze rendering entirely while stopped (skip the per-track render loop,
  not just the note-scheduling/position-advance section, in
  `SongState::renderBlock()`) - trivially correct (nothing can drift if
  nothing advances) but a real behavior change: no more tails/decays
  continuing to ring after Stop, which is arguably a deliberate, valued
  feature today, not just an oversight.
- Extend the resync approach this plan takes for the arpeggiator to
  envelopes generally - much bigger in scope (would need every envelope
  implementation to expose some kind of "re-anchor to the transport"
  operation) and still runs into the same "never cut/skip a note" bar this
  plan already had to work through.

**A genuinely correct chord-change/seek resync needs the arp to know the
pattern's own note-event history, not just react to the most recent row.**
This is the root cause behind *both* reverted attempts above (Revision 2's
deferred-resync catch-up, and the row-aware `resyncPlayhead(int)`): neither
had enough information to aim a resync anywhere reliably better than
"restart at the pool's own first index." Both symptoms - a chord-change
that lands mid-ringing-step, and a seek to an arbitrary row - are really
the same underlying gap: `ArpeggiatorState` only ever sees pattern data one
row at a time, exactly when `InstrumentTrackState::render()`'s
pending-events loop happens to reach it, with no way to look ahead or
reconstruct what came before a seek landed.

A real fix needs the stepper's own events to be knowable independent of
realtime playback order - e.g. song playback pre-scheduling/pre-creating
the arpeggiator's step events ahead of time (from the full pattern data,
not just "whatever row is due right now"), so seeking to any row can
consult that schedule directly instead of trying to infer a phase from a
formula. This also matters for its own sake as patterns get more complex
(chords that change shape via partial edits, note-delay columns, multiple
overlapping arpeggiator tracks, etc.) - the further this class's stepping
logic drifts from "one simple, closed-form function of elapsed rows," the
harder any resync-time recovery gets, which is exactly what both reverted
attempts ran into even in this codebase's comparatively simple current
state. Real scope - a scheduling/pre-creation layer, not a fix localized to
`ArpeggiatorState` - and deliberately out of scope for this plan.
