#include "TestFramework.h"

#include "../PatternBlockOps.h"
#include "../Pattern.h"

#include <vector>

using namespace std;

TEST(pattern_block_copy_captures_notes_and_commands) {
  Pattern p;
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
  Pattern p;
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

TEST(pattern_block_transpose_skips_percussion_tracks_within_a_mixed_range) {
  // A percussion track's Note::getValue() selects a drum sound (MIDI key),
  // not a pitch - transposing it would silently swap to a different,
  // unrelated drum, so it must be left untouched even when it sits inside
  // an otherwise-transposed multi-track range.
  Pattern p;
  vector<int> track_ids = {10, 20, 30};

  p.setNote(2, track_ids[0], 0, Note(60, 100));
  p.setNote(2, track_ids[1], 0, Note(64, 100)); // percussion - must not move
  p.setNote(2, track_ids[2], 0, Note(67, 100));

  transposePatternBlock(p, 2, 2, track_ids, 0, 2, /*up=*/true,
			[&](int track_id) { return track_id == track_ids[1]; });

  CHECK(p.getNotes(2, track_ids[0])[0].getValue() == 61); // transposed up
  CHECK(p.getNotes(2, track_ids[1])[0].getValue() == 64); // untouched (percussion)
  CHECK(p.getNotes(2, track_ids[2])[0].getValue() == 68); // transposed up
}

TEST(pattern_block_paste_writes_at_an_offset) {
  Pattern p;
  vector<int> track_ids = {10, 20, 30};

  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setCommand(0, track_ids[0], Command("U050"));

  auto block = copyPatternBlock(p, 0, 0, track_ids, 0, 0);
  pastePatternBlock(p, block, 16, 5, track_ids, 1);

  CHECK(p.getNotes(5, track_ids[1]).size() == 1);
  CHECK(p.getNotes(5, track_ids[1])[0].getValue() == 60);
  CHECK(p.getCommand(5, track_ids[1]).isDefined());

  // original cell untouched by the paste
  CHECK(p.getNotes(0, track_ids[0]).size() == 1);
}

TEST(pattern_block_paste_clips_at_row_and_track_boundaries) {
  Pattern p; // only rows 0..3 exist
  vector<int> track_ids = {10, 20};

  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setNote(1, track_ids[0], 0, Note(61, 100));

  auto block = copyPatternBlock(p, 0, 1, track_ids, 0, 0); // 2 rows, 1 track

  // paste near the bottom edge: row offset 1 would land on row 4, out of range
  pastePatternBlock(p, block, 4, 3, track_ids, 0);
  CHECK(p.getNotes(3, track_ids[0]).size() == 1);
  CHECK(p.getNotes(3, track_ids[0])[0].getValue() == 60);
  // no crash/throw for the clipped out-of-range row - nothing to assert beyond reaching here

  // paste past the last track: target_track = 1 (last valid index), block has
  // only 1 track column so nothing should be out of bounds here; use an
  // explicitly out-of-range target instead
  pastePatternBlock(p, block, 4, 0, track_ids, static_cast<int>(track_ids.size())); // fully out of range
  // should not have thrown or corrupted existing data
  CHECK(p.getNotes(0, track_ids[0]).size() == 1);
  CHECK(p.getNotes(0, track_ids[0])[0].getValue() == 60);
}

TEST(pattern_block_cut_then_paste_back_round_trips) {
  Pattern p;
  vector<int> track_ids = {10, 20, 30};

  p.setNote(3, track_ids[0], 0, Note(60, 100));
  p.setNote(3, track_ids[1], 0, Note(64, 100));
  p.setCommand(4, track_ids[2], Command("D0A0"));

  auto block = copyPatternBlock(p, 3, 4, track_ids, 0, 2);
  clearPatternBlock(p, 3, 4, track_ids, 0, 2);

  CHECK(p.getNotes(3, track_ids[0]).empty());
  CHECK(!p.getCommand(4, track_ids[2]).isDefined());

  pastePatternBlock(p, block, 16, 3, track_ids, 0);

  CHECK(p.getNotes(3, track_ids[0]).size() == 1);
  CHECK(p.getNotes(3, track_ids[0])[0].getValue() == 60);
  CHECK(p.getNotes(3, track_ids[1]).size() == 1);
  CHECK(p.getNotes(3, track_ids[1])[0].getValue() == 64);
  CHECK(p.getCommand(4, track_ids[2]).isDefined());
}

TEST(pattern_block_chord_round_trips_with_every_voice_intact) {
  Pattern p;
  vector<int> track_ids = {10};

  // a C-Eb-G chord as three simultaneous voices on one track/row
  p.setNote(0, track_ids[0], 0, Note(60, 100));
  p.setNote(0, track_ids[0], 1, Note(63, 100));
  p.setNote(0, track_ids[0], 2, Note(67, 100));

  auto block = copyPatternBlock(p, 0, 0, track_ids, 0, 0);
  clearPatternBlock(p, 0, 0, track_ids, 0, 0);
  CHECK(p.getNotes(0, track_ids[0]).empty());

  pastePatternBlock(p, block, 16, 8, track_ids, 0);

  auto & notes = p.getNotes(8, track_ids[0]);
  CHECK(notes.size() == 3);
  CHECK(notes[0].getValue() == 60);
  CHECK(notes[1].getValue() == 63);
  CHECK(notes[2].getValue() == 67);
}

// Note-column-scoped (single-track) variants, used when a selection is
// narrowed to a subset of one track's simultaneous note columns.

TEST(pattern_block_notes_copy_captures_only_the_requested_column_range) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setNote(2, track_id, 2, Note(67, 100));
  p.setCommand(2, track_id, Command("U050"));

  auto block = copyPatternBlockNotes(p, 2, 2, track_id, 1, 2);

  CHECK(block.size() == 1);
  CHECK(block[0].size() == 1);
  CHECK(block[0][0].note_offset == 1);
  CHECK(block[0][0].notes.size() == 2);
  CHECK(block[0][0].notes[0].getValue() == 63);
  CHECK(block[0][0].notes[1].getValue() == 67);
  // the effect column isn't part of any note column - must not be captured
  CHECK(!block[0][0].command.isDefined());
}

TEST(pattern_block_notes_clear_only_touches_the_requested_column_range) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setNote(2, track_id, 2, Note(67, 100));
  p.setCommand(2, track_id, Command("U050"));

  clearPatternBlockNotes(p, 2, 2, track_id, 1, 1);

  auto & notes = p.getNotes(2, track_id);
  CHECK(notes.size() == 3); // deleteNote only clears trailing entries, not middle ones
  CHECK(notes[0].getValue() == 60); // untouched
  CHECK(!notes[1].isDefined()); // cleared
  CHECK(notes[2].getValue() == 67); // untouched
  CHECK(p.getCommand(2, track_id).isDefined()); // effect column untouched
}

TEST(pattern_block_notes_transpose_only_touches_the_requested_column_range) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setNote(2, track_id, 2, Note(67, 100));

  transposePatternBlockNotes(p, 2, 2, track_id, 1, 2, true, /*is_percussion=*/false);

  auto & notes = p.getNotes(2, track_id);
  CHECK(notes[0].getValue() == 60); // untouched
  CHECK(notes[1].getValue() == 64); // transposed up
  CHECK(notes[2].getValue() == 68); // transposed up
}

TEST(pattern_block_notes_transpose_is_a_no_op_for_a_percussion_track) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));

  transposePatternBlockNotes(p, 2, 2, track_id, 0, 1, /*up=*/true, /*is_percussion=*/true);

  auto & notes = p.getNotes(2, track_id);
  CHECK(notes[0].getValue() == 60); // untouched - would be 61 if transposed
  CHECK(notes[1].getValue() == 63); // untouched - would be 64 if transposed
}

// Note-column-scoped copy/clear/paste never touch the row's Command - a
// selection spanning both a note column and the effect column escalates to
// a whole-track operation instead (see PatternEditor::getEffectiveSelectionBounds()),
// so this family has no include-the-command variant any more.
TEST(pattern_block_notes_copy_and_clear_never_touch_the_command) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setCommand(2, track_id, Command("U050"));

  auto block = copyPatternBlockNotes(p, 2, 2, track_id, 0, 1);
  CHECK(!block[0][0].command.isDefined());

  clearPatternBlockNotes(p, 2, 2, track_id, 0, 1);
  CHECK(p.getCommand(2, track_id).isDefined()); // untouched
}

TEST(pattern_block_notes_paste_never_touches_the_command) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  auto block = copyPatternBlockNotes(p, 2, 2, track_id, 0, 0);

  p.setCommand(9, track_id, Command("D0A0"));
  pastePatternBlockNotes(p, block, 16, 9, track_id, 0);
  CHECK(p.getNotes(9, track_id)[0].getValue() == 60);
  CHECK(p.getCommand(9, track_id).isDefined()); // untouched, still the original
}

// SelectionScope::COMMAND's own family (PatternEditor::getEffectiveSelectionBounds()
// resolves a selection confined to just the effect column to this scope) -
// independent of any note data on the same row.
TEST(pattern_block_command_copy_and_clear_round_trip_independent_of_notes) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setCommand(2, track_id, Command("U050"));

  auto block = copyPatternBlockCommand(p, 2, 2, track_id);
  CHECK(block.size() == 1);
  CHECK(block[0].isDefined());

  clearPatternBlockCommand(p, 2, 2, track_id);
  CHECK(!p.getCommand(2, track_id).isDefined());
  CHECK(p.getNotes(2, track_id)[0].getValue() == 60); // untouched
}

TEST(pattern_block_command_paste_never_touches_note_data) {
  Pattern p;
  int track_id = 10;

  p.setCommand(2, track_id, Command("U050"));
  auto block = copyPatternBlockCommand(p, 2, 2, track_id);

  p.setNote(9, track_id, 0, Note(67, 100));
  pastePatternBlockCommand(p, block, 16, 9, track_id);
  CHECK(p.getCommand(9, track_id).isDefined());
  CHECK(p.getNotes(9, track_id)[0].getValue() == 67); // untouched

  // clips at the pattern-length boundary the same way pastePatternBlock does
  pastePatternBlockCommand(p, block, 16, 15, track_id);
  CHECK(p.getCommand(15, track_id).isDefined());
}

TEST(pattern_block_notes_paste_merges_into_target_range_without_clobbering_others) {
  Pattern p;
  int track_id = 10;

  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setNote(2, track_id, 2, Note(67, 100));

  auto block = copyPatternBlockNotes(p, 2, 2, track_id, 1, 2); // voices 1,2: 63,67

  // paste that pair into a different row, at a different note-column offset (0)
  pastePatternBlockNotes(p, block, 16, 9, track_id, 0);

  auto & notes = p.getNotes(9, track_id);
  CHECK(notes.size() == 2);
  CHECK(notes[0].getValue() == 63);
  CHECK(notes[1].getValue() == 67);

  // pasting into a row that already has other voices merges rather than
  // replacing the whole vector
  p.setNote(10, track_id, 0, Note(48, 100));
  p.setNote(10, track_id, 2, Note(72, 100));
  pastePatternBlockNotes(p, block, 16, 10, track_id, 1); // target voices 1,2

  auto & merged = p.getNotes(10, track_id);
  CHECK(merged.size() == 3);
  CHECK(merged[0].getValue() == 48); // untouched, outside the pasted range
  CHECK(merged[1].getValue() == 63); // overwritten by the paste
  CHECK(merged[2].getValue() == 67); // overwritten by the paste
}

TEST(pattern_block_notes_paste_overwrites_gaps_left_by_a_sparser_source_row) {
  Pattern p;
  int track_id = 10;

  // Source: row 2 only has a note in column 0 - columns 1 and 2 are
  // undefined gaps within the copied range.
  p.setNote(2, track_id, 0, Note(60, 100));
  p.setNote(2, track_id, 1, Note(63, 100));
  p.setNote(2, track_id, 2, Note(67, 100));
  p.deleteNote(2, track_id, 2);
  p.deleteNote(2, track_id, 1);
  CHECK(p.getNotes(2, track_id).size() == 1); // columns 1,2 are gaps, not just undefined-in-place

  auto block = copyPatternBlockNotes(p, 2, 2, track_id, 0, 2);

  // Destination already has real notes in all 3 columns - the gaps in the
  // copied source must overwrite them with "undefined", not leave them.
  p.setNote(9, track_id, 0, Note(48, 100));
  p.setNote(9, track_id, 1, Note(52, 100));
  p.setNote(9, track_id, 2, Note(55, 100));

  pastePatternBlockNotes(p, block, 16, 9, track_id, 0);

  auto & notes = p.getNotes(9, track_id);
  CHECK(notes.size() >= 1);
  CHECK(notes[0].getValue() == 60); // overwritten by the paste
  CHECK((notes.size() < 2 || !notes[1].isDefined())); // gap overwrote the stale note
  CHECK((notes.size() < 3 || !notes[2].isDefined())); // gap overwrote the stale note
}
