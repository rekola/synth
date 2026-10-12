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
  rows = drawGallery(plane, theme);
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
    std::ofstream(path) << page;
  }
  std::ifstream in(path);
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
  // The last row drawn holds text, so the text has as many lines.
  CHECK(lines == rows);
}

TEST(gallery_ansi_carries_the_theme_colours) {
  Theme theme = Theme::dark();
  StyleProvider styles(theme);
  CellBuffer buffer(120, 72);
  BufferPlane plane(buffer, styles);
  int rows = drawGallery(plane, theme);
  std::string ansi = buffer.toAnsi(rows);
  // The playing token #50e070 is a background in the accent-and-state list.
  CHECK(ansi.find("48;2;80;224;112") != std::string::npos);
  // Bold, italic and underline each appear.
  CHECK(ansi.find(";1") != std::string::npos);
  CHECK(ansi.find(";3") != std::string::npos);
  CHECK(ansi.find(";4") != std::string::npos);
  CHECK(ansi.size() > 4000);
}
