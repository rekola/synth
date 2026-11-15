#include "TestFramework.h"

#include <cellui/HeatmapImage.h>

using namespace cellui;

namespace {

struct Px { int r, g, b, a; };

Px at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return { int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff), int(p >> 24) };
}

bool isBackground(Px p, const StyleProvider & s) {
  return std::abs(p.r - s.window_bg_color.getRed()) <= 1 && std::abs(p.g - s.window_bg_color.getGreen()) <= 1 && std::abs(p.b - s.window_bg_color.getBlue()) <= 1;
}

}  // namespace

TEST(heatmap_image_empty_grid_is_all_background_and_opaque) {
  StyleProvider styles;
  std::vector<float> zeros(8 * 4, 0.0f);
  auto img = heatmap_image::render(zeros, zeros, 8, 4, {}, {}, 64, 32, styles, 0.0f);
  CHECK(img.size() == 64u * 32u);
  for (size_t i = 0; i < img.size(); i++) {
    CHECK((img[i] >> 24) == 0xff);
    CHECK(isBackground(at(img, 64, i % 64, i / 64), styles));
  }
}

TEST(heatmap_image_row_zero_of_the_grid_is_the_bottom_of_the_image) {
  StyleProvider styles;
  std::vector<float> b(8 * 4, 0.0f), s(8 * 4, 1.0f);
  for (size_t x = 0; x < 8; x++) b[x] = 1.0f; // grid row 0 fully lit
  auto img = heatmap_image::render(b, s, 8, 4, {}, {}, 80, 40, styles, 0.0f);
  CHECK(!isBackground(at(img, 80, 40, 39), styles));
  CHECK(isBackground(at(img, 80, 40, 0), styles));
}

TEST(heatmap_image_lit_cell_is_lit_at_its_centre_and_not_far_away) {
  StyleProvider styles;
  std::vector<float> b(16 * 8, 0.0f), s(16 * 8, 1.0f);
  b[3u * 16u + 4u] = 1.0f; // column 4, row 3 (from the bottom)
  auto img = heatmap_image::render(b, s, 16, 8, {}, {}, 160, 80, styles, 0.0f);
  // Column 4 spans x 40..50; row 3 from the bottom spans y 40..50 from the top.
  CHECK(!isBackground(at(img, 160, 45, 45), styles));
  CHECK(isBackground(at(img, 160, 120, 10), styles));
}

TEST(heatmap_image_edge_cells_keep_their_strength) {
  StyleProvider styles;
  std::vector<float> b(8 * 4, 1.0f), s(8 * 4, 1.0f);
  auto img = heatmap_image::render(b, s, 8, 4, {}, {}, 80, 40, styles, 0.0f);
  Px corner = at(img, 80, 0, 0), middle = at(img, 80, 40, 20);
  CHECK(std::abs(corner.r - middle.r) <= 2 && std::abs(corner.g - middle.g) <= 2 && std::abs(corner.b - middle.b) <= 2);
}

TEST(heatmap_image_marker_is_a_text_coloured_square_and_v_points_up) {
  StyleProvider styles;
  std::vector<float> z(8 * 4, 0.0f);
  auto img = heatmap_image::render(z, z, 8, 4, { { 0.5f, 0.75f } }, {}, 80, 40, styles, 0.0f);
  // u = 0.5 -> x 40; v = 0.75 -> 30 px up -> y = 40 - 1 - 30 = 9.
  Px p = at(img, 80, 40, 9);
  auto ink = styles.window_fg_color.toSrgb8();
  CHECK(p.r == ink[0] && p.g == ink[1] && p.b == ink[2]);
  CHECK(isBackground(at(img, 80, 40, 20), styles));
}

TEST(heatmap_image_mismatched_sizes_give_a_blank_image) {
  StyleProvider styles;
  auto img = heatmap_image::render(std::vector<float>(3, 1.0f), std::vector<float>(3, 1.0f), 8, 4, {}, {}, 10, 10, styles, 0.0f);
  CHECK(img.size() == 100u);
  for (uint32_t p : img) CHECK(p == 0u);
}

TEST(heatmap_image_a_fully_lit_cell_is_the_heatmap_hue_lit_past_white_and_mapped) {
  StyleProvider styles;
  std::vector<float> b(4 * 4, 1.0f), s(4 * 4, 1.0f);
  auto img = heatmap_image::render(b, s, 4, 4, {}, {}, 40, 40, styles, 0.0f);
  uint8_t r, g, bl;
  heatmapHsvToRgb(kHeatmapHue, 1.0f, 1.0f, r, g, bl);
  Px p = at(img, 40, 20, 20);
  // What an 8-bit picture shows of the lit colour: the hue washed toward white, never darker or another hue.
  auto expected = heatmap_image::litCellColor(styles.window_bg_color, 1.0f, 1.0f).toSrgb8();
  CHECK(std::abs(p.r - expected[0]) <= 1 && std::abs(p.g - expected[1]) <= 1 && std::abs(p.b - expected[2]) <= 1);
  CHECK(p.r >= r - 1 && p.g >= g - 1 && p.b >= bl - 1);
  CHECK(p.r > p.b); // orange, not blue
}

TEST(heatmap_image_a_cell_under_the_lit_range_is_the_plain_blend) {
  StyleProvider styles;
  std::vector<float> b(4 * 4, 0.5f), s(4 * 4, 1.0f);
  auto img = heatmap_image::render(b, s, 4, 4, {}, {}, 40, 40, styles, 0.0f);
  float r, g, bl;
  heatmap_image::cellColor(styles.window_bg_color, 1.0f, 0.5f, r, g, bl);
  Px p = at(img, 40, 20, 20);
  CHECK(std::abs(p.r - static_cast<int>(r + 0.5f)) <= 1 && std::abs(p.g - static_cast<int>(g + 0.5f)) <= 1 && std::abs(p.b - static_cast<int>(bl + 0.5f)) <= 1);
}

TEST(heatmap_image_float_picture_is_the_same_grid_with_hot_cells_past_white) {
  StyleProvider styles;
  // Two columns: a middling cell and a fully lit one, the same all the way up.
  std::vector<float> b(2 * 2), s(2 * 2, 1.0f);
  for (int row = 0; row < 2; row++) { b[static_cast<size_t>(row) * 2] = 0.5f; b[static_cast<size_t>(row) * 2 + 1] = 1.0f; }
  auto pic = heatmap_image::renderPicture(b, s, 2, 2, {}, {}, 20, 10, styles, 0.0f, true);
  CHECK(pic.isHdr() && pic.pixels.empty() && pic.hdr.size() == 20u * 10u * 4u);
  auto peak = [&](unsigned x, unsigned y) { const float * p = &pic.hdr[(y * 20 + x) * 4]; return std::max({p[0], p[1], p[2]}); };
  CHECK(peak(5, 5) <= 1.0f);   // under the lit range
  CHECK(peak(15, 5) > 2.0f);   // lit past white
  // Blocks: a cell is one colour across its pixels, with a sharp edge between cells.
  const float * a = &pic.hdr[(2 * 20 + 1) * 4];
  const float * a2 = &pic.hdr[(2 * 20 + 8) * 4];
  CHECK(a[0] == a2[0] && a[1] == a2[1] && a[2] == a2[2]);
  const float * edge = &pic.hdr[(2 * 20 + 10) * 4];
  CHECK(edge[0] != a[0]);
  CHECK(pic.hdr[3] == 1.0f && pic.hdr[pic.hdr.size() - 1] == 1.0f);
}

TEST(heatmap_image_float_and_8_bit_pictures_agree_on_where_things_are) {
  StyleProvider styles;
  std::vector<float> b(8 * 4, 0.0f), s(8 * 4, 1.0f);
  b[3u * 8u + 2u] = 0.5f;
  auto flat = heatmap_image::renderPicture(b, s, 8, 4, {}, {}, 80, 40, styles, 0.0f, false);
  auto pic = heatmap_image::renderPicture(b, s, 8, 4, {}, {}, 80, 40, styles, 0.0f, true);
  CHECK(!flat.isHdr() && pic.isHdr());
  // Every pixel of the float picture, mapped to sRGB, is the 8-bit pixel (to a level).
  size_t wrong = 0;
  for (size_t i = 0; i < 80u * 40u; i++) {
    auto rgb = Color::linear(pic.hdr[i * 4], pic.hdr[i * 4 + 1], pic.hdr[i * 4 + 2]).toSrgb8();
    uint32_t p = flat.pixels[i];
    if (std::abs(int(p & 0xff) - rgb[0]) > 1 || std::abs(int((p >> 8) & 0xff) - rgb[1]) > 1 || std::abs(int((p >> 16) & 0xff) - rgb[2]) > 1) wrong++;
  }
  CHECK(wrong == 0u);
}

TEST(heatmap_image_float_markers_and_labels_are_drawn_like_the_8_bit_ones) {
  StyleProvider styles;
  std::vector<float> z(8 * 4, 0.0f);
  std::vector<HeatmapChart::Marker> markers{{0.5f, 0.5f}};
  std::vector<HeatmapChart::Label> labels{{0.5f, 0.9f, "+90", 0.5f, 0.5f}};
  auto flat = heatmap_image::renderPicture(z, z, 8, 4, markers, labels, 160, 80, styles, 12.0f, false);
  auto pic = heatmap_image::renderPicture(z, z, 8, 4, markers, labels, 160, 80, styles, 12.0f, true);
  CHECK(pic.isHdr());
  size_t wrong = 0;
  for (size_t i = 0; i < 160u * 80u; i++) {
    auto rgb = Color::linear(pic.hdr[i * 4], pic.hdr[i * 4 + 1], pic.hdr[i * 4 + 2]).toSrgb8();
    uint32_t p = flat.pixels[i];
    if (std::abs(int(p & 0xff) - rgb[0]) > 2 || std::abs(int((p >> 8) & 0xff) - rgb[1]) > 2 || std::abs(int((p >> 16) & 0xff) - rgb[2]) > 2) wrong++;
  }
  CHECK(wrong == 0u);
}

TEST(heatmap_image_label_is_drawn_over_the_picture_where_it_is_put) {
  StyleProvider styles;
  std::vector<float> z(8 * 4, 0.0f);
  auto none = heatmap_image::render(z, z, 8, 4, {}, {}, 160, 80, styles, 12.0f);
  // Hung from the top edge, centred, and the bottom corner left alone.
  auto img = heatmap_image::render(z, z, 8, 4, {}, { { 0.5f, 1.0f, "+90", 0.5f, 1.0f } }, 160, 80, styles, 12.0f);
  int changed_top = 0, changed_bottom = 0;
  for (unsigned y = 0; y < 80; y++) {
    for (unsigned x = 0; x < 160; x++) {
      if (img[y * 160 + x] == none[y * 160 + x]) continue;
      CHECK(x >= 60 && x < 100);
      (y < 16 ? changed_top : changed_bottom)++;
    }
  }
  CHECK(changed_top > 0);
  CHECK(changed_bottom == 0);
}

TEST(heatmap_image_no_labels_or_no_label_height_leaves_the_picture_alone) {
  StyleProvider styles;
  std::vector<float> z(8 * 4, 0.0f);
  auto none = heatmap_image::render(z, z, 8, 4, {}, {}, 80, 40, styles, 12.0f);
  CHECK(heatmap_image::render(z, z, 8, 4, {}, { { 0.0f, 0.5f, "-180" } }, 80, 40, styles, 0.0f) == none);
}
