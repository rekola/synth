#include "TestFramework.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/SpatialPlacement.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Song.h"

#include <cmath>
#include <filesystem>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace spatial;

namespace {

SphericalPosition stage() { return {10.0f, 5.0f, 1.0f, 1.0f}; }

} // namespace

TEST(spiral_slot_zero_is_the_track_position) {
  auto placed = placeOnSpiral(stage(), 0);
  CHECK(placed.azimuth == 10.0f);
  CHECK(placed.elevation == 5.0f);
}

TEST(a_track_without_extent_places_every_slot_at_its_position) {
  SphericalPosition point{10.0f, 5.0f, 1.0f, 0.0f};
  for (int slot = 0; slot < 8; slot++) {
    auto placed = placeOnSpiral(point, slot);
    CHECK(placed.azimuth == 10.0f);
    CHECK(placed.elevation == 5.0f);
  }
}

TEST(a_spiral_slot_does_not_depend_on_how_many_slots_there_are) {
  // The offset is a function of the slot index alone.
  auto first = spiralOffset(1);
  auto again = spiralOffset(1);
  CHECK(first.u == again.u && first.v == again.v);
  CHECK(placeOnSpiral(stage(), 3).azimuth == placeOnSpiral(stage(), 3).azimuth);
}

TEST(the_spiral_turns_by_the_golden_angle_and_reaches_the_rim_at_the_full_slot) {
  for (int slot = 1; slot <= 12; slot++) {
    auto offset = spiralOffset(slot);
    float radius = std::sqrt(offset.u * offset.u + offset.v * offset.v);
    CHECK_NEAR(radius, std::min(1.0f, std::sqrt(static_cast<float>(slot) / kSpiralFull)), 1e-4f);
  }
  auto a = spiralOffset(1);
  auto b = spiralOffset(2);
  float turn = std::atan2(b.v, b.u) - std::atan2(a.v, a.u);
  float degrees = std::fmod(turn * 180.0f / static_cast<float>(M_PI) + 720.0f, 360.0f);
  CHECK_NEAR(degrees, kGoldenAngleDegrees, 0.01f);
  // No two slots share a bearing.
  for (int i = 1; i < 12; i++) {
    for (int j = i + 1; j <= 12; j++) {
      auto p = spiralOffset(i), q = spiralOffset(j);
      CHECK(std::fabs(p.u - q.u) + std::fabs(p.v - q.v) > 1e-3f);
    }
  }
}

TEST(a_chord_on_the_spiral_is_spread_in_two_dimensions) {
  auto c = placeOnSpiral(stage(), 0), e = placeOnSpiral(stage(), 1), g = placeOnSpiral(stage(), 2);
  // Three slots, not all on one line in azimuth/elevation.
  float cross = (e.azimuth - c.azimuth) * (g.elevation - c.elevation) - (e.elevation - c.elevation) * (g.azimuth - c.azimuth);
  CHECK(std::fabs(cross) > 1e-3f);
}

TEST(an_arc_runs_from_the_low_key_to_the_high_key_across_the_extent) {
  auto low = placeOnArc(stage(), static_cast<float>(kArcLowKey), kArcLowKey, kArcHighKey);
  auto mid = placeOnArc(stage(), 64.5f, kArcLowKey, kArcHighKey);
  auto high = placeOnArc(stage(), static_cast<float>(kArcHighKey), kArcLowKey, kArcHighKey);
  CHECK(low.azimuth < mid.azimuth);
  CHECK(mid.azimuth < high.azimuth);
  CHECK_NEAR(mid.azimuth, 10.0f, 0.01f);
  CHECK_NEAR(low.azimuth - 10.0f, -(high.azimuth - 10.0f), 0.01f);
  // Beyond the span it stops at the ends.
  CHECK(placeOnArc(stage(), 0.0f, kArcLowKey, kArcHighKey).azimuth == low.azimuth);
}

TEST(a_percussion_note_value_is_already_a_key) {
  CHECK(keyFor(Tuning::PERCUSSION, 36) == 36.0f);
}

TEST(a_tracks_spatial_mode_round_trips_through_the_song_file) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/spatial_chord_ring.xml", provider));
  auto mode_of = [](const Song & s) {
    for (auto id : s.getRootTrackIds()) {
      auto * track = dynamic_cast<const InstrumentTrack *>(s.getMasterTrack().getChildByInternalId(id));
      if (track) return track->getSpatialMode();
    }
    CHECK(false);
    return SpatialMode::AUTO;
  };
  CHECK(mode_of(song) == SpatialMode::RING);

  auto path = std::string(TESTS_SCRATCH_DIR) + "/spatial_mode_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(mode_of(reloaded) == SpatialMode::RING);
  std::filesystem::remove(path);

  // An unset mode is not written, and reads back as AUTO.
  Song plain;
  CHECK(plain.open(std::string(TESTS_FIXTURES_DIR) + "/spatial_chord_none.xml", provider));
  CHECK(mode_of(plain) == SpatialMode::AUTO);
}
