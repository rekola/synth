#include "TestFramework.h"

#include <cellui/CellRasterizer.h>

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
