#include "TestFramework.h"

#include "../src/model/SongStructure.h"
#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/Group.h"
#include "../src/effects/Amplifier.h"

using namespace std;

TEST(song_structure_numbers_root_tracks_in_encounter_order) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<PercussionTrack>()); // no lanes
  auto & c = dynamic_cast<PercussionTrack &>(song.addTrack(make_unique<PercussionTrack>()));
  c.addLane(36); // step-sequenced - a different layout branch than b's

  SongStructure structure(song);
  CHECK(structure.getOrdinalFor(a) == 0);
  CHECK(structure.getOrdinalFor(b) == 1);
  CHECK(structure.getOrdinalFor(c) == 2);
  vector<int> expected = { a.getInternalId(), b.getInternalId(), c.getInternalId(), song.getMasterTrack().getInternalId() };
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
  vector<int> expected = { inner.getInternalId(), song.getMasterTrack().getInternalId() };
  CHECK(structure.getOrderedTrackIds() == expected);
}

TEST(song_structure_gives_a_wrapping_effect_its_own_ordinal_as_well_as_its_child) {
  Song song;
  auto & effect = song.addTrack(make_unique<Amplifier>());
  auto & inner = effect.addChild(make_unique<InstrumentTrack>(0));

  SongStructure structure(song);
  // Child visited (and assigned) before the effect itself - see
  // SongStructure::visit()'s own EFFECT branch: the effect-command column
  // sits to the right of what it wraps, not in front of it.
  CHECK(structure.getOrdinalFor(inner) == 0);
  CHECK(structure.getOrdinalFor(effect) == 1);
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

TEST(song_structure_baseline_is_a_wide_waveform_column_only_for_sample_tracks) {
  Song song;
  auto & sample = song.addTrack(make_unique<SampleTrack>());
  SongStructure structure(song);

  auto & sample_info = structure.getBaselineInfo(sample.getInternalId());
  // The waveform placeholder column (PatternEditor's own waveform-box
  // rendering) only - no command column, unlike every other leaf track
  // type: a SampleTrack's own clip content is raw audio, not a Pattern,
  // so there's no per-row Command data for one to ever hold.
  CHECK(sample_info.getColumnCount() == 1);
  CHECK(sample_info.getColumnType(0) == ColumnType::NOTE); // the waveform placeholder - no dedicated ColumnType of its own, see VisibleTrackInfo::getColumnWidth()'s own comment
  CHECK(sample_info.sample_placeholder_width_ > 4); // much wider than an ordinary NOTE column
  CHECK(sample_info.color_ordinal_ >= 0); // color-eligible, same as every other leaf track
}

// The static_assert in SongStructure.cpp's own SAMPLE branch already
// guards sample_placeholder_width_ at compile time - this verifies the
// actual geometry it exists to protect: PatternEditor::renderRow()'s
// three waveform-box reference lines (0 amplitude at the center, 0.5
// amplitude at the quarter/three-quarter columns) land with exactly
// equal spacing on every side, not skewed toward one edge.
TEST(song_structure_sample_placeholder_width_gives_evenly_spaced_reference_lines) {
  Song song;
  auto & sample = song.addTrack(make_unique<SampleTrack>());
  SongStructure structure(song);

  auto & sample_info = structure.getBaselineInfo(sample.getInternalId());
  // Same "- 2" derivation PatternEditor::renderRow()'s own SAMPLE branch
  // uses to get from getColumnWidth() to the actually-drawn width.
  auto width = sample_info.getColumnWidth(0) - 2;
  auto quarter = width / 4, half = width / 2, three_quarter = 3 * width / 4;

  auto gap_before = quarter;
  auto gap_mid1 = half - quarter - 1;
  auto gap_mid2 = three_quarter - half - 1;
  auto gap_after = width - 1 - three_quarter;
  CHECK(gap_before == gap_mid1);
  CHECK(gap_mid1 == gap_mid2);
  CHECK(gap_mid2 == gap_after);
  CHECK(gap_before > 0); // a real gap, not the lines flush against each other/the edges
}

TEST(song_structure_gives_a_drum_machine_track_one_note_only_column_per_lane_plus_effect) {
  // A step-sequenced PercussionTrack's step content is an ordinary
  // per-scene Pattern (like InstrumentTrack/a lane-less PercussionTrack),
  // not a track-global sequence - it must get real columns, not the single
  // placeholder column SampleTrack still gets. But unlike a regular
  // InstrumentTrack/lane-less PercussionTrack, its columns are the
  // step-sequencer's own compact shape: one NOTE-only cell per lane (no
  // velocity/delay), plus the shared per-row effect/command column.
  Song song;
  auto & raw_drum = song.addTrack(make_unique<PercussionTrack>());
  auto & drum = dynamic_cast<PercussionTrack &>(raw_drum);
  for (int note : { 36, 38, 42 }) drum.addLane(note);
  SongStructure structure(song);

  auto & drum_info = structure.getBaselineInfo(drum.getInternalId());
  CHECK(drum_info.has_note_column_);
  CHECK(drum_info.num_velocity_columns_ == 0);
  CHECK(!drum_info.has_delay_column_);
  CHECK(drum_info.has_effect_column_);
  CHECK(drum_info.num_subtracks_ == 3); // one column per lane
  CHECK(drum_info.getColumnCount() == 4); // 3 lane columns + effect
}

TEST(song_structure_gives_every_instrument_track_type_a_color_ordinal) {
  Song song;
  auto & instrument = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & percussion = song.addTrack(make_unique<PercussionTrack>()); // no lanes
  auto & drum = dynamic_cast<PercussionTrack &>(song.addTrack(make_unique<PercussionTrack>()));
  drum.addLane(36); // step-sequenced - a different layout branch than percussion's
  auto & sample = song.addTrack(make_unique<SampleTrack>());
  SongStructure structure(song);

  CHECK(structure.getBaselineInfo(instrument.getInternalId()).color_ordinal_ == 0);
  CHECK(structure.getBaselineInfo(percussion.getInternalId()).color_ordinal_ == 1);
  CHECK(structure.getBaselineInfo(drum.getInternalId()).color_ordinal_ == 2);
  CHECK(structure.getBaselineInfo(sample.getInternalId()).color_ordinal_ == 3);
}

TEST(song_structure_gives_no_color_ordinal_to_an_effect_track) {
  Song song;
  auto & effect = song.addTrack(make_unique<Amplifier>());
  SongStructure structure(song);
  CHECK(structure.getBaselineInfo(effect.getInternalId()).color_ordinal_ == -1);
}

// The whole reason color_ordinal_ is its own counter, not derived from
// getOrdinalFor()/getOrderedTrackIds().size() - an interleaved, non-color-
// eligible track (here an Effect) must never "use up" a color ordinal, or
// two instrument tracks could end up with less-distinct colors purely
// because of how many effects happen to sit between them.
TEST(song_structure_color_ordinal_skips_interleaved_non_eligible_tracks) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  song.addTrack(make_unique<Amplifier>());
  auto & b = song.addTrack(make_unique<PercussionTrack>());
  SongStructure structure(song);

  CHECK(structure.getBaselineInfo(a.getInternalId()).color_ordinal_ == 0);
  CHECK(structure.getBaselineInfo(b.getInternalId()).color_ordinal_ == 1);
}
