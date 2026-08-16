#include "PatternScroll.h"
#include "../model/VisibleTrackInfo.h"

using namespace std;

namespace {

// A track's own on-screen contribution summing columns [from_col, to_col]
// (inclusive, clipped to the track's real column count).
// VisibleTrackInfo::getColumnWidth()'s own "+1 per column" (see its own
// doc comment) sums exactly to a track's true footprint - content, every
// real inter-column separator, and its own trailing "|" - matching what's
// actually needed whenever something *else* still needs to be positioned
// after this track (that trailing "|" is also the gap the next track's
// own content starts right after). is_last_term is true for whichever
// term is the last one contributing to a particular pos sum below - nothing
// else follows it there, so its own trailing border is decorative only:
// the real renderer draws it (or tries to; harmlessly clipped if it
// doesn't fit) *after* already having drawn everything that actually
// needs to be visible, so it never has to be budgeted for. This is not
// the same thing as "to_col reaches the track's real last column" - a
// track whose highlighted group happens to end exactly on its own last
// column (e.g. any single-column track) still doesn't need its border
// budgeted if it's the last term, and a track that reaches its own last
// column but *isn't* the last term (an earlier, fully-shown track ahead
// of the cursor) still does.
int trackWidthRange(const VisibleTrackInfo & info, int from_col, int to_col, bool is_last_term) {
  int w = 0;
  for (int k = from_col; k <= to_col && k < info.getColumnCount(); k++) w += info.getColumnWidth(k);
  if (w > 0 && is_last_term) w -= 1;
  return w;
}

// Minimum characters of headroom to guarantee past the last track when
// the cursor is parked on the annotation slot - "the last track is fully
// visible" alone (see computeScrollPosition()'s own comment) can still
// leave zero width for the annotation itself if the tracks happen to
// fill the screen right up to the edge, which is the whole reason
// reaching the slot didn't reliably bring it into view. Sized to fit
// PatternEditor.cpp's own "(add annotation)" placeholder (17 characters)
// plus the 2-character leading gap renderRow() always puts before
// annotation content - not a promise about arbitrary (unbounded) real
// annotation text, which can always run off the right edge regardless.
constexpr int kAnnotationMinWidth = 20;

} // namespace

GridPosition
computeScrollPosition(const GridPosition & current_scroll, int new_row,
		      int cursor_track, int cursor_col,
		      const vector<int> & track_ids,
		      const unordered_map<int, VisibleTrackInfo> & track_info,
		      int cols) {
  GridPosition new_scroll;
  new_scroll.row = new_row;

  if (track_ids.empty() || cursor_track < 0 || cursor_track > static_cast<int>(track_ids.size())) {
    new_scroll.track = 0;
    new_scroll.col = 0;
    return new_scroll;
  }

  // One past every real track - the cursor is parked on the current row's
  // annotation slot (PatternEditor::render() passes track_ids.size() as
  // the target for this, since the cursor's own track/col stay on the
  // last real column the whole time it's parked there - see
  // GridPosition::scope's own comment). Substitute the last track's own
  // last column as the concrete target - reusing the exact same growth
  // loop below - and separately reserve kAnnotationMinWidth beyond it
  // (below), since "the last track is fully visible" alone doesn't
  // reserve *any* width for the annotation itself.
  bool is_annotation_target = cursor_track == static_cast<int>(track_ids.size());
  if (is_annotation_target) {
    cursor_track -= 1;
    auto it = track_info.find(track_ids[static_cast<size_t>(cursor_track)]);
    cursor_col = it != track_info.end() ? it->second.getColumnCount() - 1 : 0;
  }

  // The cursor's own always-on highlight isn't just column cursor_col -
  // with no mark set, getEffectiveSelectionBounds() falls back to the
  // whole note/velocity/delay group cursor_col belongs to (VisibleTrackInfo::
  // getNoteColumnRange - {cursor_col, cursor_col} on the effect column,
  // which belongs to no such group), so that's what has to stay on
  // screen, never split, no matter which track ends up anchoring the
  // window.
  auto cursor_it = track_info.find(track_ids[static_cast<size_t>(cursor_track)]);
  auto [group_lo, group_hi] = cursor_it != track_info.end() ?
    cursor_it->second.getNoteColumnRange(cursor_col) : pair<int, int>{cursor_col, cursor_col};

  // Starting point. cursor_track left of the current window can't be
  // reached by the rightward-only growth below, so it's a direct reset
  // (matching the old track-level "snap left" case) rather than something
  // the loop could ever walk back to on its own. Otherwise keep today's
  // anchor/column as the starting point to grow from - including,
  // symmetrically, snapping the column back left when the cursor's own
  // group has moved left of it (the loop below only ever grows right).
  if (cursor_track < current_scroll.track) {
    new_scroll.track = cursor_track;
    new_scroll.col = 0;
  } else {
    new_scroll.track = current_scroll.track;
    new_scroll.col = current_scroll.col;
    if (new_scroll.track == cursor_track && group_lo < new_scroll.col) new_scroll.col = group_lo;
  }

  // Grows the window's left edge rightward - one column at a time, never
  // a whole track at once - until the run from (new_scroll.track,
  // new_scroll.col) through cursor_track's own highlighted group fits.
  // Trimming the anchor track's own front one column at a time first
  // (rather than only ever dropping it outright, which is what a
  // track-at-a-time-only version of this loop used to do) matters for
  // *any* track standing between the old anchor and the cursor, not just
  // the cursor's own - dropping a whole track when trimming a couple of
  // its own leading columns would have been enough overshoots by however
  // wide that track is, which is exactly the "always a maximal jump"
  // symptom this replaced.
  while (true) {
    auto anchor_it = track_info.find(track_ids[static_cast<size_t>(new_scroll.track)]);
    if (anchor_it == track_info.end()) break;

    // pos is an exclusive upper bound (the position just past the last
    // character drawn), so pos == cols is an exact fit, not an overflow.
    int pos = 5;
    for (int i = new_scroll.track; i <= cursor_track; i++) {
      auto it = track_info.find(track_ids[static_cast<size_t>(i)]);
      if (it == track_info.end()) continue;
      auto from_col = (i == new_scroll.track) ? new_scroll.col : 0;
      auto to_col = (i == cursor_track) ? group_hi : it->second.getColumnCount() - 1;
      pos += trackWidthRange(it->second, from_col, to_col, i == cursor_track);
    }
    if (is_annotation_target) pos += kAnnotationMinWidth;
    if (pos <= cols) break;

    if (new_scroll.track == cursor_track) {
      // No further whole track to drop - this is the cursor's own; the
      // only room left to give is trimming further into its own front,
      // never past the start of its own highlighted group.
      if (new_scroll.col < group_lo) new_scroll.col++;
      else break; // the group itself is wider than cols - nothing more to give
    } else if (new_scroll.col < anchor_it->second.getColumnCount() - 1) {
      new_scroll.col++;
    } else {
      // Fully trimmed to nothing - drop it and move on to the next track.
      new_scroll.track++;
      new_scroll.col = 0;
    }
  }

  return new_scroll;
}
