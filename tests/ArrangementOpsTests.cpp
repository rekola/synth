#include "TestFramework.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/Song.h"
#include "../src/model/Arrangement.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/SampleContent.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/instruments/InstrumentProvider.h"
#include <filesystem>

using namespace std;

// A looping clip plays on until the track's next event, so placing one
// clears only its first pass - never the rest of the song.
TEST(place_clip_instance_looping_clears_only_its_first_pass) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 5, "other"); // within the first pass
  arrangement.setInstance(track_id, 40, "other"); // well past it

  placeClipInstance(song, track_id, 0, 0);

  CHECK(arrangement.getInstance(track_id, 0) == clip_id);
  CHECK(arrangement.getInstance(track_id, 5).empty()); // cleared away
  CHECK(arrangement.getInstance(track_id, 40) == "other"); // left alone
}

TEST(place_clip_instance_one_shot_clears_only_through_its_own_length) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(8);
  shot.setLooping(false);
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 4, "other1");  // within [0, 7] - the one-shot's own reach
  arrangement.setInstance(track_id, 8, "other2");  // just past it

  placeClipInstance(song, track_id, 0, 0);

  CHECK(arrangement.getInstance(track_id, 0) == clip_id);
  CHECK(arrangement.getInstance(track_id, 4).empty()); // cleared, within reach
  CHECK(arrangement.getInstance(track_id, 8) == "other2"); // untouched, beyond the one-shot's own reach
}

TEST(place_clip_instance_out_of_range_index_is_a_noop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & arrangement = song.getArrangement();

  placeClipInstance(song, track_id, 0, 5); // no clips exist at all

  CHECK(arrangement.getInstance(track_id, 0).empty());
}

TEST(place_stop_instance_clears_nothing) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 20, "other"); // a pre-existing, later instance event

  placeStopInstance(song, track_id, 0);

  CHECK(arrangement.getInstance(track_id, 0) == "OFF");
  CHECK(arrangement.getInstance(track_id, 20) == "other"); // untouched - a stop clears nothing
}

// Leaves a hole rather than shifting every later clip's own index down -
// holes are allowed (Song::ensureClipAt()), and every other track's own
// scene rows are indexed against this same track's clip list, so
// renumbering everything past the deleted one would silently misalign
// them all against it.
TEST(delete_clip_leaves_a_hole_in_place_and_clears_every_instance) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip a(track_id);
  auto a_id = song.addClip(move(a)).getId(); // index 0
  Clip b(track_id);
  auto b_id = song.addClip(move(b)).getId(); // index 1

  song.getArrangement().setInstance(track_id, 0, a_id);
  song.getArrangement().setInstance(track_id, 32, b_id); // a different clip, same track - untouched
  song.getArrangement().setInstance(track_id, 80, a_id); // the same clip, placed again later

  deleteClip(song, track_id, 0); // "a"

  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 2); // b's own index (1) never shifts
  CHECK(clips[0].isEmpty()); // "a"'s own slot is now a fresh, id-less filler
  CHECK(clips[0].getId().empty());
  CHECK(clips[1].getId() == b_id); // untouched, still at its own index
  CHECK(song.getArrangement().getInstance(track_id, 0).empty()); // cleared
  CHECK(song.getArrangement().getInstance(track_id, 32) == b_id); // a different clip - left alone
  CHECK(song.getArrangement().getInstance(track_id, 80).empty()); // cleared at every placement, not just the first one found
}

TEST(delete_clip_out_of_range_index_is_a_noop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip a(track_id);
  song.addClip(move(a));

  deleteClip(song, track_id, 5);

  CHECK(song.getClips(track_id).size() == 1); // untouched
}

TEST(duplicate_clip_copies_into_the_slot_below_under_a_fresh_id) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip a(track_id);
  a.getLeafPattern().setNote(0, 0, Note(60, 100));
  a.setName("riff");
  auto a_id = song.addClip(move(a)).getId(); // index 0

  CHECK(duplicateClip(song, track_id, 0) == 1);
  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 2);
  CHECK(clips[1].getName() == "riff");
  CHECK(clips[1].getLeafPattern().getNote(0, 0).getValue() == 60);
  CHECK(!clips[1].getId().empty() && clips[1].getId() != a_id);
  clips[1].getLeafPattern().setNote(0, 0, Note(72, 100)); // independent of the source
  CHECK(clips[0].getLeafPattern().getNote(0, 0).getValue() == 60);
}

TEST(duplicate_clip_overwrites_the_slot_below_and_its_placements) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip a(track_id);
  a.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(move(a));
  Clip b(track_id);
  b.getLeafPattern().setNote(0, 0, Note(64, 100));
  auto b_id = song.addClip(move(b)).getId(); // index 1 - populated
  song.getArrangement().setInstance(track_id, 8, b_id);

  CHECK(duplicateClip(song, track_id, 0) == 1);
  auto & clips = song.getClips(track_id);
  CHECK(clips[1].getLeafPattern().getNote(0, 0).getValue() == 60);
  CHECK(clips[1].getId() != b_id);
  CHECK(song.getArrangement().getInstancesForTrack(track_id).empty()); // b's placement went with it
}

TEST(duplicate_clip_refuses_nothing_to_copy) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip a(track_id);
  a.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(move(a));
  CHECK(duplicateClip(song, track_id, 1) == -1); // off the end
  CHECK(duplicateClip(song, track_id, -1) == -1);
}

TEST(place_clip_copy_retargets_a_clip_onto_another_track) {
  Song song;
  auto first = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto second = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  Clip a(first);
  a.getLeafPattern().setNote(0, 0, Note(60, 100));
  auto a_id = song.addClip(move(a)).getId();

  Clip copy = song.getClips(first)[0];
  CHECK(placeClipCopy(song, second, 2, copy) == 2); // pads the slots before it
  auto & clips = song.getClips(second);
  CHECK(clips.size() == 3);
  CHECK(clips[2].getLeafTrackId() == second);
  CHECK(clips[2].getLeafPattern().getNote(0, 0).getValue() == 60);
  CHECK(clips[2].getId() != a_id);
  CHECK(song.getClips(first).size() == 1);                   // the source stays put
  CHECK(placeClipCopy(song, second, 0, Clip(second)) == -1); // an empty clip is nothing
}

TEST(resolve_instance_at_finds_nothing_before_any_event) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 10, "clip0");

  CHECK(resolveInstanceAt(song, track_id, 5).clip_index == Arrangement::kNoInstance);
}

TEST(resolve_instance_at_finds_a_looping_clip_indefinitely) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(4);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, track_id, 10).clip_index == 0);
  CHECK(resolveInstanceAt(song, track_id, 100).clip_index == 0); // still active, far later
}

TEST(resolve_instance_at_stops_a_one_shot_once_its_own_length_elapses) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, track_id, 10).clip_index == 0); // its own first row
  CHECK(resolveInstanceAt(song, track_id, 13).clip_index == 0); // last row still sounding
  CHECK(resolveInstanceAt(song, track_id, 14).clip_index == Arrangement::kNoInstance); // finished
}

TEST(resolve_instance_at_finds_an_explicit_stop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 10, "OFF");

  CHECK(resolveInstanceAt(song, track_id, 10).clip_index == Arrangement::kStopInstance);
  CHECK(resolveInstanceAt(song, track_id, 50).clip_index == Arrangement::kStopInstance);
}

TEST(resolve_instance_at_a_later_event_supersedes_an_earlier_one) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip a(track_id);
  a.setLooping(true);
  auto a_id = song.addClip(move(a)).getId(); // index 0
  Clip b(track_id);
  b.setLooping(true);
  auto b_id = song.addClip(move(b)).getId(); // index 1

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 0, a_id);
  arrangement.setInstance(track_id, 20, b_id);

  CHECK(resolveInstanceAt(song, track_id, 10).clip_index == 0);
  CHECK(resolveInstanceAt(song, track_id, 20).clip_index == 1);
  CHECK(resolveInstanceAt(song, track_id, 50).clip_index == 1);
}

// start_row is what lets a caller (SongState.h's own note scheduler)
// compute the row *within* the active clip's own content, not just which
// clip is active.
TEST(resolve_instance_at_reports_the_instances_own_start_row) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, track_id, 10).start_row == 10);
  CHECK(resolveInstanceAt(song, track_id, 25).start_row == 10);
}

// The whole point of addressing a placed instance by the clip's own
// stable id, not its position: an instance keeps resolving to the same
// clip even after something else in the same track's clip list changes
// position around it.
TEST(resolve_instance_at_survives_a_reorder_of_the_clip_list) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip a(track_id);
  a.setLooping(true);
  auto a_id = song.addClip(move(a)).getId(); // index 0
  Clip b(track_id);
  b.setLooping(true);
  song.addClip(move(b)); // index 1

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 0, a_id); // placed while a is at index 0

  // Simulate a future delete/reorder (Phase E, not built yet): a ends up
  // at index 1 instead of 0.
  std::swap(song.getClips(track_id)[0], song.getClips(track_id)[1]);
  CHECK(song.getClips(track_id)[1].getId() == a_id);

  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == 1); // follows a to its new position
}

// The exact mechanism LaunchpadManager::handleStepGridPadEvent()/
// triggerAuditionStep()/the LED-state builder now delegate to instead of
// reading/writing the track's background Pattern directly - a step
// written while a clip instance is active must land in the clip's own
// leaf Pattern, live-linked, not the background, and reading it back
// must resolve to the same place. A step-sequenced PercussionTrack
// specifically, since nothing else exercises resolveEditTarget()/
// resolveReadTarget() with one.
TEST(resolve_edit_and_read_target_route_drum_machine_steps_through_a_clip) {
  Song song;
  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(make_unique<PercussionTrack>()));
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.setLength(8);
  clip.setLooping(true);
  song.addClip(move(clip)); // index 0

  auto & arrangement = song.getArrangement();
  placeClipInstance(song, track_id, 0, 0);

  // Write step 2, the same call handleStepGridPadEvent() now makes.
  auto edit_target = resolveEditTarget(song, track_id, 2);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(36, 100));

  // Read it back the same way triggerAuditionStep()/the LED builder now do.
  auto read_target = resolveReadTarget(song, track_id, 2);
  CHECK(read_target.is_instance);
  CHECK(read_target.pattern->getNote(read_target.effective_row, 0).getValue() == 36);

  // It landed in the clip's own leaf Pattern, not the track's background.
  CHECK(song.getClips(track_id)[0].getLeafPattern().getNote(2, 0).getValue() == 36);
  CHECK(!arrangement.getNote(2, track_id, 0).isDefined());
}

// A sample clip carries raw audio, not a Pattern (Clip::getLeafPattern()
// would throw std::out_of_range on one - it has no entry in
// patterns_by_track_ at all) - resolveReadTarget()/resolveEditTarget()
// must not dereference it the way they do for a note-based clip.
// Regression test for a real crash: PatternEditor::renderRow() calling
// resolveReadTarget() on every visible row, including a placed
// SampleTrack instance, uncaught until an actual sample clip was placed
// and the row it lived on came into view.
TEST(resolve_read_target_does_not_crash_on_a_sample_clip_instance) {
  Song song;
  auto & track = song.addTrack(make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.getSampleContent().setBuffer(make_shared<AudioBuffer>(1, 4));
  clip.setLength(4);
  clip.setLooping(false);
  song.addClip(move(clip)); // index 0

  placeClipInstance(song, track_id, 0, 0);

  auto read_target = resolveReadTarget(song, track_id, 1);
  CHECK(read_target.is_instance);
  CHECK(read_target.clip_index == 0);
  CHECK(read_target.unwrapped_row == 1);
  CHECK(read_target.pattern != nullptr); // the shared empty-pattern sentinel, never null

  // Falls back to ordinary background-pattern resolution rather than
  // crashing - there is nothing meaningful to edit on a sample clip's row.
  auto edit_target = resolveEditTarget(song, track_id, 1);
  CHECK(edit_target.pattern != nullptr);
}

// Controller::getFocusedClip()'s own override: resolves to a focused
// clip's own leaf Pattern regardless of what's actually placed at the
// requested row - no instance placed at all, and even a *different*
// clip's own instance active there.
TEST(resolve_edit_and_read_target_apply_a_focused_clip_override_regardless_of_row_state) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip focused(track_id);
  focused.setLength(8);
  auto focused_id = song.addClip(move(focused)).getId(); // index 0
  Clip other(track_id);
  other.setLength(8);
  other.setLooping(true);
  song.addClip(move(other)); // index 1

  placeClipInstance(song, track_id, 0, 1); // "other" is active at every row

  // Write through the focus, at a row where "other"'s own instance is
  // what would ordinarily resolve.
  auto edit_target = resolveEditTarget(song, track_id, 3, focused_id);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Landed in the focused clip's own leaf Pattern, not "other"'s.
  CHECK(song.getClips(track_id)[0].getLeafPattern().getNote(3, 0).getValue() == 60);
  CHECK(!song.getClips(track_id)[1].getLeafPattern().getNote(3, 0).isDefined());

  auto read_target = resolveReadTarget(song, track_id, 3, focused_id);
  CHECK(read_target.is_instance);
  CHECK(read_target.is_focused_override);
  CHECK(read_target.pattern->getNote(3, 0).getValue() == 60);

  // No focus (empty id): ordinary resolution, "other" is active again.
  CHECK(!resolveReadTarget(song, track_id, 3).is_focused_override);
  CHECK(resolveReadTarget(song, track_id, 3).clip_index == 1);
}

// A stale/deleted clip id falls back cleanly to ordinary resolution,
// matching resolveInstanceAt()'s own resilience for a dangling instance
// reference.
TEST(resolve_edit_target_falls_back_when_the_focused_clip_id_is_stale) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & arrangement = song.getArrangement();

  auto edit_target = resolveEditTarget(song, track_id, 3, "no-such-clip");
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Landed in the track's own background - the ordinary no-instance path.
  CHECK(arrangement.getNote(3, track_id, 0).getValue() == 60);
}

// A focus set for one track's own clip must not affect a different
// track's resolution, even when that other track happens to have a clip
// occupying the same list position.
TEST(resolve_edit_target_focus_does_not_leak_across_tracks) {
  Song song;
  auto & track_a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_a_id = track_a.getInternalId();
  auto & track_b = song.addTrack(make_unique<InstrumentTrack>(1));
  auto track_b_id = track_b.getInternalId();

  Clip clip_a(track_a_id);
  clip_a.setLength(8);
  auto clip_a_id = song.addClip(move(clip_a)).getId(); // track_a's own index 0

  auto & arrangement = song.getArrangement();

  // Focused on track_a's own clip, but this call is against track_b -
  // that id doesn't resolve to anything in track_b's own clip list.
  auto edit_target = resolveEditTarget(song, track_b_id, 3, clip_a_id);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Falls through to track_b's own background, untouched by track_a's focus.
  CHECK(arrangement.getNote(3, track_b_id, 0).getValue() == 60);
  CHECK(!song.getClips(track_a_id)[0].getLeafPattern().getNote(3, 0).isDefined());
}

// A stop placed after a *looping* clip's own trigger must silence every
// later row indefinitely, not just the one row it was placed at - the
// exact contract SongState::renderBlock()'s own note scheduler relies on
// (resolveInstanceAt() returning Arrangement::kStopInstance, never falling back
// to re-reading the clip once its own stop has been reached) and what
// LaunchpadManager::placeRecordingStop() (Live-View recording's own
// "stop this track" primitive - an empty-row press or CC49 held) writes.
TEST(resolve_instance_at_a_stop_after_a_looping_trigger_silences_every_later_row) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  song.addClip(move(loop)); // index 0

  song.setTimeSignature(TimeSignature{4, 4});

  placeClipInstance(song, track_id, 0, 0);
  // Still looping well past its own native length, before any stop -
  // this is what "looping" actually means for resolveInstanceAt().
  CHECK(resolveInstanceAt(song, track_id, 56).clip_index == 0);

  placeStopInstance(song, track_id, 48); // bar-aligned, matching placeRecordingStop()'s own quantizedBarRow()
  CHECK(resolveInstanceAt(song, track_id, 47).clip_index == 0); // still active the row just before the stop
  CHECK(resolveInstanceAt(song, track_id, 48).clip_index == Arrangement::kStopInstance);
  CHECK(resolveInstanceAt(song, track_id, 49).clip_index == Arrangement::kStopInstance);
  CHECK(resolveInstanceAt(song, track_id, 200).clip_index == Arrangement::kStopInstance); // stays silenced, not just for one row
}

// ArrangementGrid's own overview samples one row per bar (raw_row =
// bar_in_scene * rows_per_bar) - a one-shot short enough to start and
// finish again entirely inside a single bar's own row span, never
// touching that bar's own first row, would otherwise be invisible to
// plain resolveInstanceAt(raw_row) in every bar: too early in the bar it
// starts in (raw_row is before the instance's own start), already
// expired again by the next bar's own raw_row. This is exactly what
// disabling a clip's own looping without moving it to a bar boundary
// used to make disappear from the overview entirely.
TEST(resolve_instance_for_bar_finds_a_one_shot_that_starts_and_ends_inside_one_bar) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 5, clip_id); // mid-bar start, well inside bar 0 (rows 0-15)

  // A plain per-row sample at each bar's own first row misses it either
  // way - confirms the scenario this test is actually about.
  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == Arrangement::kNoInstance); // too early
  CHECK(resolveInstanceAt(song, track_id, 16).clip_index == Arrangement::kNoInstance); // already expired again

  // The bar-granular query still finds it, attributed to bar 0 (its own
  // real start row, not the bar's start).
  auto active = resolveInstanceForBar(song, track_id, 0, 16);
  CHECK(active.clip_index == 0);
  CHECK(active.start_row == 5);
  // Bar 1 correctly shows nothing - the one-shot is long gone by row 16,
  // and nothing new was placed within bar 1's own span either.
  CHECK(resolveInstanceForBar(song, track_id, 16, 16).clip_index == Arrangement::kNoInstance);
}

TEST(resolve_instance_for_bar_still_finds_a_looping_instance_carried_over_from_an_earlier_bar) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 0, clip_id);

  // Unaffected: a bar this instance already reaches via its own first
  // row resolves exactly as resolveInstanceAt() itself would.
  CHECK(resolveInstanceForBar(song, track_id, 0, 16).clip_index == 0);
  CHECK(resolveInstanceForBar(song, track_id, 16, 16).clip_index == 0);
  CHECK(resolveInstanceForBar(song, track_id, 32, 16).start_row == 0);
}

// A stop landing mid-bar, right after the same bar's own real content,
// must not make the whole bar look empty in ArrangementGrid - the clip
// was genuinely active for part of it (the same "genuinely active for at
// least part of this bar" reasoning resolveInstanceForBar() already uses
// for one-shot expiry), so this bar still reports the clip; only a *later*
// bar, which the stop actually reaches before anything else does, reports
// stopped. A real bug report: C-k in PatternEditor terminating a clip a
// few rows into its own first bar made that bar disappear entirely from
// the overview instead of still showing the clip that had just played
// there.
TEST(resolve_instance_for_bar_still_shows_the_clip_when_a_stop_lands_later_in_its_own_leading_bar) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 0, clip_id);
  arrangement.setInstance(track_id, 5, "OFF"); // stopped mid-bar, well before bar 0 ends

  CHECK(resolveInstanceForBar(song, track_id, 0, 16).clip_index == 0); // still shows the clip - it played for rows 0-4 of this same bar
  CHECK(resolveInstanceForBar(song, track_id, 16, 16).clip_index == Arrangement::kStopInstance); // a later bar the stop actually reaches first, unaffected
}

// Two stops close enough together to leave nothing real in between within
// one bar - a degenerate case resolveInstanceForBar() doesn't try to walk
// arbitrarily far back for; "this bar is stopped" is a reasonable enough
// answer for it.
TEST(resolve_instance_for_bar_reports_stopped_when_nothing_real_precedes_a_mid_bar_stop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  auto & arrangement = song.getArrangement();
  arrangement.setInstance(track_id, 2, "OFF");
  arrangement.setInstance(track_id, 5, "OFF"); // a second stop - nothing real between rows 2 and 5

  CHECK(resolveInstanceForBar(song, track_id, 0, 16).clip_index == Arrangement::kStopInstance);
}

// mergeClipToBackground(): a one-shot clip's own content destructively
// replaces the background at every row its placement covers, including a
// row the clip itself left blank (silence overwrites whatever the
// background already had there too - a flatten, not a mix), then that one
// placement is removed.
TEST(merge_clip_to_background_overwrites_the_background_and_removes_the_placement) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  shot.getLeafPattern().setNote(0, 0, Note(60, 100));
  shot.getLeafPattern().setNote(2, 0, Note(64, 100));
  shot.getLeafPattern().setCommand(1, Command("0U50"));
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  arrangement.setNote(10, track_id, 0, Note(50, 80)); // pre-existing background content, about to be overwritten
  arrangement.setNote(13, track_id, 0, Note(70, 80)); // pre-existing background content the clip's own blank row 3 should also clear
  placeClipInstance(song, track_id, 10, 0); // covers rows 10-13

  CHECK(mergeClipToBackground(song, track_id, 10, ChannelConfiguration()) == true);

  CHECK(arrangement.getNotes(10, track_id).size() == 1);
  CHECK(arrangement.getNotes(10, track_id)[0].getValue() == 60); // overwritten, not left at 50
  CHECK(arrangement.getNotes(11, track_id).empty());
  CHECK(arrangement.getCommand(11, track_id).isDefined());
  CHECK(arrangement.getNotes(12, track_id)[0].getValue() == 64);
  CHECK(arrangement.getNotes(13, track_id).empty()); // the clip's own blank row cleared the old background note too

  // The one placement removed - a stop where the clip used to start -
  // but the clip itself stays in the pool.
  CHECK(resolveInstanceAt(song, track_id, 10).clip_index == Arrangement::kStopInstance);
  CHECK(song.getClips(track_id).size() == 1);
  CHECK(song.getClips(track_id)[0].getId() == clip_id);
}

// A looping clip's placement reaches all the way to the arrangement's end
// when nothing follows it, and each background row still reads back from
// the clip's own leaf Pattern wrapped by its length - the same modulo
// every other playback path already uses.
TEST(merge_clip_to_background_looping_clip_wraps_up_to_the_arrangements_end) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  song.setTimeSignature(TimeSignature{1, 4});

  Clip loop(track_id);
  loop.setLength(3);
  loop.setLooping(true);
  loop.getLeafPattern().setNote(0, 0, Note(60, 100));
  loop.getLeafPattern().setNote(2, 0, Note(64, 100));
  song.addClip(move(loop)); // index 0

  auto & arrangement = song.getArrangement();
  placeClipInstance(song, track_id, 0, 0);

  CHECK(mergeClipToBackground(song, track_id, 0, ChannelConfiguration()) == true);

  CHECK(arrangement.getNotes(0, track_id)[0].getValue() == 60); // leaf row 0
  CHECK(arrangement.getNotes(1, track_id).empty());              // leaf row 1
  CHECK(arrangement.getNotes(2, track_id)[0].getValue() == 64);  // leaf row 2
  CHECK(arrangement.getNotes(3, track_id)[0].getValue() == 60);  // wraps: 3 % 3 == 0

  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == Arrangement::kStopInstance);
}

TEST(merge_clip_to_background_is_a_noop_with_nothing_placed) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  CHECK(mergeClipToBackground(song, track_id, 5, ChannelConfiguration()) == false);
}

// A SampleTrack clip merges as a real additive mix into the track's own
// background bed, resolved the same way real playback would (gain 1.0 -
// no per-instance loudness/velocity concept exists yet), then the
// placement is removed the same as a note-based clip's own merge.
TEST(merge_clip_to_background_mixes_a_sample_clip_into_the_background_bed) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 4});
  auto & track = song.addTrack(make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  constexpr int kSourceFrames = 100;
  auto buffer = make_shared<AudioBuffer>(1, kSourceFrames);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < kSourceFrames; i++) data[i] = 1.0f;

  Clip sample_clip(track_id);
  sample_clip.setLength(2);
  sample_clip.setLooping(false);
  sample_clip.getSampleContent().setBuffer(buffer);
  auto clip_id = song.addClip(move(sample_clip)).getId(); // index 0

  auto & arrangement = song.getArrangement();
  placeClipInstance(song, track_id, 0, 0);

  ChannelConfiguration channel_config; // 44100Hz, tempo defaults to Song's own 90 bpm
  auto sample_interval = channel_config.getSampleInterval(song.getTempo());

  CHECK(mergeClipToBackground(song, track_id, 0, channel_config) == true);

  auto * background = arrangement.getSampleBackgroundContent(track_id);
  CHECK(background != nullptr);
  if (background) {
    CHECK(background->getBuffer() != nullptr);
    if (background->getBuffer()) {
      CHECK(background->getBuffer()->numberOfFrames() == 2 * sample_interval); // sized to what the one-shot covers
      auto background_data = background->getBuffer()->getChannelData(0);
      for (int i = 0; i < kSourceFrames; i++) CHECK_NEAR(background_data[i], 1.0f, 1e-6f);
      CHECK_NEAR(background_data[kSourceFrames], 0.0f, 1e-6f); // nothing past the clip's own real audio
    }
  }

  // The one placement removed - a stop where the clip used to start - but
  // the clip itself stays in the pool.
  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == Arrangement::kStopInstance);
  CHECK(song.getClips(track_id)[0].getId() == clip_id);
}

TEST(quantize_clip_snaps_each_note_to_its_closest_row_and_clears_the_delay) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip clip(track_id);
  clip.setLength(16);
  clip.setLooping(true);
  clip.getLeafPattern().setNote(2, 0, Note(60, 100, 40));   // early in its row: stays
  clip.getLeafPattern().setNote(4, 0, Note(62, 100, 200));  // late: next row
  clip.getLeafPattern().setNote(5, 0, Note(0, 0, 100));     // its off, snaps back onto row 5
  clip.getLeafPattern().setNote(15, 0, Note(64, 100, 128)); // past the end: wraps
  song.addClip(move(clip));

  CHECK(quantizeClip(song, track_id, 0));
  auto & pattern = song.getClips(track_id)[0].getLeafPattern();
  CHECK(pattern.getNote(2, 0).getValue() == 60 && pattern.getNote(2, 0).getDelay() == 0);
  CHECK(pattern.getNote(5, 0).getValue() == 62 && pattern.getNote(5, 0).getDelay() == 0);
  CHECK(!pattern.getNote(4, 0).isDefined());
  CHECK(pattern.getNote(0, 0).getValue() == 64);
  CHECK(!pattern.getNote(15, 0).isDefined());
  // The off collided with the note moved onto its row, so it moves one row on.
  CHECK(pattern.getNote(6, 0).isOff());
}

TEST(quantize_clip_clamps_in_a_one_shot_and_moves_a_colliding_note_to_the_next_column) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip clip(track_id);
  clip.setLength(8);
  clip.setLooping(false);
  clip.getLeafPattern().setNote(3, 0, Note(60, 100, 220)); // lands on row 4...
  clip.getLeafPattern().setNote(4, 0, Note(67, 100, 0));   // ...which is taken
  clip.getLeafPattern().setNote(7, 0, Note(72, 100, 250)); // past the end: clamped
  song.addClip(move(clip));

  CHECK(quantizeClip(song, track_id, 0));
  auto & pattern = song.getClips(track_id)[0].getLeafPattern();
  CHECK(pattern.getNote(4, 0).getValue() == 60);
  CHECK(pattern.getNote(4, 1).getValue() == 67);
  CHECK(pattern.getNote(7, 0).getValue() == 72);
}

TEST(quantize_clip_refuses_an_empty_or_missing_clip) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  song.ensureClipAt(track_id, 1);
  CHECK(!quantizeClip(song, track_id, 0));
  CHECK(!quantizeClip(song, track_id, 1)); // an empty filler
  CHECK(!quantizeClip(song, track_id, 9));
}

TEST(record_quantize_is_off_by_default_and_round_trips_through_the_song_file) {
  Song song;
  song.addTrack(std::make_unique<InstrumentTrack>(0));
  CHECK(!song.getRecordQuantize());
  song.setRecordQuantize(true);

  auto path = std::string(TESTS_SCRATCH_DIR) + "/record_quantize_round_trip.xml";
  song.save(path);
  Song reloaded;
  InstrumentProvider provider;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getRecordQuantize());
  std::filesystem::remove(path);
}
