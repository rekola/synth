#ifndef _VISIBLETRACKINFO_H_
#define _VISIBLETRACKINFO_H_

#include <utility>

enum class ColumnType {
  UNKNOWN = 0,
  NOTE,
  VELOCITY,
  DELAY,
  EFFECT
};

class VisibleTrackInfo {
public:
  VisibleTrackInfo() { }

  int getColumnCount() const { return num_subtracks_ * ((has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0)) + (has_effect_column_ ? 1 : 0); }
  // The track's true total on-screen footprint, *including* its own
  // trailing "|" border character (see getColumnWidth()'s own comment for
  // why summing getColumnWidth() over every column gets this exactly
  // right, not by one character either way).
  int getTrackWidth() const {
    int w = 0;
    for (int k = 0; k < getColumnCount(); k++) w += getColumnWidth(k);
    return w;
  }
  // Column k's own true content width (3/2/2/4 - matches renderRow()'s own
  // `current_pos +=` increments exactly) plus a flat +1. That +1 is *not*
  // a per-column separator (a column doesn't know whether it's about to be
  // the first one actually drawn in some sub-range, which is the only
  // thing that ever determines whether a real separator precedes it) - it's
  // this column's own share of the track's total border/separator budget:
  // N columns need N-1 real inter-column separators plus 1 trailing "|",
  // i.e. N extra characters total, and N columns each contributing +1 adds
  // up to exactly that. This only balances out over a **complete** track
  // (0 through getColumnCount() - 1) - a caller summing a *sub-range* that
  // doesn't reach the track's real last column (PatternScroll.cpp's
  // trackWidthRange(), used for a column-scrolled anchor track or a
  // deliberately truncated terminal track) needs to give back one of those
  // reserved characters, since no trailing border is actually drawn there.
  int getColumnWidth(int k) const {
    switch (getColumnType(k)) {
    case ColumnType::NOTE: return 4;
    case ColumnType::VELOCITY: return 3;
    case ColumnType::DELAY: return 3;
    case ColumnType::EFFECT: return 5;
    default: return 0;
    }
  }
  ColumnType getColumnType(int k) const {
    auto column_count = getColumnCount();
    if (has_effect_column_ && k == column_count - 1) {
      return ColumnType::EFFECT;
    } else {
      auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
      k = k % n;

      if (has_note_column_) {
	if (k == 0) return ColumnType::NOTE;
	else k--;
      }

      if (k < num_velocity_columns_) return ColumnType::VELOCITY;
      else k -= num_velocity_columns_;

      if (has_delay_column_) {
	if (k == 0) return ColumnType::DELAY;
	else k--;      
      }
      
      return ColumnType::UNKNOWN;
    }
  }
  bool isNoteColumn(int k) const { return getColumnType(k) == ColumnType::NOTE; }
  bool isVelocityColumn(int k) const { return getColumnType(k) == ColumnType::VELOCITY; }
  bool isDelayColumn(int k) const { return getColumnType(k) == ColumnType::DELAY; }
  bool isEffectColumn(int k) const { return getColumnType(k) == ColumnType::EFFECT; }
  
  int getNoteNumber(int k) const {
    auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
    return k / n;
  }

  // The [lo, hi] (inclusive) column range sharing k's own note number - its
  // note column, velocity column(s), and delay column, if present. This is
  // the same grouping PatternEditor's getEffectiveSelectionBounds() falls
  // back to with no mark set (one note's worth of columns, not just k
  // alone), and hence what the cursor's own always-on highlight actually
  // covers - see PatternScroll.cpp, which keeps the whole range on screen
  // rather than just k. {k, k} for the effect column, which belongs to no
  // note number (isEffectColumn() callers already special-case it the same
  // way before trusting getNoteNumber()).
  std::pair<int, int> getNoteColumnRange(int k) const {
    if (isEffectColumn(k)) return { k, k };
    auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
    if (n <= 0) return { k, k };
    auto note = getNoteNumber(k);
    return { note * n, note * n + n - 1 };
  }

  void updateNumSubtracks(int n) {  
    if (n > num_subtracks_) num_subtracks_ = n;
  }
  
  int num_subtracks_ = 1;
  int num_velocity_columns_ = 0;
  bool has_note_column_ = true;
  bool has_delay_column_ = false;
  bool has_effect_column_ = false;
};

#endif
