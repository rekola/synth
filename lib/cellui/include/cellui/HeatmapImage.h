#pragma once

#include <cellui/HeatmapChart.h>
#include <cellui/StyleProvider.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cellui {

namespace heatmap_image {

// The colour of one cell: the window background at value 0, the fully lit
// fixed-hue colour at value 1, a straight lerp between (so a value that only
// ever decays towards 0 settles on the background without a cutoff).
inline void cellColor(const Color & background, float saturation, float value, float & r, float & g, float & b) {
  value = std::clamp(value, 0.0f, 1.0f);
  uint8_t lr, lg, lb;
  heatmapHsvToRgb(kHeatmapHue, std::clamp(saturation, 0.0f, 1.0f), 1.0f, lr, lg, lb);
  auto bg = background.toSrgb8();
  r = static_cast<float>(bg[0]) * (1.0f - value) + static_cast<float>(lr) * value;
  g = static_cast<float>(bg[1]) * (1.0f - value) + static_cast<float>(lg) * value;
  b = static_cast<float>(bg[2]) * (1.0f - value) + static_cast<float>(lb) * value;
}

// The heatmap as pixels: `brightness`/`saturation` are cols*rows entries,
// row 0 at the bottom (HeatmapChart::setGrid()), smoothly scaled up to
// width x height, a 3x3 white square on each marker (u, v in 0..1, v up).
// Returns width*height pixels, row 0 at the top, one uint32_t per pixel with
// bytes R, G, B, A in memory order (all opaque).
std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, unsigned width, unsigned height, const StyleProvider & styles);

}  // namespace heatmap_image

}  // namespace cellui

