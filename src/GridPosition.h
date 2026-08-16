#ifndef _GRIDPOSITION_H_
#define _GRIDPOSITION_H_

#include "SelectionScope.h"

// A row/track/column location in the pattern grid - the shape shared by
// every single-cell position: where the grid is scrolled to
// (PatternEditor's own current_scroll_), the selection mark, and the
// cursor itself (PatternEditor's current_cursor/new_cursor).
//
// subcol is meaningful only for the cursor: which hex digit within a
// VELOCITY/DELAY/EFFECT column's own multi-character value a C-+/C--style
// edit is about to touch (see PatternEditor::offerInput()'s EFFECT/
// VELOCITY/DELAY branches). It's really internal state of that in-place
// nibble editor, not part of "a grid location" the way row/track/col are -
// left here anyway rather than split into a third class, since nothing
// else needs a track/col pair without it and every non-cursor use (scroll
// position, the selection mark) simply leaves it at its default.
//
// scope is the same story, cursor-only, reusing SelectionScope.h's own
// vocabulary rather than a one-off boolean: it says which parameters of
// *this* position actually describe where the cursor is. Today the only
// value the cursor itself ever sets is ANNOTATION - ANNOTATION means the
// cursor has moved past every real track onto the current row's
// annotation "slot" (PatternEditor::startAnnotationEdit(), reached via
// Right arrow past the last track's last column), and track/col are
// meaningless there - not "point past track_ids" meaningless, but really
// left exactly where they already were (that same last column), since
// every piece of code that indexes track_ids by the cursor's own track
// (selection bounds, scroll, note entry, ...) needs to keep working
// unchanged and stay unaware this state exists at all. Anything else
// (the default, NOTE_COLUMN) means track/col *are* the real position -
// isHighlighted()/PatternEditor::getEffectiveSelectionBounds() are the
// two places that branch on this. TRACK (a range outcome, never a single
// point) and COMMAND (still resolved externally, via VisibleTrackInfo::
// getColumnType(col) - a GridPosition alone has no track layout to
// classify its own column with) are never actually stored here yet; the
// enum is shared so a later phase that needs finer cursor classification
// isn't inventing a second, parallel vocabulary next to SelectionBounds's.
// The eventual plan is hierarchical cursor navigation through this same
// field: TRACK to pick a whole track, in to NOTE_COLUMN to pick one voice
// slot within it, in again to pick a specific sub-column (velocity,
// delay, ...) within that slot - not just the single ANNOTATION case this
// started as.
class GridPosition {
 public:
  GridPosition() { }

  bool isHighlighted(int _track, int _col) const {
    return scope != SelectionScope::ANNOTATION && _track == track && _col == col;
  }
  bool isOnAnnotation() const { return scope == SelectionScope::ANNOTATION; }

  bool operator==(const GridPosition & other) const {
    return row == other.row && track == other.track && col == other.col && subcol == other.subcol &&
      scope == other.scope;
  }
  bool operator!=(const GridPosition & other) const { return !(*this == other); }

  int row = 0, track = 0, col = 0, subcol = 0;
  SelectionScope scope = SelectionScope::NOTE_COLUMN;
};

#endif
