#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/InputDispatcher.h>

#include <memory>
#include <string>
#include <vector>

using namespace cellui;

namespace {

using Log = std::vector<std::string>;

// A widget with a plane of its own, placed and sized.
class Panel : public Widget {
 public:
  Panel(Plane & parent, int y, int x, int rows, int cols) : Widget(parent) {
    move(y, x);
    resize(rows, cols);
  }
  Plane & plane() { return getPlane(); }
};

// A screen of 10 x 40 cells with the dispatcher over a root panel.
struct Screen {
  StyleProvider styles;
  CellBuffer cells{ 10, 40 };
  BufferPlane root{ cells, styles };
  Panel top{ root, 0, 0, 10, 40 };
  InputDispatcher dispatcher{ top };
  std::vector<Connection> keep;

  // Draws the frame and gives the dispatcher its areas.
  void refresh() {
    root.refresh();
    dispatcher.setHitAreas(root.takeHitAreas());
  }

  template <class E>
  void watch(Widget & widget, Log & log, const std::string & name) {
    keep.push_back(widget.on<E>([&log, name](const E &, EventContext &) {
      log.push_back(name);
      return Handled::no;
    }));
  }
};

MouseDown down(int row, int col, int button = 1) { return MouseDown{ float(col), float(row), row, col, button, {} }; }
MouseMove move(int row, int col, unsigned buttons = 0) { return MouseMove{ float(col), float(row), row, col, buttons, {} }; }
MouseUp up(int row, int col, int button = 1) { return MouseUp{ float(col), float(row), row, col, button, {} }; }

}  // namespace

TEST(hit_test_finds_the_topmost_widget_under_a_cell) {
  Screen s;
  Panel a(s.top.plane(), 1, 1, 4, 10);
  Panel b(s.top.plane(), 1, 5, 4, 10);  // over a's right part
  a.attach(s.top);
  b.attach(s.top);
  s.refresh();
  CHECK(s.dispatcher.hitTest(2, 2) == &a);
  CHECK(s.dispatcher.hitTest(2, 7) == &b);
  CHECK(s.dispatcher.hitTest(2, 20) == &s.top);
  CHECK(s.dispatcher.hitTest(9, 39) == &s.top);
  CHECK(s.dispatcher.hitTest(20, 20) == nullptr);
}

TEST(hit_test_prefers_a_higher_plane_over_a_later_one) {
  Screen s;
  Panel low(s.top.plane(), 0, 0, 5, 5);
  Panel high(s.top.plane(), 0, 0, 5, 5);
  low.attach(s.top);
  high.attach(s.top);
  low.plane().setElevation(2.0f);
  s.refresh();
  CHECK(s.dispatcher.hitTest(1, 1) == &low);
}

TEST(hit_test_skips_hidden_planes_and_widgets_of_another_tree) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 5, 5);
  a.attach(s.top);
  s.refresh();
  CHECK(s.dispatcher.hitTest(1, 1) == &a);
  a.detach();
  CHECK(s.dispatcher.hitTest(1, 1) == &s.top);
  a.attach(s.top);
  a.setVisible(false);
  s.refresh();
  CHECK(s.dispatcher.hitTest(1, 1) == &s.top);
}

TEST(a_destroyed_widget_is_not_hit_from_an_old_frame) {
  Screen s;
  {
    Panel a(s.top.plane(), 0, 0, 5, 5);
    a.attach(s.top);
    s.refresh();
    CHECK(s.dispatcher.hitTest(1, 1) == &a);
  }
  CHECK(s.dispatcher.hitTest(1, 1) == &s.top);
}

TEST(mouse_events_reach_the_widget_under_the_pointer_and_bubble) {
  Screen s;
  Panel a(s.top.plane(), 1, 1, 4, 10);
  a.attach(s.top);
  s.refresh();
  Log log;
  s.watch<MouseDown>(a, log, "a down");
  s.watch<MouseDown>(s.top, log, "top down");
  s.dispatcher.mouseDown(down(2, 2));
  CHECK(log == (Log{ "a down", "top down" }));
  log.clear();
  s.dispatcher.mouseUp(up(2, 2));
  s.dispatcher.mouseDown(down(8, 30));
  CHECK(log == (Log{ "top down" }));
}

TEST(a_press_grabs_the_pointer_until_the_last_button_is_up) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 4, 10);
  Panel b(s.top.plane(), 0, 20, 4, 10);
  a.attach(s.top);
  b.attach(s.top);
  s.refresh();
  Log log;
  s.watch<MouseMove>(a, log, "a move");
  s.watch<MouseMove>(b, log, "b move");
  s.watch<MouseUp>(a, log, "a up");
  s.watch<MouseUp>(b, log, "b up");
  s.dispatcher.mouseDown(down(1, 1));
  s.dispatcher.mouseDown(down(1, 1, 3));
  CHECK(s.dispatcher.pressedButtons() == 5u);
  s.dispatcher.mouseMove(move(1, 25, 5));
  s.dispatcher.mouseUp(up(1, 25, 1));
  s.dispatcher.mouseMove(move(1, 26, 4));  // button 3 still holds it
  s.dispatcher.mouseUp(up(1, 26, 3));
  s.dispatcher.mouseMove(move(1, 27));
  CHECK(log == (Log{ "a move", "a up", "a move", "a up", "b move" }));
  CHECK(s.dispatcher.pressedButtons() == 0u);
}

TEST(the_grab_survives_the_grabbed_widget_being_destroyed) {
  Screen s;
  auto a = std::make_unique<Panel>(s.top.plane(), 0, 0, 4, 10);
  a->attach(s.top);
  s.refresh();
  Log log;
  s.watch<MouseUp>(s.top, log, "top up");
  s.dispatcher.mouseDown(down(1, 1));
  a.reset();
  s.dispatcher.mouseUp(up(1, 1));
  CHECK(log == (Log{ "top up" }));
  CHECK(s.dispatcher.pressedButtons() == 0u);
}

TEST(enter_and_leave_follow_the_pointer_outermost_first_in_deepest_first_out) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 5, 20);
  Panel a1(a.plane(), 0, 0, 3, 8);
  Panel b(s.top.plane(), 0, 25, 5, 10);
  a.attach(s.top);
  a1.attach(a);
  b.attach(s.top);
  s.refresh();
  Log log;
  for (auto * w : { &s.top, &a, &a1, &b }) {
    std::string name = w == &s.top ? "top" : w == &a ? "a" : w == &a1 ? "a1" : "b";
    s.watch<Enter>(*w, log, "enter " + name);
    s.watch<Leave>(*w, log, "leave " + name);
  }
  s.dispatcher.mouseMove(move(1, 1));
  CHECK(log == (Log{ "enter top", "enter a", "enter a1" }));
  log.clear();
  s.dispatcher.mouseMove(move(1, 12));  // a, outside a1
  CHECK(log == (Log{ "leave a1" }));
  log.clear();
  s.dispatcher.mouseMove(move(1, 28));
  CHECK(log == (Log{ "leave a", "enter b" }));
  log.clear();
  s.dispatcher.mouseMove(move(1, 29));
  CHECK(log.empty());
  s.dispatcher.pointerLeft();
  CHECK(log == (Log{ "leave b", "leave top" }));
}

TEST(enter_and_leave_do_not_travel_to_ancestors) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 5, 20);
  a.attach(s.top);
  s.refresh();
  s.dispatcher.mouseMove(move(1, 1));
  Log log;
  s.watch<Leave>(s.top, log, "top leave");
  s.watch<Enter>(s.top, log, "top enter");
  s.dispatcher.mouseMove(move(1, 30));  // still top, a left
  CHECK(log.empty());
}

TEST(the_hover_follows_the_grab_and_catches_up_on_release) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 4, 10);
  Panel b(s.top.plane(), 0, 20, 4, 10);
  a.attach(s.top);
  b.attach(s.top);
  s.refresh();
  Log log;
  s.watch<Enter>(b, log, "enter b");
  s.watch<Leave>(a, log, "leave a");
  s.dispatcher.mouseDown(down(1, 1));
  s.dispatcher.mouseMove(move(1, 25, 1));
  CHECK(log.empty());
  s.dispatcher.mouseUp(up(1, 25));
  CHECK(log == (Log{ "leave a", "enter b" }));
}

TEST(a_press_focuses_the_nearest_focusable_widget) {
  Screen s;
  Panel field(s.top.plane(), 0, 0, 5, 20);
  Panel inner(field.plane(), 0, 0, 2, 5);
  field.attach(s.top);
  inner.attach(field);
  field.setFocusable(true);
  s.refresh();
  Log log;
  s.watch<FocusIn>(field, log, "in");
  s.watch<FocusOut>(field, log, "out");
  s.dispatcher.mouseDown(down(0, 0));
  CHECK(s.dispatcher.focused() == &field);
  CHECK(log == (Log{ "in" }));
  s.dispatcher.mouseUp(up(0, 0));
  s.dispatcher.mouseDown(down(8, 30));  // nothing focusable there: focus stays
  CHECK(s.dispatcher.focused() == &field);
  s.dispatcher.setFocus(nullptr);
  CHECK(log == (Log{ "in", "out" }));
}

TEST(keys_go_to_the_focused_widget_or_the_root) {
  Screen s;
  Panel field(s.top.plane(), 0, 0, 5, 20);
  field.attach(s.top);
  Log log;
  s.watch<KeyDown>(field, log, "field");
  s.watch<KeyDown>(s.top, log, "top");
  s.dispatcher.keyDown(KeyDown{ 'a', {}, "a", false });
  CHECK(log == (Log{ "top" }));
  log.clear();
  s.dispatcher.setFocus(&field);
  s.dispatcher.keyDown(KeyDown{ 'a', {}, "a", false });
  CHECK(log == (Log{ "field", "top" }));
}

TEST(a_destroyed_focus_sends_keys_to_the_root) {
  Screen s;
  auto field = std::make_unique<Panel>(s.top.plane(), 0, 0, 5, 20);
  field->attach(s.top);
  s.dispatcher.setFocus(field.get());
  field.reset();
  CHECK(s.dispatcher.focused() == nullptr);
  Log log;
  s.watch<KeyDown>(s.top, log, "top");
  s.dispatcher.keyDown(KeyDown{ 'a', {}, "a", false });
  CHECK(log == (Log{ "top" }));
}

TEST(focus_traversal_walks_the_focusable_widgets_in_tree_order_and_wraps) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 2, 5);
  Panel b(s.top.plane(), 2, 0, 2, 5);
  Panel c(s.top.plane(), 4, 0, 2, 5);
  Panel plain(s.top.plane(), 6, 0, 2, 5);
  for (Panel * p : { &a, &b, &c, &plain }) p->attach(s.top);
  for (Panel * p : { &a, &b, &c }) p->setFocusable(true);
  CHECK(s.dispatcher.focusNext());
  CHECK(s.dispatcher.focused() == &a);
  s.dispatcher.focusNext();
  CHECK(s.dispatcher.focused() == &b);
  s.dispatcher.focusNext();
  s.dispatcher.focusNext();
  CHECK(s.dispatcher.focused() == &a);
  s.dispatcher.focusPrev();
  CHECK(s.dispatcher.focused() == &c);
  s.dispatcher.setFocus(nullptr);
  s.dispatcher.focusPrev();
  CHECK(s.dispatcher.focused() == &c);
}

TEST(focus_traversal_with_nothing_focusable_does_nothing) {
  Screen s;
  CHECK(!s.dispatcher.focusNext());
  CHECK(!s.dispatcher.focusPrev());
  CHECK(s.dispatcher.focused() == nullptr);
}

TEST(wheel_goes_to_the_widget_under_the_pointer_not_the_focus) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 4, 10);
  Panel b(s.top.plane(), 0, 20, 4, 10);
  a.attach(s.top);
  b.attach(s.top);
  s.refresh();
  Log log;
  s.watch<Wheel>(a, log, "a");
  s.watch<Wheel>(b, log, "b");
  b.setFocusable(true);
  s.dispatcher.setFocus(&b);
  s.dispatcher.wheel(Wheel{ 1, 1, 1, 1, 0, 1, {} });
  CHECK(log == (Log{ "a" }));
}

TEST(resize_reaches_every_handler_of_the_tree) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 4, 10);
  a.attach(s.top);
  Log log;
  s.watch<Resize>(a, log, "a");
  s.watch<Resize>(s.top, log, "top");
  s.dispatcher.resize(Resize{ 30, 100 });
  CHECK(log == (Log{ "top", "a" }));
}

TEST(offer_translates_the_pointer_inputs) {
  Screen s;
  Panel a(s.top.plane(), 0, 0, 4, 10);
  a.attach(s.top);
  s.refresh();
  Log log;
  s.watch<MouseDown>(a, log, "down");
  s.watch<MouseMove>(a, log, "move");
  s.watch<MouseUp>(a, log, "up");
  s.watch<Wheel>(a, log, "wheel");
  using K = Input::Kind;
  s.dispatcher.offer(Input(Key::Button1, 1, 2, false, false, false, false, K::PRESS));
  s.dispatcher.offer(Input(Key::Button1, 1, 3, false, false, false, false, K::REPEAT));
  s.dispatcher.offer(Input(Key::Button1, 1, 3, false, false, false, false, K::RELEASE));
  s.dispatcher.offer(Input(Key::Motion, 1, 4, false, false, false, false));
  s.dispatcher.offer(Input(Key::Button4, 1, 4, false, false, false, false, K::PRESS));
  CHECK(log == (Log{ "down", "move", "up", "move", "wheel" }));
}

TEST(offer_translates_keys_with_their_text) {
  Screen s;
  std::vector<KeyDown> downs;
  std::vector<KeyUp> ups;
  s.keep.push_back(s.top.on<KeyDown>([&](const KeyDown & e, EventContext &) {
    downs.push_back(e);
    return Handled::no;
  }));
  s.keep.push_back(s.top.on<KeyUp>([&](const KeyUp & e, EventContext &) {
    ups.push_back(e);
    return Handled::no;
  }));
  using K = Input::Kind;
  s.dispatcher.offer(keyInput('A', kModShift, K::PRESS));
  s.dispatcher.offer(keyInput('x', kModCtrl, K::PRESS));
  s.dispatcher.offer(keyInput(Key::Up, 0, K::REPEAT));
  s.dispatcher.offer(keyInput(0x00e9, 0, K::PRESS));  // é
  s.dispatcher.offer(keyInput('a', 0, K::RELEASE));
  CHECK(downs.size() == 4);
  CHECK(downs[0].text == "A");
  CHECK(downs[0].mods.shift);
  CHECK(downs[1].text.empty() && downs[1].mods.ctrl);
  CHECK(downs[2].text.empty() && downs[2].repeat);
  CHECK(downs[3].text == "\xc3\xa9");
  CHECK(ups.size() == 1 && ups[0].key == 'a');
}

TEST(a_handler_may_destroy_widgets_during_hover_changes) {
  Screen s;
  auto a = std::make_unique<Panel>(s.top.plane(), 0, 0, 4, 10);
  a->attach(s.top);
  s.refresh();
  s.dispatcher.mouseMove(move(1, 1));
  s.keep.push_back(a->on<Leave>([&](const Leave &, EventContext &) {
    a.reset();
    return Handled::no;
  }));
  s.dispatcher.mouseMove(move(8, 30));
  CHECK(a == nullptr);
  s.dispatcher.mouseMove(move(8, 31));
}
