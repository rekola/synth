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
