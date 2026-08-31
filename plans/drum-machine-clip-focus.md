# Drum machine: scope preview to what's visible, add a clip picker

## Context

Two related problems, from the user directly:

1. "The drum machine is playing the active scene all the time... it must
   go. Some kind of preview mode... is useful." Traced to
   `LaunchpadManager.cpp`'s free-running `audition_clock_` (pre-existing,
   not introduced this session): whenever the transport is stopped and
   Record Arm is off, it loops through **every** `DrumMachineTrack` in the
   whole song via `triggerAuditionStep()`, firing real audio continuously,
   regardless of whether anyone's actually looking at any of them. The
   user's own resolution: "Drum machine preview should run whenever the
   drum machine sequencer is visible in Launchpad" - i.e. scoped to
   exactly the one track whose step grid is actually being shown (a
   connected device in `GridMode::NOTES` with that track assigned - "every
   device follows fallback_track_index", so there's only ever one
   candidate track at a time), not the whole song. `triggerClipStep()`
   (Session-view's own separate live-clip-launch auditioning,
   `triggered_pattern_by_track_`/`queued_pattern_by_track_`) is a
   different, intentional feature and stays untouched.

2. "How do I configure the notes of a specific drum machine clip? ...
   how do we get it visible for a specific clip? We need some
   functionality to pick a clip for editing." Today the step grid (and
   `PatternEditor`'s own note columns) always show whatever's active at
   the *current row* (background, or a placed instance -
   `ArrangementOps.h`'s `resolveEditTarget()`/`resolveReadTarget()`) -
   there's no way to look at/edit a specific clip's own content
   independent of scene position, and (per an earlier finding this same
   session) no `insert-clip` yet either, so even placing an instance
   somewhere to edit through is awkward. The fix here is narrower and more
   directly useful: a **clip focus** - pick a clip explicitly (from
   `SessionView`, which already lists them) and both the step grid and
   `PatternEditor` show/edit *that* clip's own content directly, for
   whichever track it belongs to, regardless of row/instance state.

## Approach

### Clip focus - `Controller`

Per-buffer live state (same shape as `recording_track_id_`/
`pattern_selection_active_` - a live mirror plus a `map<string, T>` swapped
in `saveActiveBufferState()`/`loadActiveBufferState()`), keyed *within* a
buffer by `track_id` (a `std::unordered_map<int, std::string>` of
`track_id -> clip id`, empty entry = no focus for that track - independent
per track, so focusing a clip on the drum track doesn't disturb whatever's
focused on another):

- `std::string getFocusedClip(int track_id) const` - empty if none.
- `void setFocusedClip(int track_id, const std::string & clip_id)`.
- `void clearFocusedClip(int track_id)`.
- `void toggleFocusedClip(int track_id, const std::string & clip_id)` -
  clears if `clip_id` is already the focused one, else sets it - the
  primitive `SessionView`'s own Enter binds to.

Never serialized (XML round-trip untouched) - purely live editing-session
state, same category as the other four per-buffer maps.

### `ArrangementOps.h`/`.cpp` - the one place resolution changes

Add an optional trailing parameter to both functions rather than a
parallel set or a branch duplicated at every call site - keeps every
existing call site a one-line change, and keeps "what does track_id's
content mean right now" centralized in this one file, matching how these
two functions are already positioned:

```cpp
EditTarget resolveEditTarget(Song & song, Scene & scene, int track_id, int row, const std::string & focused_clip_id = "");
ReadTarget resolveReadTarget(const Song & song, const Scene & scene, int track_id, int row, const std::string & focused_clip_id = "");
```

When `focused_clip_id` is non-empty: look it up in `song.getClips(track_id)`
by id (the same linear scan `resolveInstanceAt()` already does for the
same reason - a clip's *position* can drift under reorder/delete, its id
can't) and resolve directly against its own leaf `Pattern`, row wrapped by
the clip's own length - bypassing `resolveInstanceAt()`'s scene/instance-
event lookup entirely, since a focused clip is being edited in isolation,
not "wherever it happens to be placed". Falls through to today's ordinary
position-based resolution if the id doesn't match anything (stale/deleted
- same resilience precedent `resolveInstanceAt()` already has for a
dangling instance reference). `ReadTarget` gains `bool is_focused_override`
alongside the existing `is_instance`/`clip_index` so a caller can tell a
focus override apart from a real placed instance if it needs to.

### Call sites - pass the focus through, no other logic changes

All ten existing `resolveEditTarget()`/`resolveReadTarget()` call sites
(`Controller.cpp` x2, `LaunchpadManager.cpp` x4, `PatternEditor.cpp` x3)
add `controller.getFocusedClip(track_id)` (or `getFocusedClip(track_id)`
where a `Controller&` is already in scope) as the new trailing argument -
mechanical, one line each, no other change needed at any of them.

### Preview scoping - `LaunchpadManager::refresh()`

- Before the audition-clock block: a quick pass over `devices_` -
  `bool step_grid_visible = any device with grid_mode == GridMode::NOTES`.
  Combined with `is_drum_machine` for the shared assigned track
  (`fallback_track_index`'s own track, resolved the same way the
  per-device loop already does) to decide whether `triggerAuditionStep()`
  should fire at all this step.
- `triggerAuditionStep()`'s own signature simplifies from `const
  vector<int> & track_ids` (loop every track, filter to drum machines
  internally) to a single `int track_id` - it only ever has one real
  candidate now (the assigned track), so the internal type-check/loop
  goes away along with the vector.
- The call site (both the "(re)start" and "advance" branches of the
  audition-clock block) wraps just the `triggerAuditionStep(...)` call in
  `if (step_grid_visible) { ... }` - `triggerClipStep(...)` right next to
  it stays unconditional under the existing `audition_active` check,
  untouched (separate feature, not what's being scoped here).
- `triggerAuditionStep()` itself also gains the `focused_clip_id`
  parameter (threaded from the same per-track focus map) so previewing
  the assigned track auditions whatever's actually focused, not just
  whatever the current row happens to resolve to.

### The picker - `SessionView`

- **Enter** on a clip row: `getController().toggleFocusedClip(track_id,
  clip.getId())` - the first real command/keybinding this widget gets
  (its original read-only pass deliberately had none).
- Visual: the focused clip's own row gets a distinct marker (e.g. a
  leading indicator glyph before the loop glyph) so it's visible which
  clip (if any) is focused per track, independent of cursor position.
- `setStatus(...)` on toggle - "Editing clip: <name>" / back to normal -
  since there's no deeper visual integration in this pass (see below).

### Deliberately out of scope for this pass

- **`PatternEditor`'s own row-rendering visual treatment** for a focused
  clip (the "ear"/half-block tint machinery Phase D built is intricate -
  `unwrapped_row == 0` leading-row logic assumes a real instance's own
  `start_row`, which a focus override has no equivalent of). `PatternEditor`
  *edits* the focused clip correctly (via the `resolveEditTarget()`
  change above) but doesn't yet show a distinct visual cue for it beyond
  the status-line message `SessionView`'s toggle already gives - a
  follow-up, not folded in here given how easy it'd be to introduce a
  subtle rendering bug in that existing logic without dedicated time to
  verify it.
- `insert-clip` (placing a brand-new instance from the keyboard) - still
  doesn't exist; unrelated to clip *focus*, which needs no instance at
  all.

## Files

- `src/Controller.h`/`.cpp` - the five-per-buffer-map pattern gains a
  sixth (`focused_clips_`), `getFocusedClip()`/`setFocusedClip()`/
  `clearFocusedClip()`/`toggleFocusedClip()`.
- `src/model/ArrangementOps.h`/`.cpp` - the optional parameter and
  focused-clip resolution branch, `is_focused_override`.
- `src/ui/PatternEditor.cpp`, `src/launchpad/LaunchpadManager.cpp` - the
  ten call-site updates plus the audition-scoping changes.
- `tests/ArrangementOpsTests.cpp` - new tests: a focused clip resolves
  correctly (edit + read) regardless of row/instance state, falls back
  cleanly when the focused id is stale, and doesn't affect an unrelated
  track's own normal resolution.

## Verification

- `cmake --build build -j` clean, `ctest --test-dir build
  --output-on-failure` 100%.
- Manual check via the pty harness (`tools/e2e/harness.py`, this
  session's established pattern, not the ALSA/fake-Launchpad one - that
  layer is confirmed unreliable in this sandbox regardless of code
  changes): open `SessionView`, press Enter on a clip row, confirm the
  status message and the row's own new marker; switch to `PatternEditor`
  and confirm typing a note writes into that clip's own leaf `Pattern`
  (not the background) even while the cursor sits on an unrelated row.
