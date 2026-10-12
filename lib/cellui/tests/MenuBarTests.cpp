#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/MenuBar.h>

using namespace cellui;

namespace {

std::vector<MenuBar::Section> sections() {
  return {
    {"File", 'f', {{"Open"}, {"", true}, {"Quit"}}},
    {"Help", 'h', {{"About"}}},
  };
}

Input key(int id, bool alt = false) { return Input(id, -1, -1, alt, false, false, false); }
Input click(int y, int x) { return Input(NCKEY_BUTTON1, y, x, false, false, false, false, Input::Kind::RELEASE); }

}  // namespace

TEST(menubar_header_and_open_section) {
  StyleProvider styles;
  CellBuffer b(8, 30);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  MenuBar menu(*plane);
  menu.setSections(sections());
  menu.layout(30);
  CHECK(plane->getDim() == std::pair(6, 30));
  root.refresh();
  CHECK(b.toPlainText() == "  File  Help\n");
  CHECK(b.at(0, 2).bold && b.at(0, 2).underline && !b.at(0, 3).bold);
  CHECK(menu.offerInput(key('f', true)));
  root.refresh();
  CHECK(b.toPlainText() == "  File  Help\n  ╭────╮\n  │Open│\n  ├────┤\n  │Quit│\n  ╰────╯\n");
  CHECK(menu.selectedItem() == 0);
  // Down skips the separator, and wraps.
  menu.offerInput(key(NCKEY_DOWN));
  CHECK(menu.selectedItem() == 2);
  menu.offerInput(key(NCKEY_DOWN));
  CHECK(menu.selectedItem() == 0);
  menu.offerInput(key(NCKEY_RIGHT));
  CHECK(menu.openSection() == 1 && menu.selectedItem() == 0);
  menu.offerInput(key(NCKEY_ENTER));
  CHECK(!menu.isOpen());
  auto a = menu.takeActivated();
  CHECK(a && a->first == 1 && a->second == 0);
  CHECK(!menu.takeActivated());
}

TEST(menubar_mouse) {
  StyleProvider styles;
  CellBuffer b(8, 30);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  MenuBar menu(*plane);
  menu.setSections(sections());
  menu.layout(30);
  CHECK(menu.offerInput(click(0, 3)));
  CHECK(menu.openSection() == 0);
  CHECK(!menu.offerInput(click(3, 4)));  // the separator
  CHECK(menu.offerInput(click(4, 4)));
  auto a = menu.takeActivated();
  CHECK(a && a->first == 0 && a->second == 2);
  CHECK(!menu.isOpen());
  CHECK(menu.offerInput(click(0, 9)));
  CHECK(menu.openSection() == 1);
  CHECK(menu.offerInput(click(0, 9)));
  CHECK(!menu.isOpen());
  CHECK(!menu.offerInput(key(NCKEY_ESC)));
}

TEST(menubar_box_stays_on_screen_and_is_translucent) {
  StyleProvider styles;
  CellBuffer b(4, 12);
  BufferPlane root(b, styles);
  root.setBgColor(0x80, 0x80, 0x80);
  root.putstr(0, 0, std::string(12, ' '));
  auto plane = root.createChild();
  MenuBar menu(*plane);
  menu.setSections({{"File", 'f', {}}, {"Help", 'h', {{"About it"}}}});
  menu.layout(12);
  menu.open(1);
  root.refresh();
  CHECK(b.toPlainText() == "  File  Help\n  ╭────────╮\n  │About it│\n  ╰────────╯\n");
  // The header's background is averaged with what is below it.
  CHECK(b.at(0, 0).bg && b.at(0, 0).bg->getRed() == (0x7f + 0x80) / 2);
}
