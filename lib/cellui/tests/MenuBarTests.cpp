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
Input click(int y, int x) { return Input(cellui::Key::Button1, y, x, false, false, false, false, Input::Kind::RELEASE); }

}  // namespace

TEST(menubar_header_and_open_section) {
  StyleProvider styles;
  CellBuffer b(8, 30);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  MenuBar menu(*plane);
  menu.setSections(sections());
  menu.layout(30);
  CHECK(plane->getDim() == std::pair(5, 30));
  root.refresh();
  CHECK(b.toPlainText() == "  File  Help\n");
  CHECK(b.at(0, 2).bold && b.at(0, 2).underline && !b.at(0, 3).bold);
  CHECK(menu.offerInput(key('f', true)));
  root.refresh();
  CHECK(b.toPlainText() == "  File  Help\n   Open\n   ────\n   Quit\n");
  CHECK(menu.selectedItem() == 0);
  // Down skips the separator, and wraps.
  menu.offerInput(key(cellui::Key::Down));
  CHECK(menu.selectedItem() == 2);
  menu.offerInput(key(cellui::Key::Down));
  CHECK(menu.selectedItem() == 0);
  menu.offerInput(key(cellui::Key::Right));
  CHECK(menu.openSection() == 1 && menu.selectedItem() == 0);
  menu.offerInput(key(cellui::Key::Enter));
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
  CHECK(!menu.offerInput(click(2, 4)));  // the separator
  CHECK(menu.offerInput(click(3, 4)));
  auto a = menu.takeActivated();
  CHECK(a && a->first == 0 && a->second == 2);
  CHECK(!menu.isOpen());
  CHECK(menu.offerInput(click(0, 9)));
  CHECK(menu.openSection() == 1);
  CHECK(menu.offerInput(click(0, 9)));
  CHECK(!menu.isOpen());
  CHECK(!menu.offerInput(key(cellui::Key::Escape)));
}

TEST(menubar_panel_stays_on_screen_and_is_opaque_without_a_border) {
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
  CHECK(b.toPlainText() == "  File  Help\n   About it\n");
  // The panel is one column wider each side of the label and its colours
  // are its own, not an average with what is below.
  CHECK(b.at(1, 2).bg && b.at(1, 11).bg);
  CHECK(b.at(1, 0).text == " " && !b.at(1, 0).bg);
  CHECK(b.at(0, 0).bg && b.at(0, 0).bg->getRed() == MenuBar::Colors().header_bg.getRed());
}

TEST(menubar_open_section_floats_over_the_planes_below) {
  StyleProvider styles;
  CellBuffer b(8, 30);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  MenuBar menu(*plane);
  menu.setSections(sections());
  menu.layout(30);
  // A plane created after the bar, at the base, doesn't cover the section.
  auto later = root.createChild();
  later->resize(8, 30);
  later->move(1, 0);
  later->putstr(0, 0, std::string(30, 'x'));
  menu.open(0);
  root.refresh();
  CHECK(b.toPlainText().find("xx Open") != std::string::npos);
  auto areas = root.takeElevations();
  CHECK(areas.size() == 1 && areas[0].name == "menu-section" && areas[0].elevation > 0);
  CHECK(areas[0].row == 1 && areas[0].rows == 4);
  menu.close();
  root.refresh();
  CHECK(root.takeElevations().empty());
}
