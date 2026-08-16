#include "TestFramework.h"

#include "../src/ui/PatternScroll.h"
#include "../src/model/VisibleTrackInfo.h"

#include <optional>
#include <vector>

using namespace std;

namespace {

// Independent reimplementation of PatternEditor::renderRow()'s own
// horizontal layout arithmetic - deliberately not sharing any code with
// VisibleTrackInfo::getColumnWidth() or computeScrollPosition(), so a test
// failure here means the production math and this checker disagree, not
// that both happen to share the same bug. Mirrors renderRow() exactly:
// row-number gutter width 5, a per-track early exit once current_pos
// reaches cols, no leading separator on a track's first *rendered*
// column, and a trailing "|" after every track's last column.
int contentWidth(ColumnType type) {
  switch (type) {
  case ColumnType::NOTE: return 3;
  case ColumnType::VELOCITY: return 2;
  case ColumnType::DELAY: return 2;
  case ColumnType::EFFECT: return 4;
  default: return 0;
  }
}

struct Span { int start, end; };

// The [start, end) character range (track_index, col) would actually occupy
// on screen, or nullopt if renderRow() never even reaches it (the per-track
// early exit fires first).
optional<Span> renderedSpanFor(const GridPosition & scroll, int track_index, int col,
				const vector<int> & track_ids,
				const unordered_map<int, VisibleTrackInfo> & track_info) {
  int current_pos = 5;
  optional<Span> result;
  for (int i = scroll.track; i < static_cast<int>(track_ids.size()); i++) {
    auto it = track_info.find(track_ids[static_cast<size_t>(i)]);
    if (it == track_info.end()) continue;
    auto & info = it->second;
    int first_col = (i == scroll.track) ? scroll.col : 0;
    for (int k = first_col; k < info.getColumnCount(); k++) {
      if (k != first_col) current_pos += 1;
      int width = contentWidth(info.getColumnType(k));
      if (i == track_index && k == col) result = Span{current_pos, current_pos + width};
      current_pos += width;
    }
    current_pos += 1; // trailing "|"
  }
  return result;
}

// Independent reimplementation of PatternEditor::getEffectiveSelectionBounds()'s
// own no-mark fallback grouping (deliberately not calling
// VisibleTrackInfo::getNoteColumnRange, the production code this exists to
// check): every column sharing cursor_col's note number, since with no mark
// set that whole group - not just cursor_col alone - is what the cursor's
// always-on highlight actually covers. Just {cursor_col} on the effect
// column, which belongs to no note number.
vector<int> highlightedColumnsFor(const VisibleTrackInfo & info, int cursor_col) {
  vector<int> result;
  if (info.isEffectColumn(cursor_col)) {
    result.push_back(cursor_col);
    return result;
  }
  auto note = info.getNoteNumber(cursor_col);
  for (int k = 0; k < info.getColumnCount(); k++) {
    if (!info.isEffectColumn(k) && info.getNoteNumber(k) == note) result.push_back(k);
  }
  return result;
}

// A violation is any column of cursor_col's own highlighted group (see
// highlightedColumnsFor) that renderRow() either never reaches or clips
// past cols.
bool cursorHighlightFullyVisible(const GridPosition & scroll, int cursor_track, int cursor_col,
				  const vector<int> & track_ids,
				  const unordered_map<int, VisibleTrackInfo> & track_info,
				  int cols) {
  auto it = track_info.find(track_ids[static_cast<size_t>(cursor_track)]);
  if (it == track_info.end()) return false;
  for (int k : highlightedColumnsFor(it->second, cursor_col)) {
    auto span = renderedSpanFor(scroll, cursor_track, k, track_ids, track_info);
    if (!span || span->end > cols) return false;
  }
  return true;
}

VisibleTrackInfo makeTrackInfo(int num_subtracks, bool note, int velocity_columns, bool delay, bool effect) {
  VisibleTrackInfo info;
  info.num_subtracks_ = num_subtracks;
  info.has_note_column_ = note;
  info.num_velocity_columns_ = velocity_columns;
  info.has_delay_column_ = delay;
  info.has_effect_column_ = effect;
  return info;
}

} // namespace

// Sweeps every (cols, cursor_track, cursor_col) combination against a mix
// of ordinary and deliberately oversized tracks (wider alone than any cols
// value tried here) and checks the invariant computeScrollPosition() exists
// to guarantee: wherever the cursor lands, renderRow() actually draws its
// *whole* highlighted group (see cursorHighlightFullyVisible) on screen,
// never partially clipped - not just the raw column the cursor happens to
// be on.
TEST(cursor_highlight_always_fully_on_screen_across_track_and_col_combinations) {
  vector<int> track_ids = { 10, 20, 30 };
  unordered_map<int, VisibleTrackInfo> track_info = {
    { 10, makeTrackInfo(2, true, 1, false, true) },   // modest: 2 subtracks, note+velocity+effect
    { 20, makeTrackInfo(20, true, 0, false, false) }, // oversized: 20 note columns alone
    { 30, makeTrackInfo(1, true, 1, true, true) },    // modest: note+velocity+delay+effect
  };

  int violations = 0;
  for (int cols : { 20, 40, 60, 80, 100, 120 }) {
    GridPosition current_scroll;
    for (int cursor_track = 0; cursor_track < static_cast<int>(track_ids.size()); cursor_track++) {
      auto & info = track_info.at(track_ids[static_cast<size_t>(cursor_track)]);
      for (int cursor_col = 0; cursor_col < info.getColumnCount(); cursor_col++) {
	auto new_scroll = computeScrollPosition(current_scroll, 0, cursor_track, cursor_col, track_ids, track_info, cols);
	if (!cursorHighlightFullyVisible(new_scroll, cursor_track, cursor_col, track_ids, track_info, cols)) {
	  violations++;
	} else {
	  // Only advance current_scroll (mirroring PatternEditor persisting
	  // current_scroll_ frame to frame) when the cursor was actually
	  // found fully on screen - matches render()'s own "scroll follows
	  // the cursor" sequencing.
	  current_scroll = new_scroll;
	}
      }
    }
  }
  CHECK(violations == 0);
}

// The single scenario the original bug report was about: a lone track (no
// others before it) wider than any reasonable screen, cursor sweeping
// across every one of its columns from left to right and back.
TEST(cursor_highlight_always_fully_on_screen_within_a_single_oversized_track) {
  vector<int> track_ids = { 1 };
  unordered_map<int, VisibleTrackInfo> track_info = {
    { 1, makeTrackInfo(10, true, 1, true, false) }, // 10 subtracks x (note+velocity+delay) = 100 columns wide
  };
  auto & info = track_info.at(1);
  int cols = 80;

  GridPosition current_scroll;
  for (int cursor_col = 0; cursor_col < info.getColumnCount(); cursor_col++) {
    current_scroll = computeScrollPosition(current_scroll, 0, 0, cursor_col, track_ids, track_info, cols);
    CHECK(cursorHighlightFullyVisible(current_scroll, 0, cursor_col, track_ids, track_info, cols));
  }
  for (int cursor_col = info.getColumnCount() - 1; cursor_col >= 0; cursor_col--) {
    current_scroll = computeScrollPosition(current_scroll, 0, 0, cursor_col, track_ids, track_info, cols);
    CHECK(cursorHighlightFullyVisible(current_scroll, 0, cursor_col, track_ids, track_info, cols));
  }
}

// The exact bug report this fix addresses: cursor on the NOTE subcolumn of
// a note/velocity/delay group near the right edge of an oversized track -
// the note itself fits, but its sibling delay column (part of the same
// always-on highlight, per getEffectiveSelectionBounds()'s no-mark
// fallback) doesn't, unless computeScrollPosition() scrolls far enough to
// fit the *whole* group, not just the column the cursor happens to sit on.
TEST(delay_column_stays_visible_when_cursor_is_on_the_sibling_note_column) {
  vector<int> track_ids = { 1 };
  unordered_map<int, VisibleTrackInfo> track_info = {
    { 1, makeTrackInfo(15, true, 1, true, false) }, // 15 subtracks x (note+velocity+delay) = 150 columns wide
  };
  auto & info = track_info.at(1);

  // Column index of the NOTE subcolumn for the last (rightmost) subtrack -
  // its own velocity/delay siblings follow immediately after it.
  int last_note_col = (info.num_subtracks_ - 1) * 3;
  CHECK(info.isNoteColumn(last_note_col));

  // Swept across a range rather than one fixed value: whether a given cols
  // value happens to expose the bug depends on where it falls relative to
  // the group's own width, so a single arbitrarily-picked value could
  // (and, picked wrong, silently did during development) land on a point
  // that never violated even with the fix reverted.
  for (int cols = 20; cols <= 150; cols++) {
    auto scroll = computeScrollPosition(GridPosition(), 0, 0, last_note_col, track_ids, track_info, cols);
    CHECK(cursorHighlightFullyVisible(scroll, 0, last_note_col, track_ids, track_info, cols));
  }
}

// Minimality, not just eventual correctness: computeScrollPosition()'s own
// doc comment describes moving the window by no more than necessary to
// keep the cursor on screen, but the tests above only ever check the
// *result* is fully visible - none of them would catch the window jumping
// further than required along the way. This walks the cursor one column
// at a time (the same granularity Left/Right ever moves it) across every
// column of a mix of ordinary and oversized tracks, feeding each step's
// own resulting scroll back in as the next step's current_scroll (matching
// how PatternEditor::render() persists current_scroll_ frame to frame),
// and fails the moment a step moves the window while the cursor's own
// highlighted group was already fully visible in the *previous* window -
// i.e. moving would have been unnecessary.
TEST(scroll_never_moves_while_the_cursor_is_already_visible) {
  vector<int> track_ids = { 10, 20, 30 };
  unordered_map<int, VisibleTrackInfo> track_info = {
    { 10, makeTrackInfo(2, true, 1, false, true) },   // modest: 2 subtracks, note+velocity+effect
    { 20, makeTrackInfo(20, true, 0, false, false) }, // oversized: 20 note columns alone
    { 30, makeTrackInfo(1, true, 1, true, true) },    // modest: note+velocity+delay+effect
  };

  // Every (track, col) position in left-to-right order, walked forward then
  // back - mirrors a user holding Right to the far end and then holding
  // Left all the way back, one column at a time.
  vector<pair<int, int>> path;
  for (int t = 0; t < static_cast<int>(track_ids.size()); t++) {
    auto & info = track_info.at(track_ids[static_cast<size_t>(t)]);
    for (int k = 0; k < info.getColumnCount(); k++) path.push_back({t, k});
  }
  path.insert(path.end(), path.rbegin(), path.rend());

  for (int cols : { 20, 40, 60, 80, 100, 120 }) {
    GridPosition current_scroll;
    int violations = 0;
    for (auto & [t, k] : path) {
      bool was_already_visible = cursorHighlightFullyVisible(current_scroll, t, k, track_ids, track_info, cols);
      auto new_scroll = computeScrollPosition(current_scroll, 0, t, k, track_ids, track_info, cols);
      if (was_already_visible && new_scroll != current_scroll) violations++;
      current_scroll = new_scroll;
    }
    CHECK(violations == 0);
  }
}

// A SampleTrack/DrumMachineTrack ahead of the cursor's own track used to
// get no VisibleTrackInfo entry at all (PatternEditor.cpp's
// fill_track_info() never matched TrackType::SAMPLE), which
// computeScrollPosition() and renderRow() both silently treat as a
// zero-width track rather than its real ~4-character placeholder width -
// undercounting how much screen space it actually consumes and letting a
// too-wide scroll position through, clipping a few characters off the
// cursor's real column. Regression test for that specific composition,
// with the placeholder entry present (as fill_track_info() now always
// gives SAMPLE/DRUM_MACHINE) at exactly the cols value where the
// undercount used to matter.
TEST(cursor_column_fully_visible_with_a_placeholder_track_ahead_of_it) {
  vector<int> track_ids = { 5, 6 };
  unordered_map<int, VisibleTrackInfo> track_info = {
    { 5, VisibleTrackInfo() }, // SAMPLE/DRUM_MACHINE placeholder: 1 note column, real width 4
    { 6, makeTrackInfo(20, true, 0, false, false) }, // 20 note columns wide
  };
  auto & cursor_track_info = track_info.at(6);

  for (int cols = 90; cols <= 100; cols++) {
    GridPosition current_scroll;
    for (int cursor_col = 0; cursor_col < cursor_track_info.getColumnCount(); cursor_col++) {
      current_scroll = computeScrollPosition(current_scroll, 0, 1, cursor_col, track_ids, track_info, cols);
      CHECK(cursorHighlightFullyVisible(current_scroll, 1, cursor_col, track_ids, track_info, cols));
    }
  }
}
