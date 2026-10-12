#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/Gallery.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace cellui;

namespace {

std::string renderPage(int & rows) {
  Theme theme = Theme::dark();
  StyleProvider styles(theme);
  CellBuffer buffer(120, 72);
  BufferPlane plane(buffer, styles);
  Gallery gallery(plane, theme);
  plane.refresh();
  rows = gallery.rows();
  return buffer.toPlainText();
}

}  // namespace

// The page as plain text is compared with tests/golden/gallery.txt. After a
// deliberate change, run the tests with CELLUI_UPDATE_GOLDEN=1 to rewrite it.
TEST(gallery_page_matches_the_golden_text) {
  int rows = 0;
  std::string page = renderPage(rows);
  std::string path = std::string(CELLUI_TEST_DIR) + "/golden/gallery.txt";
  if (std::getenv("CELLUI_UPDATE_GOLDEN")) {
    std::ofstream(path, std::ios::binary) << page;
  }
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::stringstream expected;
  expected << in.rdbuf();
  CHECK(page == expected.str());
}

TEST(gallery_row_count_matches_the_text) {
  int rows = 0;
  std::string page = renderPage(rows);
  int lines = 0;
  for (char c : page) lines += c == '\n';
  CHECK(rows > 40);
  // The page ends with a widget a few rows tall that draws on only some of
  // them, so the text has at most as many lines as the page has rows.
  CHECK(lines <= rows && rows - lines < 6);
}

TEST(gallery_ansi_carries_the_theme_colours) {
  Theme theme = Theme::dark();
  StyleProvider styles(theme);
  CellBuffer buffer(120, 72);
  BufferPlane plane(buffer, styles);
  Gallery gallery(plane, theme);
  plane.refresh();
  std::string ansi = buffer.toAnsi(gallery.rows());
  // The playing token #50e070 is a background in the accent-and-state list.
  CHECK(ansi.find("48;2;80;224;112") != std::string::npos);
  // Bold, italic and underline each appear.
  CHECK(ansi.find(";1") != std::string::npos);
  CHECK(ansi.find(";3") != std::string::npos);
  CHECK(ansi.find(";4") != std::string::npos);
  CHECK(ansi.size() > 4000);
}
