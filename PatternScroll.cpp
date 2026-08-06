#include "PatternScroll.h"
#include "VisibleTrackInfo.h"

using namespace std;

GridPosition
computeScrollPosition(const GridPosition & current_scroll, int new_row,
		      int cursor_track, int cursor_col,
		      const vector<int> & track_ids,
		      const unordered_map<int, VisibleTrackInfo> & track_info,
		      int cols) {
  GridPosition new_scroll;
  new_scroll.row = new_row;

  new_scroll.track = current_scroll.track;
  if (cursor_track < new_scroll.track) {
    new_scroll.track = cursor_track;
  } else {
    while (1) {
      auto pos = 6;
      for (auto i = new_scroll.track; i < static_cast<int>(track_ids.size()) && i <= cursor_track; i++) {
	auto id = track_ids[static_cast<size_t>(i)];
	auto it = track_info.find(id);
	pos += (it != track_info.end() ? it->second.getTrackWidth() : 0);
      }
      if (pos >= cols && new_scroll.track < cursor_track) {
	new_scroll.track++;
      } else {
	break;
      }
    }
  }

  new_scroll.col = 0;
  if (new_scroll.track == cursor_track && cursor_track >= 0 && cursor_track < static_cast<int>(track_ids.size())) {
    auto it = track_info.find(track_ids[static_cast<size_t>(cursor_track)]);
    if (it != track_info.end()) {
      auto & cursor_track_info = it->second;
      // The cursor's own always-on highlight isn't just column cursor_col -
      // with no mark set, getEffectiveSelectionBounds() falls back to the
      // whole note/velocity/delay group cursor_col belongs to (see
      // VisibleTrackInfo::getNoteColumnRange's own comment), so that's what
      // has to stay on screen, not just cursor_col alone. {cursor_col,
      // cursor_col} on the effect column, which belongs to no such group -
      // reduces to the original single-column behavior there.
      auto [group_lo, group_hi] = cursor_track_info.getNoteColumnRange(cursor_col);
      while (new_scroll.col < group_lo) {
	auto pos = 6;
	for (auto k = new_scroll.col; k <= group_hi; k++) pos += cursor_track_info.getColumnWidth(k);
	if (pos >= cols) new_scroll.col++;
	else break;
      }
    }
  }

  return new_scroll;
}
