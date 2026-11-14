#include "TestFramework.h"

#include <cellui/LineImage.h>

#include <cstdio>

using namespace cellui;

namespace {

struct Rgb { int r, g, b, a; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return { int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff), int(p >> 24) };
}

}  // namespace

TEST(line_image_has_the_requested_size_and_is_opaque) {
  StyleProvider styles;
  std::vector<float> levels(40, 0.5f);
  auto img = line_image::render(levels, 40, 24, styles);
  CHECK(img.size() == 40u * 24u);
  for (uint32_t p : img) CHECK((p >> 24) == 0xff);
}

TEST(line_image_silence_is_background_plus_a_floor_line) {
  StyleProvider styles;
  std::vector<float> levels(32, 0.0f);
  auto img = line_image::render(levels, 32, 20, styles);
  Rgb top = at(img, 32, 16, 0);
  CHECK(top.r == styles.window_bg_color.getRed() && top.g == styles.window_bg_color.getGreen() && top.b == styles.window_bg_color.getBlue());
  Rgb bottom = at(img, 32, 16, 19);
  CHECK(bottom.r != top.r || bottom.g != top.g || bottom.b != top.b);
}

TEST(line_image_a_full_level_column_reaches_the_top_and_leaves_others_low) {
  StyleProvider styles;
  std::vector<float> levels(33, 0.0f);
  levels[16] = 1.0f;
  auto img = line_image::render(levels, 33, 30, styles);
  Rgb bg{styles.window_bg_color.getRed(), styles.window_bg_color.getGreen(), styles.window_bg_color.getBlue(), 255};
  auto differs = [&](Rgb p) { return p.r != bg.r || p.g != bg.g || p.b != bg.b; };
  CHECK(differs(at(img, 33, 16, 1)));
  CHECK(!differs(at(img, 33, 2, 5)));
  CHECK(!differs(at(img, 33, 30, 5)));
}

TEST(line_image_wrong_level_count_gives_a_blank_image) {
  StyleProvider styles;
  auto img = line_image::render(std::vector<float>(5, 1.0f), 10, 10, styles);
  CHECK(img.size() == 100u);
  for (uint32_t p : img) CHECK(p == 0u);
}

TEST(line_image_curve_widens_with_the_display_scale) {
  StyleProvider styles;
  std::vector<float> levels(20, 0.5f);
  auto covered = [&](float scale) {
    auto img = line_image::render(levels, 20, 40, styles, scale);
    Rgb bg = at(img, 20, 10, 0);
    int n = 0;
    for (unsigned y = 0; y < 40; y++) {
      Rgb p = at(img, 20, 10, y);
      if (p.r != bg.r || p.g != bg.g || p.b != bg.b) n++;
    }
    return n;
  };
  CHECK(covered(2.0f) > covered(1.0f));
}

TEST(line_image_fractional_scale_fills_the_exact_size) {
  StyleProvider styles;
  std::vector<float> levels(101, 0.5f);
  auto img = line_image::render(levels, 101, 37, styles, 1.25f);
  CHECK(img.size() == 101u * 37u);
  bool opaque = true;
  for (uint32_t p : img) opaque = opaque && (p >> 24) == 0xffu;
  CHECK(opaque);
  // The curve reaches the last column.
  Rgb bg = at(img, 101, 100, 0);
  bool drawn = false;
  for (unsigned y = 0; y < 37; y++) {
    Rgb p = at(img, 101, 100, y);
    drawn = drawn || p.r != bg.r || p.g != bg.g || p.b != bg.b;
  }
  CHECK(drawn);
}

TEST(line_image_float_picture_keeps_the_background_as_given) {
  StyleProvider styles;
  std::vector<float> levels(40, 0.5f);
  auto pic = line_image::renderPicture(levels, 40, 24, styles, 1.0f, true);
  CHECK(pic.width == 40 && pic.height == 24);
  if (!pic.isHdr()) {
    // A canvas backend with no float surfaces gives the 8-bit picture of the same colours.
    std::printf("note: this canvas backend has no float surfaces; the 8-bit fallback was checked\n");
    CHECK(pic.pixels.size() == 40u * 24u);
    return;
  }
  CHECK(pic.pixels.empty() && pic.hdr.size() == 40u * 24u * 4u);
  // The top-left pixel is the window background, in linear Rec.2020 as the style holds it.
  const Color & bg = styles.window_bg_color;
  CHECK_NEAR(pic.hdr[0], bg.r(), 1e-4f);
  CHECK_NEAR(pic.hdr[1], bg.g(), 1e-4f);
  CHECK_NEAR(pic.hdr[2], bg.b(), 1e-4f);
  CHECK_NEAR(pic.hdr[3], 1.0f, 1e-6f);
  // Opaque everywhere.
  for (size_t i = 3; i < pic.hdr.size(); i += 4) CHECK_NEAR(pic.hdr[i], 1.0f, 1e-5f);
}

TEST(line_image_float_and_8_bit_pictures_agree_on_the_background) {
  StyleProvider styles;
  std::vector<float> levels(40, 0.5f);
  auto flat = line_image::renderPicture(levels, 40, 24, styles, 1.0f, false);
  CHECK(!flat.isHdr() && flat.pixels.size() == 40u * 24u);
  auto pic = line_image::renderPicture(levels, 40, 24, styles, 1.0f, true);
  if (!pic.isHdr()) return;
  // What the 8-bit canvas shows of a colour is what toSrgb8() makes of the float one.
  auto expected = Color::linear(pic.hdr[0], pic.hdr[1], pic.hdr[2]).toSrgb8();
  Rgb p = at(flat.pixels, 40, 0, 0);
  CHECK(std::abs(p.r - expected[0]) <= 1 && std::abs(p.g - expected[1]) <= 1 && std::abs(p.b - expected[2]) <= 1);
}

TEST(line_image_picture_with_a_wrong_level_count_is_blank_8_bit) {
  StyleProvider styles;
  auto pic = line_image::renderPicture(std::vector<float>(5, 1.0f), 10, 10, styles, 1.0f, true);
  CHECK(!pic.isHdr() && pic.pixels.size() == 100u);
  for (uint32_t p : pic.pixels) CHECK(p == 0u);
}
