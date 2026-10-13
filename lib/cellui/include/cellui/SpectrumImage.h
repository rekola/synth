#pragma once

#include <cellui/StyleProvider.h>

#include <cstdint>
#include <vector>

namespace cellui {

namespace spectrum_image {

// The spectrum as pixels: `levels` holds one 0..1 level per pixel column
// (levels.size() == width), drawn as a curve with a faint fill below it,
// both shaded from the bottom up by StyleProvider::spectrumColor(), on the
// window background. Returns width*height pixels, row 0 at the top, one
// uint32_t per pixel with bytes R, G, B, A in memory order (all opaque).
// Drawn at `scale`, the display scale: the curve's width and inset are in
// logical units, so they grow with it.
std::vector<uint32_t> render(const std::vector<float> & levels, unsigned width, unsigned height, const StyleProvider & styles,
                             float scale = 1.0f);

}  // namespace spectrum_image

}  // namespace cellui

