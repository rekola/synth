#include <cellui/HdrSplit.h>

#include <cellui/Color.h>
#include <cellui/Image.h>

#include <algorithm>
#include <cmath>

namespace cellui {

HdrLayers splitHdr(const std::vector<float> & rgba, int width, int height) {
  HdrLayers out;
  if (width <= 0 || height <= 0) return out;
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (rgba.size() < count * 4) return out;
  out.base.resize(count);

  const uint32_t no_glow = Image::pack(0, 0, 0, 0);
  float last[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  uint32_t last_base = 0, last_glow = no_glow;
  for (size_t i = 0; i < count; i++) {
    const float * p = &rgba[i * 4];
    if (p[0] == last[0] && p[1] == last[1] && p[2] == last[2] && p[3] == last[3]) {
      out.base[i] = last_base;
      if (!out.glow.empty()) out.glow[i] = last_glow;
      continue;
    }
    std::copy(p, p + 4, last);
    Color color = Color::linear(std::max(p[0], 0.0f), std::max(p[1], 0.0f), std::max(p[2], 0.0f));
    auto rgb = color.toSrgb8();
    uint8_t alpha = static_cast<uint8_t>(std::lround(std::clamp(p[3], 0.0f, 1.0f) * 255.0f));
    last_base = Image::pack(rgb[0], rgb[1], rgb[2], alpha);
    last_glow = no_glow;
    auto v = color.linearSrgb();
    if (std::max({v[0], v[1], v[2]}) > 1.0f) {
      // The glow of a pixel: the colour of its light past white (each channel's excess, scaled so the
      // strongest is 1: its hue), with how much of it there is as the alpha. Kept apart, the display can
      // make the glow wider or stronger and the colour stays the pixel's own.
      float excess[3], strongest = 0.0f;
      for (size_t c = 0; c < 3; c++) {
        excess[c] = std::max(v[c] - 1.0f, 0.0f);
        strongest = std::max(strongest, excess[c]);
      }
      uint8_t amount = Color::linearToSrgb8(strongest / kHdrGlowRange);
      if (amount != 0) {
        last_glow = Image::pack(Color::linearToSrgb8(excess[0] / strongest), Color::linearToSrgb8(excess[1] / strongest),
                                Color::linearToSrgb8(excess[2] / strongest), amount);
        if (out.glow.empty()) out.glow.assign(count, no_glow);
      }
    }
    out.base[i] = last_base;
    if (!out.glow.empty()) out.glow[i] = last_glow;
  }
  return out;
}

}  // namespace cellui
