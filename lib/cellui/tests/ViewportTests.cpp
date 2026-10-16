#include <cellui/Viewport.h>

#include "TestFramework.h"

using namespace cellui;

TEST(scroll_axis_keeps_its_offset_inside_the_content) {
  ScrollAxis axis;
  axis.setExtent(20, 5);
  axis.scrollTo(100);
  CHECK(axis.offset() == 15 && axis.atEnd());
  axis.scrollBy(-100);
  CHECK(axis.offset() == 0 && !axis.atEnd());
  axis.scrollTo(10);
  // The content shrinks: the offset follows it in.
  axis.setExtent(12, 5);
  CHECK(axis.offset() == 7);
  // A content that fits does not scroll at all.
  axis.setExtent(3, 5);
  CHECK(axis.offset() == 0 && axis.maxOffset() == 0);
}

TEST(scroll_axis_reveal_moves_only_as_far_as_needed) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.reveal(5);
  CHECK(axis.offset() == 0);
  axis.reveal(12);
  CHECK(axis.offset() == 3 && axis.shows(12) && !axis.shows(2));
  axis.reveal(4);
  CHECK(axis.offset() == 3);
  axis.reveal(1);
  CHECK(axis.offset() == 1);
}

TEST(scroll_axis_reveal_keeps_a_margin_from_the_edges) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.scrollTo(20);
  axis.reveal(28, 3);  // 3 cells below it must show
  CHECK(axis.offset() == 22);
  axis.reveal(24, 3);  // above the margin: 3 cells above it
  CHECK(axis.offset() == 21);
  // The ends of the content stop the slide.
  axis.reveal(0, 3);
  CHECK(axis.offset() == 0);
  axis.reveal(99, 3);
  CHECK(axis.offset() == 90);
  // A margin the view cannot hold is cut down to what fits.
  axis.setExtent(100, 5);
  axis.reveal(50, 10);
  CHECK(axis.shows(50) && axis.offset() == 48);
}

TEST(scroll_axis_reveal_can_keep_room_for_something_drawn_over_the_end) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  // Three rows at the bottom are covered: the cursor stays above them.
  axis.reveal(9, 0, 3);
  CHECK(axis.offset() == 3);
  axis.reveal(5, 0, 3);
  CHECK(axis.offset() == 3);
  axis.reveal(2, 0, 3);
  CHECK(axis.offset() == 2);
  // A view too small for the margins keeps the index at its top.
  axis.setExtent(100, 2);
  axis.reveal(40, 0, 3);
  CHECK(axis.shows(40) && axis.offset() == 40);
}

TEST(viewport_wheel_scrolls_the_view_and_detaches_it) {
  Viewport view;
  view.setExtent(50, 30, 10, 10);
  CHECK(!view.detached());
  view.wheel(1, false);
  view.wheel(1, false);
  view.wheel(1, true, 4);
  CHECK(view.rows.offset() == 2 && view.cols.offset() == 4 && view.detached());
  view.wheel(-1, false);
  CHECK(view.rows.offset() == 1);
  view.attach();
  CHECK(!view.detached());
  // Not past the ends.
  for (int i = 0; i < 100; i++) view.wheel(1, false);
  CHECK(view.rows.offset() == 40);
}

TEST(scroll_axis_animates_toward_its_offset_when_smooth) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.setSmooth(true);
  axis.scrollTo(10);
  // The offset is where the view is going, shown() where it is.
  CHECK(axis.offset() == 10 && axis.shown() == 0.0 && axis.animating());
  axis.tick(ScrollAxis::kSettleSeconds);
  CHECK(axis.shown() > 5.0 && axis.shown() < 7.0);
  CHECK(axis.firstShown() == static_cast<int>(axis.shown()) && axis.fraction() > 0.0 && axis.fraction() < 1.0);
  for (int i = 0; i < 100 && axis.animating(); i++) axis.tick(0.016);
  CHECK(!axis.animating() && axis.shown() == 10.0 && axis.fraction() == 0.0);
  // Back the other way, passing through the fractions.
  axis.scrollTo(0);
  axis.tick(0.016);
  CHECK(axis.shown() < 10.0 && axis.shown() > 0.0);
}

TEST(scroll_axis_index_at_is_what_is_shown_under_a_cell) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.scrollTo(7);
  CHECK(axis.indexAt(0) == 7 && axis.indexAt(3) == 10);
  axis.setSmooth(true);
  axis.scrollTo(8);
  axis.tick(ScrollAxis::kSettleSeconds);
  // Between 7 and 8, the view moved more than half a row: row 8 is under the top cell.
  CHECK(axis.shown() > 7.5 && axis.indexAt(0) == 8);
}

TEST(scroll_axis_jumps_when_not_smooth) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.scrollTo(7);
  CHECK(axis.shown() == 7.0 && !axis.animating() && axis.fraction() == 0.0);
  axis.reveal(40);
  CHECK(axis.shown() == static_cast<double>(axis.offset()));
  // Smooth off again in the middle of an animation: the view lands.
  axis.setSmooth(true);
  axis.scrollTo(0);
  CHECK(axis.animating());
  axis.setSmooth(false);
  CHECK(!axis.animating() && axis.shown() == 0.0);
}

TEST(scroll_axis_keeps_the_shown_position_inside_a_shrunk_content) {
  ScrollAxis axis;
  axis.setExtent(100, 10);
  axis.setSmooth(true);
  axis.scrollTo(80);
  for (int i = 0; i < 200; i++) axis.tick(0.016);
  axis.setExtent(30, 10);
  CHECK(axis.offset() == 20 && axis.shown() == 20.0);
}
