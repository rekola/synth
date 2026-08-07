#ifndef _PATTERNBLOCKOPS_H_
#define _PATTERNBLOCKOPS_H_

#include "Note.h"
#include "Command.h"

#include <functional>
#include <vector>

class Pattern;

struct PatternBlockCell {
  std::vector<Note> notes;
  Command command;
  int note_offset = 0; // for note-column-scoped cells: which note column `notes[0]` is
};

// [row_offset][track_offset], row-major within the copied rectangle.
using PatternBlock = std::vector<std::vector<PatternBlockCell> >;

// Captures notes and effect command for each (row, track) in the inclusive
// range [row_lo, row_hi] x track_ids[track_lo..track_hi].
PatternBlock copyPatternBlock(const Pattern & pattern, int row_lo, int row_hi,
			     const std::vector<int> & track_ids, int track_lo, int track_hi);

// Clears notes and effect command for the same range.
void clearPatternBlock(Pattern & pattern, int row_lo, int row_hi,
		       const std::vector<int> & track_ids, int track_lo, int track_hi);

// Writes `block` into `pattern` starting at (target_row, track_ids[target_track]),
// clipping any cells whose target row or track falls outside [0, num_rows)/
// track_ids bounds.
void pastePatternBlock(Pattern & pattern, const PatternBlock & block, int num_rows,
		       int target_row, const std::vector<int> & track_ids, int target_track);

// Transposes (up if `up`, else down) every note in the same range, except
// any track `is_percussion` reports true for. A percussion track's
// Note::getValue() selects which drum sound plays (a MIDI key), not a
// pitch - transposing it would silently swap to a different, unrelated
// drum instead of "transposing" anything, so those tracks are skipped
// entirely within the range rather than shifting their notes.
void transposePatternBlock(Pattern & pattern, int row_lo, int row_hi,
			   const std::vector<int> & track_ids, int track_lo, int track_hi, bool up,
			   const std::function<bool(int track_id)> & is_percussion);

// Single-track, note-column-scoped siblings of the above: operate on just
// notes [note_lo, note_hi] of one track, leaving other note columns
// untouched. `include_command` additionally captures/clears/restores the
// track's effect Command for the row range - used when the cursor is on
// the effect column, where the note range has been widened to the whole
// track but the effect command (which applies to every note column) needs
// to travel with the operation too.
PatternBlock copyPatternBlockNotes(const Pattern & pattern, int row_lo, int row_hi,
				   int track_id, int note_lo, int note_hi, bool include_command = false);
void clearPatternBlockNotes(Pattern & pattern, int row_lo, int row_hi,
			    int track_id, int note_lo, int note_hi, bool include_command = false);
// `is_percussion`: same reasoning as transposePatternBlock() above, but a
// plain bool here (not a predicate) since this operates on exactly one
// already-known track_id, not a range.
void transposePatternBlockNotes(Pattern & pattern, int row_lo, int row_hi,
				int track_id, int note_lo, int note_hi, bool up, bool is_percussion);
// Merges `block` into `pattern` starting at (target_row, track_id, target_note_offset),
// leaving note columns outside that range untouched (unlike pastePatternBlock,
// which replaces a cell's whole note vector).
void pastePatternBlockNotes(Pattern & pattern, const PatternBlock & block, int num_rows,
			    int target_row, int track_id, int target_note_offset, bool include_command = false);

#endif
