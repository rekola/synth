#ifndef _PATTERNSCROLL_H_
#define _PATTERNSCROLL_H_

#include "GridPosition.h"

#include <unordered_map>
#include <vector>

class VisibleTrackInfo;

// Computes the pattern grid's new scroll position for PatternEditor::render(),
// pulled out into a pure function (no UIElement/notcurses dependency) so it
// can be exhaustively unit-tested (see tests/PatternScrollTests.cpp) rather
// than only exercised by eye through a real terminal.
//
// .row is `new_row` verbatim (the caller already resolved this from the
// playhead - see PatternEditor::render()'s own comment on why that's a
// separate concern from track/column scrolling).
//
// .track is current_scroll.track, adjusted just enough to keep cursor_track
// on screen: snapped left immediately if the cursor is left of the current
// window, otherwise grown right one track at a time until the run of full
// track widths from .track through cursor_track fits within cols - except
// it never grows past cursor_track itself, since a single track wider than
// the screen can never fit no matter how far right the window scrolls
// (letting it grow past would push cursor_track out of the resulting
// window entirely, which snaps right back to it the very next call - an
// oscillation that never settles).
//
// .col is 0 unless .track == cursor_track and cursor_track's own full width
// still doesn't fit even alone (the case the paragraph above defers here) -
// preferring to show the *whole* cursor track (every note/velocity/delay/
// effect column, not just cursor_col's own group), so its own heading
// (name, Mute/Solo - see renderHeading()'s use of this same scroll
// position) stays visible whenever there's room for it. Only when even
// that doesn't fit does it fall back to the narrower target this function
// has always guaranteed: the smallest column index, converged the same
// bounded way, such that the run of column widths from .col through the
// *end* of cursor_col's own note/velocity/delay group (VisibleTrackInfo::
// getNoteColumnRange - a single column only for the effect column, which
// belongs to no such group) fits within cols - i.e. which of cursor_track's
// own columns is the first one drawn, so the cursor's own always-on
// highlight (which covers that whole group, not just cursor_col - see
// getNoteColumnRange's own comment) is never partly scrolled out of view,
// even when the rest of its track can't all fit alongside it. Every other
// track always renders from its own column 0.
//
// track_info must already reflect new_row (its per-track column counts are
// scoped to whatever rows are currently visible - see
// PatternEditor::getTrackInformation's own doc comment).
GridPosition computeScrollPosition(const GridPosition & current_scroll, int new_row,
				    int cursor_track, int cursor_col,
				    const std::vector<int> & track_ids,
				    const std::unordered_map<int, VisibleTrackInfo> & track_info,
				    int cols);

#endif
