#include "TestFramework.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/Song.h"
#include "../src/model/Scene.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/DrumMachineTrack.h"

using namespace std;

TEST(place_clip_instance_looping_clears_through_the_scenes_own_end) {
  Song song;
  song.setPatternLength(64);
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
  song.setPatternLength(64);
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
// must resolve to the same place. DrumMachineTrack specifically, since
// nothing else exercises resolveEditTarget()/resolveReadTarget() with one.
TEST(resolve_edit_and_read_target_route_drum_machine_steps_through_a_clip) {
  Song song;
  song.setPatternLength(64);
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
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
