#pragma once

#include <cellui/Color.h>
#include <cellui/StyleProvider.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace cellui {

// A sparkline's reading: the level and the held peak as 0..1 fractions of the
// meter (level_meter::fraction()), and whether the signal clips.
struct SparklineReading {
  float fraction = 0.0f;
  float peak = 0.0f;  // 0: no peak marker
  bool clipping = false;
};

// How a sparkline looks: what it is drawn on, and one colour for the bar in
// place of the meter colours by height (StyleProvider::meterColor()).
struct SparklineLook {
  Color background;
  std::optional<Color> color;
};

namespace sparkline_image {

// Vertical bars side by side across a width x height image, one per
// column slot (a cell's two dot columns are two slots), each filled from the
// bottom with a peak line above it; an empty slot stays background. The slots
// are equal whole pixels, any pixels left over split between the edges. Drawn at
// `scale`, the display scale. Returns width*height pixels, row 0 at the
// top, bytes R, G, B, A (all opaque).
std::vector<uint32_t> render(const std::vector<std::optional<SparklineReading>> & columns, const SparklineLook & look, unsigned width,
                             unsigned height, const StyleProvider & styles, float scale = 1.0f);

}  // namespace sparkline_image

}  // namespace cellui
