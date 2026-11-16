#pragma once

#include <cellui/HeatmapChart.h>
#include <cellui/Picture.h>
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

// A hot cell is lit past white: from this value on its colour is multiplied by up to 1 + kHdrGain, so on a
// display that can show it the strongest part of the field glows, and on one that cannot it is the same colour
// washed toward white (Color::toSrgb8()).
constexpr float kHdrStart = 0.7f, kHdrGain = 3.0f;

// The colour of one cell in linear Rec.2020, as a Color: cellColor()'s, lit as above. Below kHdrStart it is
// cellColor()'s colour exactly.
inline Color litCellColor(const Color & background, float saturation, float value) {
  float r, g, b;
  cellColor(background, saturation, value, r, g, b);
  Color c = Color::srgb(std::min(r, 255.0f) / 255.0f, std::min(g, 255.0f) / 255.0f, std::min(b, 255.0f) / 255.0f);
  return c * StyleProvider::litGain(value, kHdrStart, kHdrGain);
}

// How much of a label's white shows over the heatmap (0 to 1).
constexpr float kLabelAlpha = 0.55f;

// The heatmap as pixels: `brightness`/`saturation` are cols*rows entries,
// row 0 at the bottom (HeatmapChart::setGrid()), smoothly scaled up to
// width x height, a 3x3 white square on each marker (u, v in 0..1, v up),
// then each label's text drawn with canvas in a font `label_px` tall, in white
// at kLabelAlpha so the heatmap shows through it.
// Returns width*height pixels, row 0 at the top, one uint32_t per pixel with
// bytes R, G, B, A in memory order (all opaque).
std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, const std::vector<HeatmapChart::Label> & labels,
                             unsigned width, unsigned height, const StyleProvider & styles, float label_px);

// The same as a Picture. With `prefer_float` the grid is enlarged into floats (linear Rec.2020: each cell a block
// of one colour, hot cells lit past white) with the markers and labels over it; no canvas backend with float
// surfaces is needed, since only the labels' text uses canvas. Otherwise it is render()'s 8-bit picture.
Picture renderPicture(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                      const std::vector<HeatmapChart::Marker> & markers, const std::vector<HeatmapChart::Label> & labels, unsigned width,
                      unsigned height, const StyleProvider & styles, float label_px, bool prefer_float);

}  // namespace heatmap_image

}  // namespace cellui

