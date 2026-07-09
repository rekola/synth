#include "TestFramework.h"

#include "../PatternBlockOps.h"
#include "../Pattern.h"

#include <vector>

using namespace std;

TEST(pattern_block_copy_captures_notes_and_commands) {
  Pattern p(16);
  vector<int> track_ids = {10, 20, 30};

  p.setNote(2, track_ids[0], 0, Note(60, 100));
  p.setNote(2, track_ids[1], 0, Note(64, 100));
  p.setCommand(2, track_ids[0], Command("U050"));

  auto block = copyPatternBlock(p, 2, 3, track_ids, 0, 1);

  CHECK(block.size() == 2); // rows 2 and 3
  CHECK(block[0].size() == 2); // tracks 0 and 1

  CHECK(block[0][0].notes.size() == 1);
  CHECK(block[0][0].notes[0].getValue() == 60);
  CHECK(block[0][0].command.isDefined());

  CHECK(block[0][1].notes.size() == 1);
  CHECK(block[0][1].notes[0].getValue() == 64);
  CHECK(!block[0][1].command.isDefined());

  // row 3 was never written, so it should come back empty
  CHECK(block[1][0].notes.empty());
}

TEST(pattern_block_clear_empties_the_range) {
  Pattern p(16);
  vector<int> track_ids = {10, 20};

  p.setNote(1, track_ids[0], 0, Note(60, 100));
  p.setNote(1, track_ids[1], 0, Note(64, 100));
  p.setCommand(1, track_ids[0], Command("U050"));
  p.setNote(5, track_ids[0], 0, Note(67, 100)); // outside the cleared range

  clearPatternBlock(p, 0, 2, track_ids, 0, 1);

  CHECK(p.getNotes(1, track_ids[0]).empty());
  CHECK(p.getNotes(1, track_ids[1]).empty());
  CHECK(!p.getCommand(1, track_ids[0]).isDefined());

  // untouched row outside range survives
  CHECK(p.getNotes(5, track_ids[0]).size() == 1);
  CHECK(p.getNotes(5, track_ids[0])[0].getValue() == 67);
}

TEST(pattern_block_paste_writes_at_an_offset) {
  Pattern p(16);
  vector<int> track_ids = {10, 20, 30};

  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setCommand(0, track_ids[0], Command("U050"));

  auto block = copyPatternBlock(p, 0, 0, track_ids, 0, 0);
  pastePatternBlock(p, block, 5, track_ids, 1);

  CHECK(p.getNotes(5, track_ids[1]).size() == 1);
  CHECK(p.getNotes(5, track_ids[1])[0].getValue() == 60);
  CHECK(p.getCommand(5, track_ids[1]).isDefined());

  // original cell untouched by the paste
  CHECK(p.getNotes(0, track_ids[0]).size() == 1);
}

TEST(pattern_block_paste_clips_at_row_and_track_boundaries) {
  Pattern p(4); // only rows 0..3 exist
  vector<int> track_ids = {10, 20};

  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setNote(1, track_ids[0], 0, Note(61, 100));

  auto block = copyPatternBlock(p, 0, 1, track_ids, 0, 0); // 2 rows, 1 track

  // paste near the bottom edge: row offset 1 would land on row 4, out of range
  pastePatternBlock(p, block, 3, track_ids, 0);
  CHECK(p.getNotes(3, track_ids[0]).size() == 1);
  CHECK(p.getNotes(3, track_ids[0])[0].getValue() == 60);
  // no crash/throw for the clipped out-of-range row - nothing to assert beyond reaching here

  // paste past the last track: target_track = 1 (last valid index), block has
  // only 1 track column so nothing should be out of bounds here; use an
  // explicitly out-of-range target instead
  pastePatternBlock(p, block, 0, track_ids, static_cast<int>(track_ids.size())); // fully out of range
  // should not have thrown or corrupted existing data
  CHECK(p.getNotes(0, track_ids[0]).size() == 1);
  CHECK(p.getNotes(0, track_ids[0])[0].getValue() == 60);
}

TEST(pattern_block_cut_then_paste_back_round_trips) {
  Pattern p(16);
  vector<int> track_ids = {10, 20, 30};

  p.setNote(3, track_ids[0], 0, Note(60, 100));
  p.setNote(3, track_ids[1], 0, Note(64, 100));
  p.setCommand(4, track_ids[2], Command("D0A0"));

  auto block = copyPatternBlock(p, 3, 4, track_ids, 0, 2);
  clearPatternBlock(p, 3, 4, track_ids, 0, 2);

  CHECK(p.getNotes(3, track_ids[0]).empty());
  CHECK(!p.getCommand(4, track_ids[2]).isDefined());

  pastePatternBlock(p, block, 3, track_ids, 0);

  CHECK(p.getNotes(3, track_ids[0]).size() == 1);
  CHECK(p.getNotes(3, track_ids[0])[0].getValue() == 60);
  CHECK(p.getNotes(3, track_ids[1]).size() == 1);
  CHECK(p.getNotes(3, track_ids[1])[0].getValue() == 64);
  CHECK(p.getCommand(4, track_ids[2]).isDefined());
}

TEST(pattern_block_chord_round_trips_with_every_voice_intact) {
  Pattern p(16);
  vector<int> track_ids = {10};

  // a C-Eb-G chord as three simultaneous voices on one track/row
  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setNote(0, track_ids[0], 1, Note(63, 100));
  p.setNote(0, track_ids[0], 2, Note(67, 100));

  auto block = copyPatternBlock(p, 0, 0, track_ids, 0, 0);
  clearPatternBlock(p, 0, 0, track_ids, 0, 0);
  CHECK(p.getNotes(0, track_ids[0]).empty());

  pastePatternBlock(p, block, 8, track_ids, 0);

  auto & notes = p.getNotes(8, track_ids[0]);
  CHECK(notes.size() == 3);
  CHECK(notes[0].getValue() == 60);
  CHECK(notes[1].getValue() == 63);
  CHECK(notes[2].getValue() == 67);
}
