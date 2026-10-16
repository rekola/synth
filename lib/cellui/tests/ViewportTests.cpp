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
