#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/term/BackdropEmulation.h>

using namespace cellui;

namespace {

// A grey level dimmed toward black in linear light, once and twice.
int dimmed(int v, float dim) { return Color(v, v, v).mix(Color(0, 0, 0), dim).getRed(); }
int dimmedTwice(int v, float dim) { return Color(v, v, v).mix(Color(0, 0, 0), dim).mix(Color(0, 0, 0), dim).getRed(); }

// A plane over the whole screen made of nothing but backdrop cells.
std::unique_ptr<Plane> scrimOver(BufferPlane & root, const Backdrop & backdrop) {
  auto scrim = root.createChild();
  auto [rows, cols] = root.getDim();
  scrim->setBackdrop(backdrop);
  for (int r = 0; r < rows; r++) scrim->putstr(r, 0, std::string(static_cast<size_t>(cols), ' '));
  return scrim;
}

// What a terminal shows of a plane tree: `b`, its target, is composed and
// then has its layers' backdrops applied; `images` are the pictures as shown.
void show(BufferPlane & root, CellBuffer & b, term::BackdropEmulation & emulation, const DisplayInfo & display,
          std::vector<ImagePlacement> & images) {
  root.refresh();
  auto layers = root.takeBackdropLayers();
  images = root.images();
  emulation.apply(b, images, layers, display);
}

// Draws every cell in one colour, and counts what it was asked for.
class CountingRasterizer : public CellRasterizer {
 public:
  Image render(const CellBuffer &, const std::vector<ImagePlacement> &, int, int, int rows, int cols, int cell_h, int cell_w,
               int scale, Color, Color) override {
    calls++;
    Image out;
    out.width = std::max(1, cols * cell_w / scale);
    out.height = std::max(1, rows * cell_h / scale);
    out.pixels.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height), Image::pack(90, 90, 90));
    return out;
  }
  int calls = 0;
};

}  // namespace

TEST(backdropemulation_without_layers_leaves_the_frame_alone) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "ab");
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText() == "ab\n");
  CHECK(!emulation.holdsState());
}

TEST(backdropemulation_dims_toward_its_tint_and_keeps_the_text) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  root.setFgColor(200, 200, 200);
  root.setBgColor(100, 100, 100);
  root.putstr(0, 0, "ab");
  auto scrim = scrimOver(root, Backdrop{0, 0.5f, Color(0, 0, 0)});
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText() == "ab\n");
  CHECK(b.at(0, 0).fg->getRed() == dimmed(200, 0.5f));
  CHECK(b.at(0, 0).bg->getRed() == dimmed(100, 0.5f));
  CHECK(!b.at(0, 0).backdrop.active());
}

TEST(backdropemulation_blur_spreads_colour_and_leaves_no_glyph) {
  StyleProvider styles;
  CellBuffer b(3, 9);
  BufferPlane root(b, styles);
  DisplayInfo display;
  display.default_fg = Color(255, 255, 255);
  display.default_bg = Color(0, 0, 0);
  root.setFgColor(255, 255, 255);
  root.setBgColor(0, 0, 0);
  for (int r = 0; r < 3; r++) root.putstr(r, 0, "         ");
  root.setBgColor(240, 0, 0);
  root.putstr(1, 4, "x");
  auto scrim = scrimOver(root, Backdrop{1});
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, display, images);
  // The red cell bleeds into its neighbours and is no longer pure red.
  CHECK(b.at(1, 3).bg->getRed() > 0);
  CHECK(b.at(1, 4).bg->getRed() < 240);
  // Only colour is left, nothing to read or select.
  CHECK(b.at(1, 4).text == " ");
  CHECK(b.toPlainText().empty());
  // Far enough away, nothing reaches.
  CHECK(b.at(1, 0).bg->getRed() == 0);
}

TEST(backdropemulation_blur_leaves_no_glyph_wide_ones_included) {
  StyleProvider styles;
  CellBuffer b(2, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "hi");
  root.putstr(1, 0, "◆漢");
  auto scrim = scrimOver(root, Backdrop{1});
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText().empty());
  for (int c = 0; c < 6; c++) CHECK(!b.at(1, c).continuation);
}

TEST(backdropemulation_dim_only_backdrop_keeps_the_glyphs) {
  StyleProvider styles;
  CellBuffer b(1, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "hello");
  auto scrim = scrimOver(root, Backdrop{0, 0.5f});
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText() == "hello\n");
}

TEST(backdropemulation_text_over_a_backdrop_is_sharp_and_planes_above_are_untouched) {
  StyleProvider styles;
  CellBuffer b(1, 8);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "behind");
  auto scrim = scrimOver(root, Backdrop{1, 0.3f});
  scrim->setFgColor(1, 2, 3);
  scrim->putstr(0, 0, "on");
  auto dialog = root.createChild();
  dialog->resize(1, 2);
  dialog->move(0, 6);
  dialog->setFgColor(9, 9, 9);
  dialog->putstr(0, 0, "ok");
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText() == "on    ok\n");
  CHECK(b.at(0, 0).fg->getBlue() == 3);
  CHECK(b.at(0, 6).fg->getRed() == 9);
}

TEST(backdropemulation_image_under_a_backdrop_is_filtered_once) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  DisplayInfo display;
  display.cell_px_height = 4;
  display.cell_px_width = 2;
  // Black with one white line across the middle.
  std::vector<uint32_t> pixels(8 * 8, Image::pack(0, 0, 0));
  for (size_t x = 0; x < 8; x++) pixels[4 * 8 + x] = Image::pack(255, 255, 255);
  root.drawImageAt(0, 0, pixels, 8, 8);
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, display, images);
  uint64_t sharp = images[0].image->version;

  auto scrim = scrimOver(root, Backdrop{1});
  show(root, b, emulation, display, images);
  auto blurred = images[0].image;
  CHECK(blurred->version != sharp);
  CHECK(Image::red(blurred->pixels[2 * 8]) > 0);    // the line spread upward
  CHECK(Image::red(blurred->pixels[4 * 8]) < 255);  // and lost its peak
  // The same frame again is the same filtered picture: nothing to resend.
  show(root, b, emulation, display, images);
  CHECK(images[0].image->version == blurred->version);

  scrim.reset();
  show(root, b, emulation, display, images);
  CHECK(images[0].image->version == sharp);
  CHECK(!emulation.holdsState());
}

TEST(backdropemulation_pictures_redraw_only_tiles_whose_source_changed) {
  StyleProvider styles;
  CellBuffer b(8, 48);  // two rows of three tiles
  BufferPlane root(b, styles);
  root.setCellPixelSize(8, 4);
  DisplayInfo display;
  display.pictures = true;
  display.cell_px_height = 8;
  display.cell_px_width = 4;
  term::BackdropEmulation emulation(std::make_shared<CountingRasterizer>());
  for (int r = 0; r < 8; r++) root.putstr(r, 0, std::string(48, 'x'));
  auto scrim = scrimOver(root, Backdrop{1});
  auto dialog = root.createChild();
  dialog->resize(1, 2);
  dialog->move(3, 20);
  dialog->putstr(0, 0, "ok");

  std::vector<ImagePlacement> images;
  show(root, b, emulation, display, images);
  CHECK(emulation.redrawnTiles() == 6);
  CHECK(images.size() == 6);
  // Every tile is a picture stretched over its cells.
  CHECK(images[0].image->cols == 16 && images[0].image->rows == 4);
  // With a pad of its neighbours for the terminal to blend its edges with.
  CHECK(images[0].image->pad == 1);
  CHECK(images[0].image->width == 16 * 4 + 2 && images[0].image->height == 4 * 8 + 2);
  CHECK(b.at(0, 0).image > 0);
  // The dialog over it stays text.
  CHECK(b.at(3, 20).text == "o" && b.at(3, 20).image == 0);

  std::vector<uint64_t> versions;
  for (auto & p : images) versions.push_back(p.image->version);
  show(root, b, emulation, display, images);
  CHECK(emulation.redrawnTiles() == 0);
  for (size_t i = 0; i < versions.size(); i++) CHECK(images[i].image->version == versions[i]);

  // A change in the top-left corner reaches only the tiles within the blur.
  root.putstr(0, 0, "y");
  show(root, b, emulation, display, images);
  CHECK(emulation.redrawnTiles() == 1);
  // One near a tile's corner reaches its neighbours too.
  root.putstr(4, 16, "y");
  show(root, b, emulation, display, images);
  CHECK(emulation.redrawnTiles() == 4);

  scrim.reset();
  show(root, b, emulation, display, images);
  CHECK(images.empty());
  CHECK(b.at(0, 0).text == "y");
}

// Backdrops one above the other: each dims what is composed below it, the
// layers between included.
TEST(backdropemulation_a_layer_between_two_backdrops_is_dimmed_by_the_upper_one) {
  StyleProvider styles;
  CellBuffer b(1, 12);
  BufferPlane root(b, styles);
  root.setFgColor(200, 200, 200);
  root.putstr(0, 0, "bottom");
  auto first = scrimOver(root, Backdrop{0, 0.5f, Color(0, 0, 0)});
  auto middle = root.createChild();
  middle->resize(1, 3);
  middle->move(0, 8);
  middle->setFgColor(200, 200, 200);
  middle->putstr(0, 0, "mid");
  auto second = scrimOver(root, Backdrop{0, 0.5f, Color(0, 0, 0)});
  term::BackdropEmulation emulation;
  std::vector<ImagePlacement> images;
  show(root, b, emulation, {}, images);
  CHECK(b.toPlainText() == "bottom  mid\n");
  CHECK(b.at(0, 0).fg->getRed() == dimmedTwice(200, 0.5f));  // dimmed by both
  CHECK(b.at(0, 8).fg->getRed() == dimmed(200, 0.5f));  // by the upper one only
}
