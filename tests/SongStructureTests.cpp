#include "TestFramework.h"

#include "../src/model/SongStructure.h"
#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/DrumMachineTrack.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/Group.h"
#include "../src/effects/Amplifier.h"

using namespace std;

TEST(song_structure_numbers_root_tracks_in_encounter_order) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<PercussionTrack>());
  auto & c = song.addTrack(make_unique<DrumMachineTrack>());

  SongStructure structure(song);
  CHECK(structure.getOrdinalFor(a) == 0);
  CHECK(structure.getOrdinalFor(b) == 1);
  CHECK(structure.getOrdinalFor(c) == 2);
  vector<int> expected = { a.getInternalId(), b.getInternalId(), c.getInternalId() };
  CHECK(structure.getOrderedTrackIds() == expected);
}

TEST(song_structure_gives_no_ordinal_to_an_unrecognized_id) {
  Song song;
  song.addTrack(make_unique<InstrumentTrack>(0));
  SongStructure structure(song);
  CHECK(structure.getOrdinalFor(999999) == -1);
}

TEST(song_structure_recurses_into_a_group_but_gives_the_group_itself_no_ordinal) {
  Song song;
  auto & group = song.addTrack(make_unique<Group>());
  auto & inner = group.addChild(make_unique<InstrumentTrack>(0));

  SongStructure structure(song);
  CHECK(structure.getOrdinalFor(group) == -1);
  CHECK(structure.getOrdinalFor(inner) == 0);
  vector<int> expected = { inner.getInternalId() };
  CHECK(structure.getOrderedTrackIds() == expected);
}

TEST(song_structure_gives_a_wrapping_effect_its_own_ordinal_as_well_as_its_child) {
  Song song;
  auto & effect = song.addTrack(make_unique<Amplifier>());
  auto & inner = effect.addChild(make_unique<InstrumentTrack>(0));

  SongStructure structure(song);
  // Effect visited (and assigned) before recursing into its child - see
  // SongStructure::visit()'s own EFFECT branch.
  CHECK(structure.getOrdinalFor(effect) == 0);
  CHECK(structure.getOrdinalFor(inner) == 1);
}

TEST(song_structure_gives_a_childless_effect_its_own_ordinal_too) {
  Song song;
  auto & effect = song.addTrack(make_unique<Amplifier>());
  SongStructure structure(song);
  CHECK(structure.getOrdinalFor(effect) == 0);
  CHECK(structure.getBaselineInfo(effect.getInternalId()).has_effect_column_);
  CHECK(!structure.getBaselineInfo(effect.getInternalId()).has_note_column_);
}

TEST(song_structure_baseline_matches_instrument_track_own_column_settings) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  SongStructure structure(song);
  auto & info = structure.getBaselineInfo(track.getInternalId());
  CHECK(info.has_note_column_);
  CHECK(info.num_velocity_columns_ == 1);
  CHECK(info.has_delay_column_);
  CHECK(info.has_effect_column_);
}

TEST(song_structure_baseline_is_a_single_placeholder_column_for_sample_and_drum_machine_tracks) {
  Song song;
  auto & sample = song.addTrack(make_unique<SampleTrack>(nullptr));
  auto & drum = song.addTrack(make_unique<DrumMachineTrack>());
  SongStructure structure(song);

  auto & sample_info = structure.getBaselineInfo(sample.getInternalId());
  CHECK(sample_info.getColumnCount() == 1);

  auto & drum_info = structure.getBaselineInfo(drum.getInternalId());
  CHECK(drum_info.getColumnCount() == 1);
}
