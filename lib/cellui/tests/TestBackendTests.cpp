#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/TestBackend.h>

#include <poll.h>

using namespace cellui;
using namespace cellui::term;
using cellui::test::TestBackend;

namespace {

bool readable(int fd) {
  struct pollfd p = { fd, POLLIN, 0 };
  return ::poll(&p, 1, 0) > 0;
}

// What an application does with a terminal: draw on a root plane, present.
void draw(TestBackend & t, BufferPlane & root, CellBuffer & screen, const std::string & focus = "") {
  root.refresh();
  FrameInfo info;
  info.planes = root.describe();
  info.focus = focus;
  t.present(screen, {}, root.images(), &info);
}

}  // namespace

TEST(testbackend_reports_its_size_and_wants_frame_info) {
  TestBackend t(5, 20);
  CHECK(t.rows() == 5);
  CHECK(t.cols() == 20);
  CHECK(t.capabilities().rows == 5);
  CHECK(t.wantsFrameInfo());
  CHECK(!t.nextDeadline());
}

TEST(testbackend_input_is_readable_until_polled) {
  TestBackend t;
  CHECK(!readable(t.inputFd()));
  t.pushKey('a');
  t.pushKey(Key::Up, kModCtrl);
  CHECK(readable(t.inputFd()));
  auto events = t.poll();
  CHECK(events.size() == 2);
  CHECK(events[0].key == 'a');
  CHECK(events[1].key == Key::Up);
  CHECK(events[1].mods == kModCtrl);
  CHECK(!readable(t.inputFd()));
  CHECK(t.poll().empty());
}

TEST(testbackend_wake_is_its_own_fd) {
  TestBackend t;
  CHECK(!readable(t.wakeFd()));
  t.wake();
  CHECK(readable(t.wakeFd()));
  CHECK(!readable(t.inputFd()));
  t.poll();
  CHECK(!readable(t.wakeFd()));
}

TEST(testbackend_text_mouse_paste_resize_and_end) {
  TestBackend t(10, 10);
  t.pushText("a\xC3\xA4");
  t.pushMouse(3, 4, Key::Button1);
  t.pushPaste("hello");
  t.resize(20, 30);
  t.pushEnd();
  auto e = t.poll();
  CHECK(e.size() == 6);
  CHECK(e[0].key == 'a');
  CHECK(e[1].key == 0xE4);
  CHECK(e[1].text == "\xC3\xA4");
  CHECK(e[2].type == TermEvent::Type::MOUSE);
  CHECK(e[2].y == 3 && e[2].x == 4);
  CHECK(e[3].type == TermEvent::Type::PASTE && e[3].text == "hello");
  CHECK(e[4].type == TermEvent::Type::RESIZE && e[4].rows == 20 && e[4].cols == 30);
  CHECK(e[5].type == TermEvent::Type::END);
  CHECK(t.rows() == 20 && t.cols() == 30);
}

TEST(testbackend_keeps_the_presented_frame) {
  TestBackend t(3, 10);
  StyleProvider styles;
  CellBuffer screen(3, 10);
  BufferPlane root(screen, styles);
  root.putstr(0, 1, "hi");
  CHECK(t.frameCount() == 0);
  draw(t, root, screen);
  CHECK(t.frameCount() == 1);
  CHECK(t.text() == " hi\n");
  t.redraw();
  CHECK(t.redrawCount() == 1);
}

TEST(testbackend_frame_gives_text_styles_planes_and_focus) {
  TestBackend t(2, 8);
  StyleProvider styles;
  CellBuffer screen(2, 8);
  BufferPlane root(screen, styles);
  root.setFgColor(255, 0, 0);
  root.setBold(true);
  root.putstr(0, 0, "ab");
  root.setBold(false);
  root.setFgColor(0, 255, 0);
  root.putstr(0, 2, "c");
  auto child = root.createChild();
  child->setName("box");
  child->resize(1, 3);
  child->move(1, 2);
  child->putstr(0, 0, "xyz");
  draw(t, root, screen, "editor");

  CHECK(t.text() == "abc\n  xyz\n");
  CHECK(t.frame().at(0, 0).bold && t.frame().at(0, 0).fg && t.frame().at(0, 0).fg->getRed() == 255);
  CHECK(!t.frame().at(0, 2).bold && t.frame().at(0, 2).fg && t.frame().at(0, 2).fg->getGreen() == 255);
  CHECK(t.info().focus == "editor");
  CHECK(t.info().planes.children.size() == 1);
  const auto & box = t.info().planes.children[0];
  CHECK(box.name == "box");
  CHECK(box.y == 1 && box.x == 2 && box.rows == 1 && box.cols == 3 && box.visible);
}

TEST(testbackend_frame_reports_the_cursor_and_images) {
  TestBackend t(2, 4);
  StyleProvider styles;
  CellBuffer screen(2, 4);
  BufferPlane root(screen, styles);
  root.setCellPixelSize(10, 5);
  root.drawImageAt(0, 1, std::vector<uint32_t>(10 * 5 * 2, 0xFF0000FFu), 10, 10);
  root.showReader("", 1, 0, 1, 4, "ab", std::nullopt);
  root.refresh();
  FrameInfo info;
  info.planes = root.describe();
  auto at = root.cursorPosition();
  CHECK(at.has_value());
  t.present(screen, { true, at->first, at->second }, root.images(), &info);

  CHECK(t.cursor().visible && t.cursor().y == 1 && t.cursor().x == 2);
  CHECK(t.images().size() == 1);
  CHECK(t.images()[0].row == 0 && t.images()[0].col == 1);
  CHECK(t.images()[0].image->width == 10 && t.images()[0].image->height == 10);
  // The cells the image covers are marked with its index + 1.
  CHECK(t.frame().at(0, 1).image == 1);
  CHECK(t.frame().at(0, 0).image == 0);
  // The reader is open on the plane showReader() was called on.
  CHECK(t.info().planes.reader);
}
