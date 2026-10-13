#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/term/TestTerminal.h>

#include <poll.h>

using namespace cellui;
using namespace cellui::term;

namespace {

bool readable(int fd) {
  struct pollfd p = { fd, POLLIN, 0 };
  return ::poll(&p, 1, 0) > 0;
}

// What an application does with a terminal: draw on a root plane, present.
void draw(TestTerminal & t, BufferPlane & root, CellBuffer & screen, const std::string & focus = "") {
  root.refresh();
  FrameInfo info;
  info.planes = root.describe();
  info.focus = focus;
  t.present(screen, {}, root.images(), &info);
}

}  // namespace

TEST(testterminal_reports_its_size_and_wants_frame_info) {
  TestTerminal t(5, 20);
  CHECK(t.rows() == 5);
  CHECK(t.cols() == 20);
  CHECK(t.capabilities().rows == 5);
  CHECK(t.wantsFrameInfo());
  CHECK(!t.nextDeadline());
}

TEST(testterminal_input_is_readable_until_polled) {
  TestTerminal t;
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

TEST(testterminal_wake_is_its_own_fd) {
  TestTerminal t;
  CHECK(!readable(t.wakeFd()));
  t.wake();
  CHECK(readable(t.wakeFd()));
  CHECK(!readable(t.inputFd()));
  t.poll();
  CHECK(!readable(t.wakeFd()));
}

TEST(testterminal_text_mouse_paste_resize_and_end) {
  TestTerminal t(10, 10);
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

TEST(testterminal_keeps_the_presented_frame) {
  TestTerminal t(3, 10);
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

TEST(testterminal_json_has_text_styles_planes_and_focus) {
  TestTerminal t(2, 8);
  StyleProvider styles;
  CellBuffer screen(2, 8);
  BufferPlane root(screen, styles);
  root.setFgColor(255, 0, 0);
  root.setBold(true);
  root.putstr(0, 0, "ab");
  root.setBold(false);
  root.setFgColor(0, 255, 0);
  root.putstr(0, 2, "c\"");
  auto child = root.createChild();
  child->setName("box");
  child->resize(1, 3);
  child->move(1, 2);
  child->putstr(0, 0, "xyz");
  draw(t, root, screen, "editor");
  std::string expected =
    "{\"rows\":2,\"cols\":8,\"cursor\":null,\"focus\":\"editor\","
    "\"planes\":{\"y\":0,\"x\":0,\"rows\":2,\"cols\":8,\"visible\":true,\"children\":["
    "{\"name\":\"box\",\"y\":1,\"x\":2,\"rows\":1,\"cols\":3,\"visible\":true,\"children\":[]}]},"
    "\"lines\":[{\"text\":\"abc\\\"\",\"styles\":["
    "{\"x\":0,\"cols\":2,\"fg\":\"#ff0000\",\"bold\":true},{\"x\":2,\"cols\":2,\"fg\":\"#00ff00\"}]},"
    "{\"text\":\"  xyz\",\"styles\":[]}],\"images\":[]}";
  CHECK(t.toJson() == expected);
}

TEST(testterminal_json_reports_the_cursor_and_images) {
  TestTerminal t(2, 4);
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
  std::string json = t.toJson();
  CHECK(json.find("\"cursor\":{\"y\":1,\"x\":2}") != std::string::npos);
  CHECK(json.find("\"images\":[{\"row\":0,\"col\":1,\"width\":10,\"height\":10}]") != std::string::npos);
  CHECK(json.find("\"reader\":true") != std::string::npos);
  CHECK(json.find("\"image\":0") != std::string::npos);
}
