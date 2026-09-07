#include "TestFramework.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/Song.h"
#include "../src/model/Scene.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/SampleContent.h"
#include "../src/audio/AudioBuffer.h"

using namespace std;

TEST(place_clip_instance_looping_clears_through_the_scenes_own_end) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setInstance(track_id, 40, "other"); // a pre-existing, later instance event

  placeClipInstance(song, scene, track_id, 0, 0);

  CHECK(scene.getInstance(track_id, 0) == clip_id);
  CHECK(scene.getInstance(track_id, 40).empty()); // cleared away
}

TEST(place_clip_instance_one_shot_clears_only_through_its_own_length) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(8);
  shot.setLooping(false);
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setInstance(track_id, 4, "other1");  // within [0, 7] - the one-shot's own reach
  scene.setInstance(track_id, 8, "other2");  // just past it

  placeClipInstance(song, scene, track_id, 0, 0);

  CHECK(scene.getInstance(track_id, 0) == clip_id);
  CHECK(scene.getInstance(track_id, 4).empty()); // cleared, within reach
  CHECK(scene.getInstance(track_id, 8) == "other2"); // untouched, beyond the one-shot's own reach
}

TEST(place_clip_instance_out_of_range_index_is_a_noop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();

  placeClipInstance(song, scene, track_id, 0, 5); // no clips exist at all

  CHECK(scene.getInstance(track_id, 0).empty());
}

TEST(place_stop_instance_clears_nothing) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();
  scene.setInstance(track_id, 20, "other"); // a pre-existing, later instance event

  placeStopInstance(scene, track_id, 0);

  CHECK(scene.getInstance(track_id, 0) == "OFF");
  CHECK(scene.getInstance(track_id, 20) == "other"); // untouched - a stop clears nothing
}

TEST(delete_clip_removes_it_from_the_clip_list_and_clears_every_instance) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip a(track_id);
  auto a_id = song.addClip(move(a)).getId(); // index 0
  Clip b(track_id);
  auto b_id = song.addClip(move(b)).getId(); // index 1

  // Indices, not references, held across both addScene() calls below -
  // the second call can reallocate Song's own scenes_ vector, which would
  // dangle a reference taken from the first.
  song.addScene(); // scene 0
  song.getScene(0).setInstance(track_id, 0, a_id);
  song.getScene(0).setInstance(track_id, 32, b_id); // a different clip, same track - untouched
  song.addScene(); // scene 1
  song.getScene(1).setInstance(track_id, 16, a_id); // the same clip, placed again in a later scene

  deleteClip(song, track_id, 0); // "a"

  CHECK(song.getClips(track_id).size() == 1);
  CHECK(song.getClips(track_id)[0].getId() == b_id);
  CHECK(song.getScene(0).getInstance(track_id, 0).empty()); // cleared
  CHECK(song.getScene(0).getInstance(track_id, 32) == b_id); // a different clip - left alone
  CHECK(song.getScene(1).getInstance(track_id, 16).empty()); // cleared in the later scene too, not just the first one found
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

TEST(resolve_instance_at_finds_nothing_before_any_event) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();
  scene.setInstance(track_id, 10, "clip0");

  CHECK(resolveInstanceAt(song, scene, track_id, 5).clip_index == Scene::kNoInstance);
}

TEST(resolve_instance_at_finds_a_looping_clip_indefinitely) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(4);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, scene, track_id, 10).clip_index == 0);
  CHECK(resolveInstanceAt(song, scene, track_id, 100).clip_index == 0); // still active, far later
}

TEST(resolve_instance_at_stops_a_one_shot_once_its_own_length_elapses) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, scene, track_id, 10).clip_index == 0); // its own first row
  CHECK(resolveInstanceAt(song, scene, track_id, 13).clip_index == 0); // last row still sounding
  CHECK(resolveInstanceAt(song, scene, track_id, 14).clip_index == Scene::kNoInstance); // finished
}

TEST(resolve_instance_at_finds_an_explicit_stop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();
  scene.setInstance(track_id, 10, "OFF");

  CHECK(resolveInstanceAt(song, scene, track_id, 10).clip_index == Scene::kStopInstance);
  CHECK(resolveInstanceAt(song, scene, track_id, 50).clip_index == Scene::kStopInstance);
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

  auto & scene = song.addScene();
  scene.setInstance(track_id, 0, a_id);
  scene.setInstance(track_id, 20, b_id);

  CHECK(resolveInstanceAt(song, scene, track_id, 10).clip_index == 0);
  CHECK(resolveInstanceAt(song, scene, track_id, 20).clip_index == 1);
  CHECK(resolveInstanceAt(song, scene, track_id, 50).clip_index == 1);
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

  auto & scene = song.addScene();
  scene.setInstance(track_id, 10, clip_id);

  CHECK(resolveInstanceAt(song, scene, track_id, 10).start_row == 10);
  CHECK(resolveInstanceAt(song, scene, track_id, 25).start_row == 10);
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

  auto & scene = song.addScene();
  scene.setInstance(track_id, 0, a_id); // placed while a is at index 0

  // Simulate a future delete/reorder (Phase E, not built yet): a ends up
  // at index 1 instead of 0.
  std::swap(song.getClips(track_id)[0], song.getClips(track_id)[1]);
  CHECK(song.getClips(track_id)[1].getId() == a_id);

  CHECK(resolveInstanceAt(song, scene, track_id, 0).clip_index == 1); // follows a to its new position
}

// The exact mechanism LaunchpadManager::handleStepGridPadEvent()/
// triggerAuditionStep()/the LED-state builder now delegate to instead of
// reading/writing the scene's background Pattern directly - a step
// written while a clip instance is active must land in the clip's own
// leaf Pattern, live-linked, not the background, and reading it back
// must resolve to the same place. A step-sequenced PercussionTrack
// specifically, since nothing else exercises resolveEditTarget()/
// resolveReadTarget() with one.
TEST(resolve_edit_and_read_target_route_drum_machine_steps_through_a_clip) {
  Song song;
  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(make_unique<PercussionTrack>()));
  track.addLane(36);
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.setLength(8);
  clip.setLooping(true);
  song.addClip(move(clip)); // index 0

  auto & scene = song.addScene();
  placeClipInstance(song, scene, track_id, 0, 0);

  // Write step 2, the same call handleStepGridPadEvent() now makes.
  auto edit_target = resolveEditTarget(song, scene, track_id, 2);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(36, 100));

  // Read it back the same way triggerAuditionStep()/the LED builder now do.
  auto read_target = resolveReadTarget(song, scene, track_id, 2);
  CHECK(read_target.is_instance);
  CHECK(track.getHitNotesAtRow(*read_target.pattern, read_target.effective_row) == (vector<int>{ 36 }));

  // It landed in the clip's own leaf Pattern, not the scene's background.
  CHECK(song.getClips(track_id)[0].getLeafPattern().getNote(2, 0).getValue() == 36);
  CHECK(!scene.getNote(2, track_id, 0).isDefined());
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
  clip.getOrCreateSampleContent().setBuffer(make_shared<AudioBuffer>(1, 4));
  clip.setLength(4);
  clip.setLooping(false);
  song.addClip(move(clip)); // index 0

  auto & scene = song.addScene();
  placeClipInstance(song, scene, track_id, 0, 0);

  auto read_target = resolveReadTarget(song, scene, track_id, 1);
  CHECK(read_target.is_instance);
  CHECK(read_target.clip_index == 0);
  CHECK(read_target.unwrapped_row == 1);
  CHECK(read_target.pattern != nullptr); // the shared empty-pattern sentinel, never null

  // Falls back to ordinary background-pattern resolution rather than
  // crashing - there is nothing meaningful to edit on a sample clip's row.
  auto edit_target = resolveEditTarget(song, scene, track_id, 1);
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
  auto other_id = song.addClip(move(other)).getId(); // index 1

  auto & scene = song.addScene();
  placeClipInstance(song, scene, track_id, 0, 1); // "other" is active at every row

  // Write through the focus, at a row where "other"'s own instance is
  // what would ordinarily resolve.
  auto edit_target = resolveEditTarget(song, scene, track_id, 3, focused_id);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Landed in the focused clip's own leaf Pattern, not "other"'s.
  CHECK(song.getClips(track_id)[0].getLeafPattern().getNote(3, 0).getValue() == 60);
  CHECK(!song.getClips(track_id)[1].getLeafPattern().getNote(3, 0).isDefined());

  auto read_target = resolveReadTarget(song, scene, track_id, 3, focused_id);
  CHECK(read_target.is_instance);
  CHECK(read_target.is_focused_override);
  CHECK(read_target.pattern->getNote(3, 0).getValue() == 60);

  // No focus (empty id): ordinary resolution, "other" is active again.
  CHECK(!resolveReadTarget(song, scene, track_id, 3).is_focused_override);
  CHECK(resolveReadTarget(song, scene, track_id, 3).clip_index == 1);
}

// A stale/deleted clip id falls back cleanly to ordinary resolution,
// matching resolveInstanceAt()'s own resilience for a dangling instance
// reference.
TEST(resolve_edit_target_falls_back_when_the_focused_clip_id_is_stale) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();

  auto edit_target = resolveEditTarget(song, scene, track_id, 3, "no-such-clip");
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Landed in the scene's own background - the ordinary no-instance path.
  CHECK(scene.getNote(3, track_id, 0).getValue() == 60);
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

  auto & scene = song.addScene();

  // Focused on track_a's own clip, but this call is against track_b -
  // that id doesn't resolve to anything in track_b's own clip list.
  auto edit_target = resolveEditTarget(song, scene, track_b_id, 3, clip_a_id);
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  // Falls through to track_b's own background, untouched by track_a's focus.
  CHECK(scene.getNote(3, track_b_id, 0).getValue() == 60);
  CHECK(!song.getClips(track_a_id)[0].getLeafPattern().getNote(3, 0).isDefined());
}

// A stop placed after a *looping* clip's own trigger must silence every
// later row indefinitely, not just the one row it was placed at - the
// exact contract SongState::renderBlock()'s own note scheduler relies on
// (resolveInstanceAt() returning Scene::kStopInstance, never falling back
// to re-reading the clip once its own stop has been reached) and what
// LaunchpadManager::placeRecordingStop() (Session-view recording's own
// "stop this track" primitive - an empty-row press or CC49 held) writes.
TEST(resolve_instance_at_a_stop_after_a_looping_trigger_silences_every_later_row) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  song.addClip(move(loop)); // index 0

  auto & scene = song.addScene();
  scene.setLengthBars(4);
  song.setRowsPerBar(16);

  placeClipInstance(song, scene, track_id, 0, 0);
  // Still looping well past its own native length, before any stop -
  // this is what "looping" actually means for resolveInstanceAt().
  CHECK(resolveInstanceAt(song, scene, track_id, 56).clip_index == 0);

  placeStopInstance(scene, track_id, 48); // bar-aligned, matching placeRecordingStop()'s own quantizedBarRow()
  CHECK(resolveInstanceAt(song, scene, track_id, 47).clip_index == 0); // still active the row just before the stop
  CHECK(resolveInstanceAt(song, scene, track_id, 48).clip_index == Scene::kStopInstance);
  CHECK(resolveInstanceAt(song, scene, track_id, 49).clip_index == Scene::kStopInstance);
  CHECK(resolveInstanceAt(song, scene, track_id, 200).clip_index == Scene::kStopInstance); // stays silenced, not just for one row
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

  auto & scene = song.addScene();
  scene.setInstance(track_id, 5, clip_id); // mid-bar start, well inside bar 0 (rows 0-15)

  // A plain per-row sample at each bar's own first row misses it either
  // way - confirms the scenario this test is actually about.
  CHECK(resolveInstanceAt(song, scene, track_id, 0).clip_index == Scene::kNoInstance); // too early
  CHECK(resolveInstanceAt(song, scene, track_id, 16).clip_index == Scene::kNoInstance); // already expired again

  // The bar-granular query still finds it, attributed to bar 0 (its own
  // real start row, not the bar's start).
  auto active = resolveInstanceForBar(song, scene, track_id, 0, 16);
  CHECK(active.clip_index == 0);
  CHECK(active.start_row == 5);
  // Bar 1 correctly shows nothing - the one-shot is long gone by row 16,
  // and nothing new was placed within bar 1's own span either.
  CHECK(resolveInstanceForBar(song, scene, track_id, 16, 16).clip_index == Scene::kNoInstance);
}

TEST(resolve_instance_for_bar_still_finds_a_looping_instance_carried_over_from_an_earlier_bar) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip loop(track_id);
  loop.setLength(8);
  loop.setLooping(true);
  auto clip_id = song.addClip(move(loop)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setInstance(track_id, 0, clip_id);

  // Unaffected: a bar this instance already reaches via its own first
  // row resolves exactly as resolveInstanceAt() itself would.
  CHECK(resolveInstanceForBar(song, scene, track_id, 0, 16).clip_index == 0);
  CHECK(resolveInstanceForBar(song, scene, track_id, 16, 16).clip_index == 0);
  CHECK(resolveInstanceForBar(song, scene, track_id, 32, 16).start_row == 0);
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

  auto & scene = song.addScene();
  scene.setInstance(track_id, 0, clip_id);
  scene.setInstance(track_id, 5, "OFF"); // stopped mid-bar, well before bar 0 ends

  CHECK(resolveInstanceForBar(song, scene, track_id, 0, 16).clip_index == 0); // still shows the clip - it played for rows 0-4 of this same bar
  CHECK(resolveInstanceForBar(song, scene, track_id, 16, 16).clip_index == Scene::kStopInstance); // a later bar the stop actually reaches first, unaffected
}

// Two stops close enough together to leave nothing real in between within
// one bar - a degenerate case resolveInstanceForBar() doesn't try to walk
// arbitrarily far back for; "this bar is stopped" is a reasonable enough
// answer for it.
TEST(resolve_instance_for_bar_reports_stopped_when_nothing_real_precedes_a_mid_bar_stop) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  auto & scene = song.addScene();
  scene.setInstance(track_id, 2, "OFF");
  scene.setInstance(track_id, 5, "OFF"); // a second stop - nothing real between rows 2 and 5

  CHECK(resolveInstanceForBar(song, scene, track_id, 0, 16).clip_index == Scene::kStopInstance);
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
  shot.getLeafPattern().setCommand(1, Command("U050"));
  auto clip_id = song.addClip(move(shot)).getId(); // index 0

  auto & scene = song.addScene();
  scene.setNote(10, track_id, 0, Note(50, 80)); // pre-existing background content, about to be overwritten
  scene.setNote(13, track_id, 0, Note(70, 80)); // pre-existing background content the clip's own blank row 3 should also clear
  placeClipInstance(song, scene, track_id, 10, 0); // covers rows 10-13

  CHECK(mergeClipToBackground(song, scene, track_id, 10) == true);

  CHECK(scene.getNotes(10, track_id).size() == 1);
  CHECK(scene.getNotes(10, track_id)[0].getValue() == 60); // overwritten, not left at 50
  CHECK(scene.getNotes(11, track_id).empty());
  CHECK(scene.getCommand(11, track_id).isDefined());
  CHECK(scene.getNotes(12, track_id)[0].getValue() == 64);
  CHECK(scene.getNotes(13, track_id).empty()); // the clip's own blank row cleared the old background note too

  // The one placement removed - a stop where the clip used to start -
  // but the clip itself stays in the pool.
  CHECK(resolveInstanceAt(song, scene, track_id, 10).clip_index == Scene::kStopInstance);
  CHECK(song.getClips(track_id).size() == 1);
  CHECK(song.getClips(track_id)[0].getId() == clip_id);
}

// A looping clip's placement reaches all the way to the scene's own end,
// and each background row still reads back from the clip's own leaf
// Pattern wrapped by its length - the same modulo every other playback
// path already uses.
TEST(merge_clip_to_background_looping_clip_wraps_across_the_whole_scene) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  song.setRowsPerBar(4);

  Clip loop(track_id);
  loop.setLength(3);
  loop.setLooping(true);
  loop.getLeafPattern().setNote(0, 0, Note(60, 100));
  loop.getLeafPattern().setNote(2, 0, Note(64, 100));
  song.addClip(move(loop)); // index 0

  auto & scene = song.addScene();
  scene.setLengthBars(1); // 4 rows total
  placeClipInstance(song, scene, track_id, 0, 0);

  CHECK(mergeClipToBackground(song, scene, track_id, 0) == true);

  CHECK(scene.getNotes(0, track_id)[0].getValue() == 60); // leaf row 0
  CHECK(scene.getNotes(1, track_id).empty());              // leaf row 1
  CHECK(scene.getNotes(2, track_id)[0].getValue() == 64);  // leaf row 2
  CHECK(scene.getNotes(3, track_id)[0].getValue() == 60);  // wraps: 3 % 3 == 0

  CHECK(resolveInstanceAt(song, scene, track_id, 0).clip_index == Scene::kStopInstance);
}

TEST(merge_clip_to_background_is_a_noop_with_nothing_placed) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & scene = song.addScene();

  CHECK(mergeClipToBackground(song, scene, track_id, 5) == false);
}

// A SampleTrack clip carries raw audio, not a Pattern - no background
// counterpart exists for it to merge into yet, so this stays a no-op
// (including leaving the placement itself untouched).
TEST(merge_clip_to_background_is_a_noop_for_a_sample_clip) {
  Song song;
  auto & track = song.addTrack(make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  Clip sample_clip(track_id);
  sample_clip.setLength(4);
  sample_clip.getOrCreateSampleContent().setBuffer(make_shared<AudioBuffer>(1, 10));
  auto clip_id = song.addClip(move(sample_clip)).getId(); // index 0

  auto & scene = song.addScene();
  placeClipInstance(song, scene, track_id, 0, 0);

  CHECK(mergeClipToBackground(song, scene, track_id, 0) == false);
  CHECK(resolveInstanceAt(song, scene, track_id, 0).clip_index == 0); // still placed, untouched
  CHECK(song.getClips(track_id)[0].getId() == clip_id);
}
