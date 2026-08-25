#include "TestFramework.h"

#include "../src/model/Scene.h"

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

TEST(scene_set_pattern_for_track_replaces_the_whole_pattern) {
  Scene scene;
  scene.setNote(0, 1, 0, Note(60, 100));
  scene.setNote(1, 1, 0, Note(62, 100));

  Pattern replacement;
  replacement.setNote(5, 0, Note(67, 100));
  scene.setPatternForTrack(1, replacement);

  CHECK(scene.getNotes(0, 1).empty());
  CHECK(scene.getNote(5, 1, 0).getValue() == 67);
}

TEST(scene_set_pattern_for_track_is_a_deep_copy) {
  Scene source;
  source.setNote(0, 1, 0, Note(60, 100));

  Scene dest;
  dest.setPatternForTrack(1, source.getPatternsByTrack().at(1));

  // Mutating the destination must never reach back into the source.
  dest.setNote(0, 1, 0, Note(72, 100));

  CHECK(source.getNote(0, 1, 0).getValue() == 60);
  CHECK(dest.getNote(0, 1, 0).getValue() == 72);
}
