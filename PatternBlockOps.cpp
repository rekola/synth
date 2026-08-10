#include "PatternBlockOps.h"

#include "Scene.h"

using namespace std;

PatternBlock
copyPatternBlock(const Scene & scene, int row_lo, int row_hi,
		 const vector<int> & track_ids, int track_lo, int track_hi) {
  PatternBlock block;

  for (int row = row_lo; row <= row_hi; row++) {
    vector<PatternBlockCell> row_cells;
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      PatternBlockCell cell;
      cell.notes = scene.getNotes(row, track_id);
      cell.command = scene.getCommand(row, track_id);
      row_cells.push_back(move(cell));
    }
    block.push_back(move(row_cells));
  }

  return block;
}

void
clearPatternBlock(Scene & scene, int row_lo, int row_hi,
		  const vector<int> & track_ids, int track_lo, int track_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      scene.clearNotes(row, track_id);
      scene.setCommand(row, track_id, Command());
    }
  }
}

void
transposePatternBlock(Scene & scene, int row_lo, int row_hi,
		      const vector<int> & track_ids, int track_lo, int track_hi, bool up,
		      const std::function<bool(int track_id)> & is_percussion) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[t];
      if (is_percussion(track_id)) continue;
      auto notes = scene.getNotes(row, track_id);
      if (notes.empty()) continue; // don't materialize a real entry in the sparse notes_ map
      for (auto & note : notes) {
	if (up) note.transposeUp();
	else note.transposeDown();
      }
      scene.setNotes(row, track_id, notes);
    }
  }
}

void
pastePatternBlock(Scene & scene, const PatternBlock & block, int num_rows,
		  int target_row, const vector<int> & track_ids, int target_track) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    for (size_t track_offset = 0; track_offset < row_cells.size(); track_offset++) {
      int t = target_track + static_cast<int>(track_offset);
      if (t < 0 || t >= static_cast<int>(track_ids.size())) continue;

      auto track_id = track_ids[t];
      auto & cell = row_cells[track_offset];
      scene.setNotes(row, track_id, cell.notes);
      scene.setCommand(row, track_id, cell.command);
    }
  }
}

PatternBlock
copyPatternBlockNotes(const Scene & scene, int row_lo, int row_hi,
		      int track_id, int note_lo, int note_hi) {
  PatternBlock block;

  auto width = note_hi - note_lo + 1;

  for (int row = row_lo; row <= row_hi; row++) {
    auto & full_notes = scene.getNotes(row, track_id);
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
clearPatternBlockNotes(Scene & scene, int row_lo, int row_hi,
		       int track_id, int note_lo, int note_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int i = note_lo; i <= note_hi; i++) {
      scene.deleteNote(row, track_id, i);
    }
  }
}

void
transposePatternBlockNotes(Scene & scene, int row_lo, int row_hi,
			   int track_id, int note_lo, int note_hi, bool up, bool is_percussion) {
  if (is_percussion) return;
  for (int row = row_lo; row <= row_hi; row++) {
    auto notes = scene.getNotes(row, track_id);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) {
      if (up) notes[i].transposeUp();
      else notes[i].transposeDown();
    }
    scene.setNotes(row, track_id, notes);
  }
}

void
pastePatternBlockNotes(Scene & scene, const PatternBlock & block, int num_rows,
		       int target_row, int track_id, int target_note_offset) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    if (row_cells.empty()) continue;
    auto & cell = row_cells[0];
    for (size_t i = 0; i < cell.notes.size(); i++) {
      scene.setNote(row, track_id, target_note_offset + static_cast<int>(i), cell.notes[i]);
    }
  }
}

vector<Command>
copyPatternBlockCommand(const Scene & scene, int row_lo, int row_hi, int track_id) {
  vector<Command> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(scene.getCommand(row, track_id));
  return block;
}

void
clearPatternBlockCommand(Scene & scene, int row_lo, int row_hi, int track_id) {
  for (int row = row_lo; row <= row_hi; row++) scene.setCommand(row, track_id, Command());
}

void
pastePatternBlockCommand(Scene & scene, const vector<Command> & block, int num_rows,
			 int target_row, int track_id) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    scene.setCommand(row, track_id, block[row_offset]);
  }
}

vector<string>
copyPatternBlockAnnotations(const Scene & scene, int row_lo, int row_hi) {
  vector<string> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(scene.getAnnotation(row));
  return block;
}

void
clearPatternBlockAnnotations(Scene & scene, int row_lo, int row_hi) {
  for (int row = row_lo; row <= row_hi; row++) scene.setAnnotation(row, "");
}

void
pastePatternBlockAnnotations(Scene & scene, const vector<string> & block, int num_rows, int target_row) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    scene.setAnnotation(row, block[row_offset]);
  }
}
