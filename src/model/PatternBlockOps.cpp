#include "PatternBlockOps.h"

#include "../model/Section.h"
#include "../model/Clip.h"
#include "PatternGrid.h"

using namespace std;

namespace {

const vector<Note> kNoNotes;
const Command kNoCommand;

const vector<Note> & notesAt(const PatternGrid & grid, int track_id, int row) {
  int pattern_row;
  auto pattern = grid.find(track_id, row, pattern_row);
  return pattern ? pattern->getNotes(pattern_row) : kNoNotes;
}

const Command & commandAt(const PatternGrid & grid, int track_id, int row) {
  int pattern_row;
  auto pattern = grid.find(track_id, row, pattern_row);
  return pattern ? pattern->getCommand(pattern_row) : kNoCommand;
}

}

PatternBlock
copyPatternBlock(const PatternGrid & grid, int row_lo, int row_hi,
		 const vector<int> & track_ids, int track_lo, int track_hi) {
  PatternBlock block;

  for (int row = row_lo; row <= row_hi; row++) {
    vector<PatternBlockCell> row_cells;
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[static_cast<size_t>(t)];
      PatternBlockCell cell;
      cell.notes = notesAt(grid, track_id, row);
      cell.command = commandAt(grid, track_id, row);
      row_cells.push_back(move(cell));
    }
    block.push_back(move(row_cells));
  }

  return block;
}

void
clearPatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		  const vector<int> & track_ids, int track_lo, int track_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      int pattern_row;
      if (auto pattern = grid.obtain(track_ids[static_cast<size_t>(t)], row, pattern_row)) {
	pattern->clearNotes(pattern_row);
	pattern->setCommand(pattern_row, Command());
      }
    }
  }
}

void
transposePatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		      const vector<int> & track_ids, int track_lo, int track_hi, bool up,
		      const std::function<bool(int track_id)> & is_percussion) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[static_cast<size_t>(t)];
      if (is_percussion(track_id)) continue;
      int pattern_row;
      auto pattern = grid.find(track_id, row, pattern_row);
      if (!pattern) continue;
      auto notes = pattern->getNotes(pattern_row);
      if (notes.empty()) continue; // don't materialize a real entry in the sparse notes_ map
      for (auto & note : notes) note.transpose(up ? 1 : -1);
      pattern->setNotes(pattern_row, notes);
    }
  }
}

void
pastePatternBlock(PatternGrid & grid, const PatternBlock & block, int num_rows,
		  int target_row, const vector<int> & track_ids, int target_track) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    for (size_t track_offset = 0; track_offset < row_cells.size(); track_offset++) {
      int t = target_track + static_cast<int>(track_offset);
      if (t < 0 || t >= static_cast<int>(track_ids.size())) continue;

      int pattern_row;
      auto pattern = grid.obtain(track_ids[static_cast<size_t>(t)], row, pattern_row);
      if (!pattern) continue;
      auto & cell = row_cells[track_offset];
      pattern->setNotes(pattern_row, cell.notes);
      pattern->setCommand(pattern_row, cell.command);
    }
  }
}

PatternBlock
copyPatternBlockNotes(const PatternGrid & grid, int row_lo, int row_hi,
		      int track_id, int note_lo, int note_hi) {
  PatternBlock block;

  auto width = note_hi - note_lo + 1;

  for (int row = row_lo; row <= row_hi; row++) {
    auto & full_notes = notesAt(grid, track_id, row);
    PatternBlockCell cell;
    cell.note_offset = note_lo;
    // Always the full requested width, not just however many notes this
    // particular row actually had defined - pastePatternBlockNotes() only
    // writes as many positions as cell.notes has, so a short vector here
    // (a row sparser than the widest row in the range) left the
    // destination's own stale content in place at the gap instead of
    // overwriting it with the blank the source row actually had there.
    // Note()'s default constructor is exactly that "undefined" value.
    cell.notes.resize(static_cast<size_t>(width));
    auto size = static_cast<int>(full_notes.size());
    for (int i = 0; i < width; i++) {
      auto src_index = note_lo + i;
      if (src_index < size) cell.notes[static_cast<size_t>(i)] = full_notes[static_cast<size_t>(src_index)];
    }
    block.push_back({move(cell)});
  }

  return block;
}

void
clearPatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
		       int track_id, int note_lo, int note_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    for (int i = note_lo; i <= note_hi; i++) {
      pattern->deleteNote(pattern_row, i);
    }
  }
}

void
transposePatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			   int track_id, int note_lo, int note_hi, bool up, bool is_percussion) {
  if (is_percussion) return;
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto notes = pattern->getNotes(pattern_row);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) notes[static_cast<size_t>(i)].transpose(up ? 1 : -1);
    // setNotes()'s vector<Note> move-assign into the unordered_map, fully
    // inlined down from here, trips a known GCC false positive
    // (-Wfree-nonheap-object misattributing the vector's heap buffer as a
    // non-heap pointer) - not a real dangling-pointer bug.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
}

void
pastePatternBlockNotes(PatternGrid & grid, const PatternBlock & block, int num_rows,
		       int target_row, int track_id, int target_note_offset) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    if (row_cells.empty()) continue;
    auto & cell = row_cells[0];
    int pattern_row;
    auto pattern = grid.obtain(track_id, row, pattern_row);
    if (!pattern) continue;
    for (size_t i = 0; i < cell.notes.size(); i++) {
      pattern->setNote(pattern_row, target_note_offset + static_cast<int>(i), cell.notes[i]);
    }
  }
}

vector<Command>
copyPatternBlockCommand(const PatternGrid & grid, int row_lo, int row_hi, int track_id) {
  vector<Command> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(commandAt(grid, track_id, row));
  return block;
}

void
clearPatternBlockCommand(PatternGrid & grid, int row_lo, int row_hi, int track_id) {
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    if (auto pattern = grid.obtain(track_id, row, pattern_row)) pattern->setCommand(pattern_row, Command());
  }
}

void
pastePatternBlockCommand(PatternGrid & grid, const vector<Command> & block, int num_rows,
			 int target_row, int track_id) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    int pattern_row;
    if (auto pattern = grid.obtain(track_id, row, pattern_row)) pattern->setCommand(pattern_row, block[row_offset]);
  }
}

Clip
extractClip(const PatternGrid & grid, int track_id, int row_lo, int row_hi, int rows_per_bar) {
  if (rows_per_bar <= 0) rows_per_bar = 1;
  auto bar_start = (row_lo / rows_per_bar) * rows_per_bar;
  auto span = row_hi - bar_start + 1;
  auto length = ((span + rows_per_bar - 1) / rows_per_bar) * rows_per_bar;

  Clip clip(track_id);
  clip.setLength(length);
  auto & pattern = clip.getLeafPattern();
  for (int row = row_lo; row <= row_hi; row++) {
    auto & notes = notesAt(grid, track_id, row);
    if (!notes.empty()) pattern.setNotes(row - bar_start, notes);
    auto & command = commandAt(grid, track_id, row);
    if (command.isDefined()) pattern.setCommand(row - bar_start, command);
  }
  return clip;
}

vector<string>
copyPatternBlockAnnotations(const Section & section, int row_lo, int row_hi) {
  vector<string> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(section.getAnnotation(row));
  return block;
}

void
clearPatternBlockAnnotations(Section & section, int row_lo, int row_hi) {
  for (int row = row_lo; row <= row_hi; row++) section.setAnnotation(row, "");
}

void
pastePatternBlockAnnotations(Section & section, const vector<string> & block, int num_rows, int target_row) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    section.setAnnotation(row, block[row_offset]);
  }
}
