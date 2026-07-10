#ifndef _PATTERNBLOCKOPS_H_
#define _PATTERNBLOCKOPS_H_

#include "Note.h"
#include "Command.h"

#include <vector>

class Pattern;

struct PatternBlockCell {
  std::vector<Note> notes;
  Command command;
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
// clipping any cells whose target row or track falls outside pattern/track_ids bounds.
void pastePatternBlock(Pattern & pattern, const PatternBlock & block,
		       int target_row, const std::vector<int> & track_ids, int target_track);

// Transposes (up if `up`, else down) every note in the same range.
void transposePatternBlock(Pattern & pattern, int row_lo, int row_hi,
			   const std::vector<int> & track_ids, int track_lo, int track_hi, bool up);

#endif
