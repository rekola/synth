#include "TestFramework.h"

#include "../src/ui/SpectrumImage.h"

namespace {

struct Rgb { int r, g, b, a; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return { int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff), int(p >> 24) };
}

}  // namespace

TEST(spectrum_image_has_the_requested_size_and_is_opaque) {
  StyleProvider styles;
  std::vector<float> levels(40, 0.5f);
  auto img = spectrum_image::render(levels, 40, 24, styles);
  CHECK(img.size() == 40u * 24u);
  for (uint32_t p : img) CHECK((p >> 24) == 0xff);
}

TEST(spectrum_image_silence_is_background_plus_a_floor_line) {
  StyleProvider styles;
  std::vector<float> levels(32, 0.0f);
  auto img = spectrum_image::render(levels, 32, 20, styles);
  Rgb top = at(img, 32, 16, 0);
  CHECK(top.r == styles.window_bg_color.getRed() && top.g == styles.window_bg_color.getGreen() && top.b == styles.window_bg_color.getBlue());
  Rgb bottom = at(img, 32, 16, 19);
  CHECK(bottom.r != top.r || bottom.g != top.g || bottom.b != top.b);
}

TEST(spectrum_image_a_full_level_column_reaches_the_top_and_leaves_others_low) {
  StyleProvider styles;
  std::vector<float> levels(33, 0.0f);
  levels[16] = 1.0f;
  auto img = spectrum_image::render(levels, 33, 30, styles);
  Rgb bg{styles.window_bg_color.getRed(), styles.window_bg_color.getGreen(), styles.window_bg_color.getBlue(), 255};
  auto differs = [&](Rgb p) { return p.r != bg.r || p.g != bg.g || p.b != bg.b; };
  CHECK(differs(at(img, 33, 16, 1)));
  CHECK(!differs(at(img, 33, 2, 5)));
  CHECK(!differs(at(img, 33, 30, 5)));
}

TEST(spectrum_image_wrong_level_count_gives_a_blank_image) {
  StyleProvider styles;
  auto img = spectrum_image::render(std::vector<float>(5, 1.0f), 10, 10, styles);
  CHECK(img.size() == 100u);
  for (uint32_t p : img) CHECK(p == 0u);
}
