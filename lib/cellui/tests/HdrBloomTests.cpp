#include "TestFramework.h"

#include <cellui/HdrBloom.h>

using namespace cellui;

namespace {
Image lamp(int w, int h, float level) {
  Image img;
  img.width = w;
  img.height = h;
  const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
  img.hdr.assign(pixels * 4, 0.0f);
  for (size_t i = 0; i < pixels; i++) img.hdr[i * 4 + 3] = 1.0f;
  size_t mid = static_cast<size_t>(h / 2) * static_cast<size_t>(w) + static_cast<size_t>(w / 2);
  img.hdr[mid * 4] = img.hdr[mid * 4 + 1] = img.hdr[mid * 4 + 2] = level;
  return img;
}
}  // namespace

TEST(flatten_hdr_8bit_picture_is_unchanged) {
  Image img;
  img.width = 1;
  img.height = 1;
  img.pixels = {Image::pack(1, 2, 3)};
  CHECK(flattenHdr(img, 20).pixels == img.pixels);
}

TEST(flatten_hdr_bright_pixel_blooms_into_neighbours) {
  Image bright = lamp(41, 41, 6.0f);
  Image out = flattenHdr(bright, 20);
  CHECK(out.hdr.empty() && out.pixels.size() == 41u * 41u);
  uint32_t next = out.pixels[20 * 41 + 22];
  CHECK(Image::red(next) > 0);  // lit by the glow, though the picture is black there
  CHECK(Image::red(out.pixels[20 * 41 + 20]) == 255);
  // a lamp at white and under has no glow
  Image plain = flattenHdr(lamp(41, 41, 1.0f), 20);
  CHECK(Image::red(plain.pixels[20 * 41 + 22]) == 0);
}

TEST(flatten_hdr_more_light_reaches_further) {
  auto reach = [](float level) {
    Image out = flattenHdr(lamp(81, 81, level), 20);
    int n = 0;
    for (size_t x = 40; x < 81; x++) n += Image::red(out.pixels[40u * 81u + x]) > 8;
    return n;
  };
  CHECK(reach(30.0f) > reach(3.0f));
}

TEST(flatten_hdr_incremental_equals_full_and_keeps_far_pixels) {
  Image a = lamp(120, 90, 6.0f);
  HdrFlattenState state;
  Image first = flattenHdr(a, 20, &state);
  Image b = a;  // a second lamp, far from the first
  size_t at = (10u * 120u + 100u) * 4;
  b.hdr[at] = b.hdr[at + 1] = b.hdr[at + 2] = 9.0f;
  b.version = 1;
  Image inc = flattenHdr(b, 20, &state);
  Image full = flattenHdr(b, 20);
  CHECK(inc.pixels == full.pixels);
  CHECK(inc.version == 1u);
  // far from the change the pixels are the earlier result's
  CHECK(inc.pixels[45u * 120u + 5u] == first.pixels[45u * 120u + 5u]);
  CHECK(first.pixels != inc.pixels);
}
