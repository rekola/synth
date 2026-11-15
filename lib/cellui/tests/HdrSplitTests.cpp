#include "TestFramework.h"

#include <cellui/Color.h>
#include <cellui/HdrSplit.h>
#include <cellui/Image.h>

using namespace cellui;

namespace {

std::vector<float> picture(std::initializer_list<std::array<float, 4>> pixels) {
  std::vector<float> out;
  for (const auto & p : pixels) out.insert(out.end(), p.begin(), p.end());
  return out;
}

}  // namespace

TEST(hdr_split_a_picture_inside_white_is_its_8_bit_self_with_no_glow) {
  auto rgba = picture({{0.2f, 0.3f, 0.4f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.5f, 0.1f, 0.0f, 1.0f}});
  auto layers = splitHdr(rgba, 2, 2);
  CHECK(layers.base.size() == 4u);
  CHECK(layers.glow.empty());
  for (size_t i = 0; i < 4; i++) {
    auto expected = Color::linear(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]).toSrgb8();
    uint32_t p = layers.base[i];
    CHECK(Image::red(p) == expected[0] && Image::green(p) == expected[1] && Image::blue(p) == expected[2] && Image::alpha(p) == 255);
  }
}

TEST(hdr_split_light_past_white_leaves_a_glow_only_where_it_is) {
  auto rgba = picture({{0.2f, 0.2f, 0.2f, 1.0f}, {6.0f, 0.1f, 0.1f, 1.0f}});
  auto layers = splitHdr(rgba, 2, 1);
  CHECK(layers.base.size() == 2u && layers.glow.size() == 2u);
  // The base shows it as bright as the display goes, washed toward white, never past it.
  uint32_t hot = layers.base[1];
  CHECK(Image::red(hot) == 255 && Image::green(hot) > Image::blue(hot) - 1 && Image::green(hot) > 30);
  // The glow is the light's own colour at full strength with how much of it there is as the alpha; none beside it,
  // where it is transparent.
  uint32_t glow = layers.glow[1];
  CHECK(Image::red(glow) == 255 && Image::red(glow) > Image::green(glow) && Image::red(glow) > Image::blue(glow));
  CHECK(Image::alpha(glow) > 100);
  CHECK(layers.glow[0] == Image::pack(0, 0, 0, 0));
}

TEST(hdr_split_more_light_is_more_glow_and_the_base_stays_in_range) {
  auto rgba = picture({{1.5f, 1.5f, 1.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 1.0f}, {5.0f, 5.0f, 5.0f, 1.0f}});
  auto layers = splitHdr(rgba, 3, 1);
  CHECK(layers.glow.size() == 3u);
  CHECK(Image::alpha(layers.glow[0]) < Image::alpha(layers.glow[1]) && Image::alpha(layers.glow[1]) < Image::alpha(layers.glow[2]));
  // Light with no hue of its own is white at any strength: more of it is only more alpha.
  for (uint32_t g : layers.glow) CHECK(Image::red(g) == 255 && Image::green(g) == 255 && Image::blue(g) == 255);
  for (uint32_t p : layers.base) CHECK(Image::red(p) == 255 && Image::green(p) == 255);
}

TEST(hdr_split_the_glow_keeps_the_hue_of_the_light_past_white_at_any_strength) {
  // The same colour at two strengths: the same hue, the stronger the more alpha.
  auto rgba = picture({{3.0f, 0.3f, 0.1f, 1.0f}, {9.0f, 0.9f, 0.3f, 1.0f}});
  auto layers = splitHdr(rgba, 2, 1);
  CHECK(layers.glow.size() == 2u);
  uint32_t a = layers.glow[0], b = layers.glow[1];
  CHECK(std::abs(int(Image::red(a)) - int(Image::red(b))) <= 3 && std::abs(int(Image::green(a)) - int(Image::green(b))) <= 40);
  CHECK(Image::alpha(a) < Image::alpha(b));
}

TEST(hdr_split_equal_neighbours_give_equal_pixels) {
  std::vector<float> rgba;
  for (int i = 0; i < 40; i++) rgba.insert(rgba.end(), {i < 20 ? 0.1f : 5.0f, 0.2f, 0.3f, 1.0f});
  auto layers = splitHdr(rgba, 40, 1);
  for (int i = 1; i < 20; i++) CHECK(layers.base[static_cast<size_t>(i)] == layers.base[0] && layers.glow[static_cast<size_t>(i)] == layers.glow[0]);
  for (int i = 21; i < 40; i++) CHECK(layers.base[static_cast<size_t>(i)] == layers.base[20] && layers.glow[static_cast<size_t>(i)] == layers.glow[20]);
  CHECK(layers.glow[0] != layers.glow[20]);
}

TEST(hdr_split_bad_sizes_give_nothing) {
  CHECK(splitHdr({}, 0, 0).base.empty());
  CHECK(splitHdr(std::vector<float>(7, 1.0f), 2, 1).base.empty());
}
