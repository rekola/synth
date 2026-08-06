#ifndef _GRIDPOSITION_H_
#define _GRIDPOSITION_H_

// A row/track/column triple - the shape shared by any single location in
// the pattern grid: where the grid is scrolled to (PatternEditor's own
// use, see render()), the selection mark, or (with Cursor.h's own extra
// subcol) the cursor itself.
class GridPosition {
 public:
  GridPosition() { }

  bool operator==(const GridPosition & other) const {
    return row == other.row && track == other.track && col == other.col;
  }
  bool operator!=(const GridPosition & other) const { return !(*this == other); }

  int row = 0, track = 0, col = 0;
};

#endif
