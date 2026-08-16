#include "TestFramework.h"

#include "../src/PercussionTrack.h"
#include "../src/Song.h"
#include "../src/InstrumentProvider.h"

#include <filesystem>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

TEST(percussion_track_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "percussion_track_round_trip_scratch.xml").string();

  Song song;
  song.addTrack(make_unique<PercussionTrack>());
  song.setPatternLength(8);
  song.addScene();
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getTracks().size() == 1);
  auto & reloaded_track = *reloaded.getTracks()[0];
  CHECK(reloaded_track.getElementName() == std::string("percussionTrack"));
  CHECK(reloaded_track.getType() == TrackType::PERCUSSION_CONTROL);
  CHECK(dynamic_cast<PercussionTrack *>(&reloaded_track) != nullptr);

  fs::remove(scratch_path);
}
