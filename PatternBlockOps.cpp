#include "PatternBlockOps.h"

#include "Pattern.h"

using namespace std;

PatternBlock
copyPatternBlock(const Pattern & pattern, int row_lo, int row_hi,
		 const vector<int> & track_ids, int track_lo, int track_hi) {
  PatternBlock block;

  for (int row = row_lo; row <= row_hi; row++) {
    vector<PatternBlockCell> row_cells;
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      PatternBlockCell cell;
      cell.notes = pattern.getNotes(row, track_id);
      cell.command = pattern.getCommand(row, track_id);
      row_cells.push_back(move(cell));
    }
    block.push_back(move(row_cells));
  }

  return block;
}

void
clearPatternBlock(Pattern & pattern, int row_lo, int row_hi,
		  const vector<int> & track_ids, int track_lo, int track_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      pattern.clearNotes(row, track_id);
      pattern.setCommand(row, track_id, Command());
    }
  }
}

void
transposePatternBlock(Pattern & pattern, int row_lo, int row_hi,
		      const vector<int> & track_ids, int track_lo, int track_hi, bool up,
		      const std::function<bool(int track_id)> & is_percussion) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      if (is_percussion(track_id)) continue;
      auto notes = pattern.getNotes(row, track_id);
      if (notes.empty()) continue; // don't materialize a real entry in the sparse notes_ map
      for (auto & note : notes) {
	if (up) note.transposeUp();
	else note.transposeDown();
      }
      pattern.setNotes(row, track_id, notes);
    }
  }
}

void
pastePatternBlock(Pattern & pattern, const PatternBlock & block,
		  int target_row, const vector<int> & track_ids, int target_track) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= pattern.getNumRows()) continue;

    auto & row_cells = block[row_offset];
    for (size_t track_offset = 0; track_offset < row_cells.size(); track_offset++) {
      int t = target_track + static_cast<int>(track_offset);
      if (t < 0 || t >= static_cast<int>(track_ids.size())) continue;

      auto track_id = track_ids[t];
      auto & cell = row_cells[track_offset];
      pattern.setNotes(row, track_id, cell.notes);
      pattern.setCommand(row, track_id, cell.command);
    }
  }
}

PatternBlock
copyPatternBlockNotes(const Pattern & pattern, int row_lo, int row_hi,
		      int track_id, int note_lo, int note_hi, bool include_command) {
  PatternBlock block;

  for (int row = row_lo; row <= row_hi; row++) {
    auto & full_notes = pattern.getNotes(row, track_id);
    PatternBlockCell cell;
    cell.note_offset = note_lo;
    auto size = static_cast<int>(full_notes.size());
    if (note_lo < size) {
      auto hi = min(note_hi + 1, size);
      cell.notes = vector<Note>(full_notes.begin() + note_lo, full_notes.begin() + hi);
    }
    if (include_command) cell.command = pattern.getCommand(row, track_id);
    block.push_back({move(cell)});
  }

  return block;
}

void
clearPatternBlockNotes(Pattern & pattern, int row_lo, int row_hi,
		       int track_id, int note_lo, int note_hi, bool include_command) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int i = note_lo; i <= note_hi; i++) {
      pattern.deleteNote(row, track_id, i);
    }
    if (include_command) pattern.setCommand(row, track_id, Command());
  }
}

void
transposePatternBlockNotes(Pattern & pattern, int row_lo, int row_hi,
			   int track_id, int note_lo, int note_hi, bool up, bool is_percussion) {
  if (is_percussion) return;
  for (int row = row_lo; row <= row_hi; row++) {
    auto notes = pattern.getNotes(row, track_id);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) {
      if (up) notes[i].transposeUp();
      else notes[i].transposeDown();
    }
    pattern.setNotes(row, track_id, notes);
  }
}

void
pastePatternBlockNotes(Pattern & pattern, const PatternBlock & block,
		       int target_row, int track_id, int target_note_offset, bool include_command) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= pattern.getNumRows()) continue;

    auto & row_cells = block[row_offset];
    if (row_cells.empty()) continue;
    auto & cell = row_cells[0];
    for (size_t i = 0; i < cell.notes.size(); i++) {
      pattern.setNote(row, track_id, target_note_offset + static_cast<int>(i), cell.notes[i]);
    }
    if (include_command) pattern.setCommand(row, track_id, cell.command);
  }
}
