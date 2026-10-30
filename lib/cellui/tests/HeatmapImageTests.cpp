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
  auto img = heatmap_image::render(zeros, zeros, 8, 4, {}, 64, 32, styles);
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
  auto img = heatmap_image::render(b, s, 8, 4, {}, 80, 40, styles);
  CHECK(!isBackground(at(img, 80, 40, 39), styles));
  CHECK(isBackground(at(img, 80, 40, 0), styles));
}

TEST(heatmap_image_lit_cell_is_lit_at_its_centre_and_not_far_away) {
  StyleProvider styles;
  std::vector<float> b(16 * 8, 0.0f), s(16 * 8, 1.0f);
  b[3u * 16u + 4u] = 1.0f; // column 4, row 3 (from the bottom)
  auto img = heatmap_image::render(b, s, 16, 8, {}, 160, 80, styles);
  // Column 4 spans x 40..50; row 3 from the bottom spans y 40..50 from the top.
  CHECK(!isBackground(at(img, 160, 45, 45), styles));
  CHECK(isBackground(at(img, 160, 120, 10), styles));
}

TEST(heatmap_image_edge_cells_keep_their_strength) {
  StyleProvider styles;
  std::vector<float> b(8 * 4, 1.0f), s(8 * 4, 1.0f);
  auto img = heatmap_image::render(b, s, 8, 4, {}, 80, 40, styles);
  Px corner = at(img, 80, 0, 0), middle = at(img, 80, 40, 20);
  CHECK(std::abs(corner.r - middle.r) <= 2 && std::abs(corner.g - middle.g) <= 2 && std::abs(corner.b - middle.b) <= 2);
}

TEST(heatmap_image_marker_is_a_text_coloured_square_and_v_points_up) {
  StyleProvider styles;
  std::vector<float> z(8 * 4, 0.0f);
  auto img = heatmap_image::render(z, z, 8, 4, { { 0.5f, 0.75f } }, 80, 40, styles);
  // u = 0.5 -> x 40; v = 0.75 -> 30 px up -> y = 40 - 1 - 30 = 9.
  Px p = at(img, 80, 40, 9);
  auto ink = styles.window_fg_color.toSrgb8();
  CHECK(p.r == ink[0] && p.g == ink[1] && p.b == ink[2]);
  CHECK(isBackground(at(img, 80, 40, 20), styles));
}

TEST(heatmap_image_mismatched_sizes_give_a_blank_image) {
  StyleProvider styles;
  auto img = heatmap_image::render(std::vector<float>(3, 1.0f), std::vector<float>(3, 1.0f), 8, 4, {}, 10, 10, styles);
  CHECK(img.size() == 100u);
  for (uint32_t p : img) CHECK(p == 0u);
}

TEST(heatmap_image_a_fully_lit_cell_has_the_heatmap_hue_in_the_right_channels) {
  StyleProvider styles;
  std::vector<float> b(4 * 4, 1.0f), s(4 * 4, 1.0f);
  auto img = heatmap_image::render(b, s, 4, 4, {}, 40, 40, styles);
  uint8_t r, g, bl;
  heatmapHsvToRgb(kHeatmapHue, 1.0f, 1.0f, r, g, bl);
  Px p = at(img, 40, 20, 20);
  CHECK(std::abs(p.r - r) <= 1 && std::abs(p.g - g) <= 1 && std::abs(p.b - bl) <= 1);
  CHECK(p.r > p.b); // orange, not blue
}
