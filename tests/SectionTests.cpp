#include "TestFramework.h"

#include "../src/model/Section.h"
#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/InstrumentProvider.h"

#include <filesystem>
#include <string>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

TEST(pattern_is_empty_when_never_touched) {
  Pattern p;
  CHECK(p.isEmpty());
}

TEST(pattern_is_not_empty_with_a_note) {
  Pattern p;
  p.setNote(0, 0, Note(60, 100));
  CHECK(!p.isEmpty());
}

TEST(pattern_is_not_empty_with_only_a_command) {
  Pattern p;
  p.setCommand(0, Command("U050"));
  CHECK(!p.isEmpty());
}

TEST(pattern_is_empty_again_after_clearing_its_only_note) {
  Pattern p;
  p.setNote(0, 0, Note(60, 100));
  p.deleteNote(0, 0);
  CHECK(p.isEmpty());
}

TEST(pattern_has_sounding_note_with_a_note_on) {
  Pattern p;
  p.setNote(0, 0, Note(60, 100));
  CHECK(p.hasSoundingNote());
}

TEST(pattern_has_no_sounding_note_with_only_a_note_off) {
  Pattern p;
  p.setNote(0, 0, Note(60, 0)); // value defined, velocity 0 - a note-off
  CHECK(!p.isEmpty());
  CHECK(!p.hasSoundingNote());
}

TEST(pattern_has_no_sounding_note_with_only_aftertouch) {
  Pattern p;
  p.setNote(0, 0, Note(-1, 100)); // no value, velocity - aftertouch
  CHECK(!p.isEmpty());
  CHECK(!p.hasSoundingNote());
}

TEST(pattern_has_no_sounding_note_with_only_a_command) {
  Pattern p;
  p.setCommand(0, Command("U050"));
  CHECK(!p.isEmpty());
  CHECK(!p.hasSoundingNote());
}

TEST(section_set_pattern_for_track_replaces_the_whole_pattern) {
  Section section;
  section.setNote(0, 1, 0, Note(60, 100));
  section.setNote(1, 1, 0, Note(62, 100));

  Pattern replacement;
  replacement.setNote(5, 0, Note(67, 100));
  section.setPatternForTrack(1, replacement);

  CHECK(section.getNotes(0, 1).empty());
  CHECK(section.getNote(5, 1, 0).getValue() == 67);
}

TEST(section_set_pattern_for_track_is_a_deep_copy) {
  Section source;
  source.setNote(0, 1, 0, Note(60, 100));

  Section dest;
  dest.setPatternForTrack(1, source.getPatternsByTrack().at(1));

  // Mutating the destination must never reach back into the source.
  dest.setNote(0, 1, 0, Note(72, 100));

  CHECK(source.getNote(0, 1, 0).getValue() == 60);
  CHECK(dest.getNote(0, 1, 0).getValue() == 72);
}

// Pattern::getEffectiveRow() - see Pattern.h's own comment: 0 (the
// default) is "no length of its own", falling back to context_length
// unchanged; a real length wraps via plain modulo.
TEST(pattern_effective_row_is_unchanged_with_no_length_set) {
  Pattern p;
  CHECK(p.getEffectiveRow(20, 64) == 20);
}

TEST(pattern_effective_row_wraps_once_a_length_is_set) {
  Pattern p;
  p.setLength(16);
  CHECK(p.getEffectiveRow(20, 64) == 4);
  CHECK(p.getEffectiveRow(4, 64) == 4);
  CHECK(p.getEffectiveRow(63, 64) == 15);
}

TEST(pattern_effective_row_needs_no_divisibility_between_length_and_context) {
  Pattern p;
  p.setLength(5);
  // 64 isn't a multiple of 5 - still well-defined, just an uneven last
  // repeat.
  CHECK(p.getEffectiveRow(60, 64) == 0);
  CHECK(p.getEffectiveRow(63, 64) == 3);
}

TEST(pattern_write_redirect_is_readable_from_both_the_written_and_repeated_row) {
  Pattern p;
  p.setLength(16);
  // Writing "row 20" (a repeat, 20 % 16 == 4) must land on the one real
  // row 4 has, not create unreachable data at a literal row 20 - see
  // Pattern.h's own "no dead write" reasoning.
  p.setNote(p.getEffectiveRow(20, 64), 0, Note(60, 100));
  CHECK(p.getNote(4, 0).getValue() == 60);
  CHECK(p.getNote(p.getEffectiveRow(20, 64), 0).getValue() == 60);
  CHECK(p.getNote(p.getEffectiveRow(36, 64), 0).getValue() == 60); // another repeat of the same row
}

TEST(pattern_length_xml_round_trip) {
  Song song;
  song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & section = song.addSection();
  section.setNote(0, song.getRootTrackIds()[0], 0, Note(60, 100));
  section.getPatternsByTrack()[song.getRootTrackIds()[0]].setLength(16);

  auto path = std::string(TESTS_SCRATCH_DIR) + "/pattern_length_round_trip.xml";
  song.save(path);

  Song reloaded;
  InstrumentProvider provider;
  CHECK(reloaded.open(path, provider));
  auto track_id = reloaded.getRootTrackIds()[0];
  CHECK(reloaded.getSection(0).getPatternsByTrack().at(track_id).getLength() == 16);

  std::filesystem::remove(path);
}

TEST(pattern_length_absent_from_xml_when_unset) {
  Song song;
  song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & section = song.addSection();
  section.setNote(0, song.getRootTrackIds()[0], 0, Note(60, 100)); // length_ left at its default (0)

  auto path = std::string(TESTS_SCRATCH_DIR) + "/pattern_length_absent.xml";
  song.save(path);

  Song reloaded;
  InstrumentProvider provider;
  CHECK(reloaded.open(path, provider));
  auto track_id = reloaded.getRootTrackIds()[0];
  CHECK(reloaded.getSection(0).getPatternsByTrack().at(track_id).getLength() == 0);

  std::filesystem::remove(path);
}

// Section::getEffectiveRow() - the row+track_id-keyed convenience callers
// without a direct Pattern reference use (PatternEditor.cpp/
// LaunchpadManager.cpp's own note-entry write sites).
TEST(section_effective_row_falls_back_to_raw_row_for_an_unknown_track) {
  Section section;
  CHECK(section.getEffectiveRow(0, 20, 64) == 20);
}

TEST(section_effective_row_uses_that_tracks_own_pattern_length) {
  Section section;
  section.setNote(0, 1, 0, Note(60, 100));
  section.getPatternsByTrack()[1].setLength(16);
  CHECK(section.getEffectiveRow(1, 20, 64) == 4);
}

TEST(section_insert_row_for_track_shifts_only_that_tracks_own_content) {
  Section section;
  section.setNote(0, 1, 0, Note(60, 100)); // track 1
  section.setNote(0, 2, 0, Note(64, 100)); // track 2
  section.setAnnotation(0, "hello");

  section.insertRowForTrack(1, 0, 8); // insert a blank row at row 0, track 1 only

  // Track 1's own note shifted down to row 1.
  CHECK(section.getNotes(0, 1).empty());
  CHECK(section.getNote(1, 1, 0).getValue() == 60);

  // Track 2 and the row's own annotation are both untouched.
  CHECK(section.getNote(0, 2, 0).getValue() == 64);
  CHECK(section.getAnnotation(0) == "hello");
}

// The arrangement layer's own instance events - independent of any
// track's own Pattern (notes/commands), same as annotations already are.
TEST(section_instance_events_default_to_absent) {
  Section section;
  CHECK(section.getInstance(1, 0).empty());
  CHECK(section.getInstancesForTrack(1).empty());
}

TEST(section_instance_events_round_trip_a_real_clip_and_a_stop) {
  Section section;
  section.setInstance(1, 0, "clip2"); // starting at row 0
  section.setInstance(1, 16, "OFF"); // stop at row 16

  CHECK(section.getInstance(1, 0) == "clip2");
  CHECK(section.getInstance(1, 16) == "OFF");
  CHECK(section.getInstance(1, 8).empty()); // nothing placed there

  auto & track_instances = section.getInstancesForTrack(1);
  CHECK(track_instances.size() == 2);

  // A different track's own instance list is entirely independent.
  CHECK(section.getInstancesForTrack(2).empty());
}

TEST(section_instance_events_can_be_cleared) {
  Section section;
  section.setInstance(1, 0, "clip2");
  section.clearInstance(1, 0);
  CHECK(section.getInstance(1, 0).empty());
  CHECK(section.getInstancesForTrack(1).empty());
}

// getInstancesForTrack() is ordered (std::map), not this class's usual
// unordered_map, precisely so a caller can walk it in row order - insert
// out of order here and confirm it comes back sorted.
TEST(section_instance_events_for_a_track_are_kept_in_row_order) {
  Section section;
  section.setInstance(1, 32, "clip0");
  section.setInstance(1, 0, "clip1");
  section.setInstance(1, 16, "OFF");

  std::vector<unsigned short> rows;
  for (auto & [ row, clip_id ] : section.getInstancesForTrack(1)) rows.push_back(row);
  CHECK(rows.size() == 3);
  if (rows.size() == 3) {
    CHECK(rows[0] == 0);
    CHECK(rows[1] == 16);
    CHECK(rows[2] == 32);
  }
}
