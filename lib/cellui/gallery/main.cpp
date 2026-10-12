// Prints the gallery page: every text style, colour token, sub-cell glyph,
// a Markdown document and a choice list, as a terminal shows them.
//   cellui_gallery            24-bit colour escape sequences
//   cellui_gallery --plain    the text alone
#include <cellui/BufferPlane.h>
#include <cellui/Gallery.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char ** argv) {
  bool plain = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--plain") == 0) plain = true;
    else {
      std::fprintf(stderr, "usage: %s [--plain]\n", argv[0]);
      return 2;
    }
  }
  cellui::Theme theme = cellui::Theme::dark();
  cellui::StyleProvider styles(theme);
  cellui::CellBuffer buffer(120, 72);
  cellui::BufferPlane plane(buffer, styles);
  cellui::Gallery gallery(plane, theme);
  plane.refresh();
  std::cout << (plain ? buffer.toPlainText() : buffer.toAnsi(gallery.rows()));
  return 0;
}
