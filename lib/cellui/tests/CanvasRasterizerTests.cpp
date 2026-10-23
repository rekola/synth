#include "TestFramework.h"

#include <cellui/CellRasterizer.h>

#ifndef __APPLE__
#include <fontconfig/fontconfig.h>
#endif

#include <cstdio>

using namespace cellui;

namespace {

uint32_t pixelAt(const Image & img, int x, int y) {
  return img.pixels[static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)];
}

}  // namespace

TEST(canvas_rasterizer_draws_backgrounds_blocks_and_images) {
  auto rasterizer = canvasRasterizer();
  CHECK(rasterizer != nullptr);
  CellBuffer cells(1, 3);
  CellBuffer::Cell red;
  red.bg = Color(255, 0, 0);
  cells.put(0, 0, red);
  CellBuffer::Cell block;
  block.text = "█";
  block.fg = Color(0, 255, 0);
  cells.put(0, 1, block);
  auto blue = std::make_shared<Image>();
  blue->width = 10;
  blue->height = 20;
  blue->pixels.assign(200, Image::pack(0, 0, 255));
  ImagePlacement p;
  p.image = blue;
  p.col = 2;
  CellBuffer::Cell marked;
  marked.image = 1;
  cells.put(0, 2, marked);

  Image img = rasterizer->render(cells, {p}, 0, 0, 1, 3, 20, 10, 1, Color(255, 255, 255), Color(0, 0, 0));
  CHECK(img.width == 30 && img.height == 20);
  CHECK(Image::red(pixelAt(img, 5, 10)) == 255 && Image::green(pixelAt(img, 5, 10)) == 0);
  CHECK(Image::green(pixelAt(img, 15, 10)) == 255 && Image::red(pixelAt(img, 15, 10)) == 0);
  CHECK(Image::blue(pixelAt(img, 25, 10)) == 255 && Image::red(pixelAt(img, 25, 10)) == 0);

  // Drawn smaller: the same picture at half the size.
  Image half = rasterizer->render(cells, {p}, 0, 0, 1, 3, 20, 10, 2, Color(255, 255, 255), Color(0, 0, 0));
  CHECK(half.width == 15 && half.height == 10);
  CHECK(Image::red(pixelAt(half, 2, 5)) == 255);
}

TEST(canvas_rasterizer_draws_text_in_the_foreground) {
  auto rasterizer = canvasRasterizer();
  CellBuffer cells(1, 1);
  CellBuffer::Cell m;
  m.text = "M";
  m.fg = Color(255, 255, 255);
  cells.put(0, 0, m);
  Image img = rasterizer->render(cells, {}, 0, 0, 1, 1, 40, 20, 1, Color(255, 255, 255), Color(0, 0, 0));
  int lit = 0;
  for (uint32_t px : img.pixels) lit += Image::red(px) > 128;
  // Some ink, but not a filled cell: a glyph from the system's font.
  CHECK(lit > 20 && lit < 600);
}

namespace {

// How many pixels of a one-cell picture of `text` are lit.
int inkOf(CellRasterizer & rasterizer, const std::string & text) {
  CellBuffer cells(1, 1);
  CellBuffer::Cell cell;
  cell.text = text;
  cell.fg = Color(255, 255, 255);
  cells.put(0, 0, cell);
  Image img = rasterizer.render(cells, {}, 0, 0, 1, 1, 40, 20, 1, Color(255, 255, 255), Color(0, 0, 0));
  int lit = 0;
  for (uint32_t px : img.pixels) lit += Image::red(px) > 128;
  return lit;
}

#ifdef __APPLE__
// Core Text falls back to a font that has it (Apple Symbols has the musical
// symbols), so the drawing is checked rather than skipped.
bool someFontHas(uint32_t) { return true; }
#else
// Whether any installed font has the character.
bool someFontHas(uint32_t cp) {
  FcInit();
  FcCharSet * wanted = FcCharSetCreate();
  FcCharSetAddChar(wanted, cp);
  FcPattern * pattern = FcPatternCreate();
  FcPatternAddCharSet(pattern, FC_CHARSET, wanted);
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result;
  FcPattern * best = FcFontMatch(nullptr, pattern, &result);
  bool has = false;
  FcCharSet * set = nullptr;
  if (best && FcPatternGetCharSet(best, FC_CHARSET, 0, &set) == FcResultMatch) has = FcCharSetHasChar(set, cp);
  if (best) FcPatternDestroy(best);
  FcPatternDestroy(pattern);
  FcCharSetDestroy(wanted);
  return has;
}
#endif

}  // namespace

// The accidentals of the microtonal note names are in few fonts, rarely the
// monospace one: a character it lacks is drawn from one that has it, not left
// as a box or a gap.
TEST(canvas_rasterizer_draws_a_character_the_font_lacks_from_another_font) {
  if (!someFontHas(0x1D12B)) {
    std::printf("SKIP: no installed font has the double flat\n");
    return;
  }
  auto rasterizer = canvasRasterizer();
  // A character no font has is whatever the system draws for a missing one.
  int missing = inkOf(*rasterizer, "\xF4\x8F\xBF\xBD");
  int double_flat = inkOf(*rasterizer, "\xF0\x9D\x84\xAB");  // U+1D12B
  CHECK(double_flat > 20);
  CHECK(double_flat != missing);
}

// Text around it stays in the font asked for, in its own cells.
TEST(canvas_rasterizer_keeps_the_cells_of_text_around_a_fallback_character) {
  if (!someFontHas(0x1D12B)) {
    std::printf("SKIP: no installed font has the double flat\n");
    return;
  }
  auto rasterizer = canvasRasterizer();
  CellBuffer cells(1, 3);
  for (int c = 0; c < 3; c++) {
    CellBuffer::Cell cell;
    cell.text = c == 1 ? "\xF0\x9D\x84\xAB" : "M";  // M, a double flat, M
    cell.fg = Color(255, 255, 255);
    cells.put(0, c, cell);
  }
  Image img = rasterizer->render(cells, {}, 0, 0, 1, 3, 40, 20, 1, Color(255, 255, 255), Color(0, 0, 0));
  auto lit = [&](int c) {
    int n = 0;
    for (int y = 0; y < 40; y++)
      for (int x = c * 20; x < (c + 1) * 20; x++) n += Image::red(pixelAt(img, x, y)) > 128;
    return n;
  };
  CHECK(lit(0) > 20 && lit(1) > 5 && lit(2) > 20);
  // The third cell is the first one's twin.
  CHECK(lit(0) == lit(2));
}

// An underlined cell has a line across its bottom, and an italic one a slanted glyph.
TEST(canvas_rasterizer_draws_underlines_and_italics) {
  auto rasterizer = canvasRasterizer();
  auto render = [&](bool underline, bool italic) {
    CellBuffer cells(1, 1);
    CellBuffer::Cell cell;
    cell.text = "M";
    cell.fg = Color(255, 255, 255);
    cell.underline = underline;
    cell.italic = italic;
    cells.put(0, 0, cell);
    return rasterizer->render(cells, {}, 0, 0, 1, 1, 40, 20, 1, Color(255, 255, 255), Color(0, 0, 0));
  };
  auto plain = render(false, false);
  auto underlined = render(true, false);
  auto italic = render(false, true);
  // The whole width of a row near the bottom is lit.
  int lit_row = 0;
  for (int y = 0; y < underlined.height; y++) {
    int lit = 0;
    for (int x = 0; x < underlined.width; x++) lit += Image::red(pixelAt(underlined, x, y)) > 128;
    if (lit == underlined.width) lit_row = y;
  }
  CHECK(lit_row > underlined.height / 2);
  CHECK(plain.pixels != underlined.pixels);
  CHECK(plain.pixels != italic.pixels);
}
