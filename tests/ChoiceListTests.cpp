#include "TestFramework.h"

#include "../src/ui/ChoiceList.h"

TEST(choice_list_starts_on_the_requested_entry_and_clamps_it) {
  ChoiceList list;
  list.reset(5, 3);
  CHECK(list.selected() == 3);
  list.reset(5, 99);
  CHECK(list.selected() == 4);
  list.reset(5, -2);
  CHECK(list.selected() == 0);
}

TEST(choice_list_stops_at_the_ends_instead_of_wrapping) {
  ChoiceList list;
  list.reset(3, 0);
  list.move(-1);
  CHECK(list.selected() == 0);
  list.move(10);
  CHECK(list.selected() == 2);
  list.home();
  CHECK(list.selected() == 0);
  list.end();
  CHECK(list.selected() == 2);
}

TEST(choice_list_empty_has_nothing_to_select_and_does_not_crash) {
  ChoiceList list;
  list.reset(0, 0);
  list.move(1);
  list.end();
  CHECK(list.selected() == 0);
  CHECK(list.firstVisible(4) == 0);
  list.scroll(3, 4);
  CHECK(list.selected() == 0);
}

TEST(choice_list_window_scrolls_only_as_far_as_needed) {
  ChoiceList list;
  list.reset(10, 0);
  CHECK(list.firstVisible(4) == 0);
  list.move(3); // still inside rows 0..3
  CHECK(list.firstVisible(4) == 0);
  list.move(1); // one past the window: it moves by exactly one
  CHECK(list.firstVisible(4) == 1);
  list.move(-1); // back inside - the window stays put
  CHECK(list.firstVisible(4) == 1);
  list.home();
  CHECK(list.firstVisible(4) == 0);
  list.end();
  CHECK(list.firstVisible(4) == 6);
}

TEST(choice_list_opens_scrolled_to_a_selection_below_the_first_window) {
  ChoiceList list;
  list.reset(10, 8);
  auto first = list.firstVisible(4);
  CHECK(first <= 8 && 8 < first + 4);
  CHECK(first == 5);
}

TEST(choice_list_a_window_taller_than_the_list_never_scrolls) {
  ChoiceList list;
  list.reset(3, 2);
  CHECK(list.firstVisible(8) == 0);
}

TEST(choice_list_wheel_scroll_keeps_the_selection_in_view) {
  ChoiceList list;
  list.reset(10, 0);
  list.scroll(3, 4); // window now 3..6; the selection was at 0
  CHECK(list.firstVisible(4) == 3);
  CHECK(list.selected() == 3);
  list.scroll(100, 4); // can't scroll past the end
  CHECK(list.firstVisible(4) == 6);
  CHECK(list.selected() == 6);
  list.scroll(-100, 4);
  CHECK(list.firstVisible(4) == 0);
}
