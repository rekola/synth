#ifndef _SELECTIONBOUNDS_H_
#define _SELECTIONBOUNDS_H_

#include "SelectionScope.h"

// Resolved row/track/note-column bounds for a selection-consuming command
// (kill-region, transpose-region-*, ...) - see
// PatternEditor::getEffectiveSelectionBounds(). When no mark is active (or
// the mark is on a different pattern), this degenerates to the single note
// the cursor is currently on - there's always a region to act on, never
// "nothing selected". note_lo/note_hi are meaningful only when
// scope == NOTE_COLUMN (track_lo == track_hi always holds there too).
struct SelectionBounds {
  // Default-initialized (not left indeterminate) so a freshly-constructed
  // SelectionBounds - e.g. PatternEditor::current_sel_bounds_ before its
  // first real assignment - compares equal to another default instance
  // rather than reading garbage on the first operator== check.
  int row_lo = 0, row_hi = 0, track_lo = 0, track_hi = 0;
  SelectionScope scope = SelectionScope::TRACK;
  int note_lo = 0, note_hi = 0;

  // Two bounds are the same selection iff every field matches - used to
  // detect a changed effective selection (mark moved, point moved, scope
  // flipped, ...) as a single comparison instead of separately diffing
  // each of the several pieces of state that feed getEffectiveSelectionBounds()
  // (see PatternEditor::render()).
  bool operator==(const SelectionBounds & other) const {
    return row_lo == other.row_lo && row_hi == other.row_hi &&
      track_lo == other.track_lo && track_hi == other.track_hi &&
      scope == other.scope && note_lo == other.note_lo && note_hi == other.note_hi;
  }
  bool operator!=(const SelectionBounds & other) const { return !(*this == other); }
};

#endif
